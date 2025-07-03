/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules.md. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/resource.h>  // برای setrlimit
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>  // برای اندازه‌گیری مدت زمان پردازش
#include <csignal>
#include <cstring>
#include <iostream>
#include <map>
#include <queue>  // برای صف درخواست و پاسخ
#include <string>
#include <thread>  // برای sleep
#include <vector>

#include "Connection.hpp"
#include "HttpParser.hpp"
#include "Logger.hpp"
#include "RequestHandler.hpp"

// --- ماکروهای لاگ برای راحتی و خوانایی ---
// #define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
// #define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl
// #define LOG_DEBUG(msg) std::cout << "[DEBUG] " << msg << std::endl

// ثابت‌ها برای خوانایی بیشتر
constexpr int PORT    = 8080;  // پورت سرور
constexpr int BACKLOG = 100;   // تعداد کلاینت‌هایی که در
                               // صف انتظار می‌مانند
constexpr int BUFFER_SIZE = 4096;  // اندازه بافر برای خواندن داده
constexpr int MAX_EVENTS  = 100;   // حداکثر رویدادهای epoll در هر بار انتظار
constexpr int CONNECTION_TIMEOUT = 30;  // timeout اتصال (ثانیه)
constexpr int MAX_HEADERS        = 50;  // حداکثر تعداد header ها
// --- محدودیت منابع ---
constexpr int MAX_CONNECTIONS = 1024;  // حداکثر تعداد اتصال همزمان
// --- محدودیت pipeline ---
constexpr int MAX_PIPELINE = 10;  // حداکثر تعداد درخواست pipelined در هر اتصال

// --- تابع تنظیم non-blocking mode ---
void set_nonblocking(int sock)
{
    int flags = fcntl(sock, F_GETFL, 0);
    if(flags == -1)
    {
        Logger::instance().error_log("fcntl F_GETFL failed");
        return;
    }
    if(fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        Logger::instance().error_log("fcntl F_SETFL failed");
    }
}

// --- تابع ساخت سوکت سرور ---
int create_server_socket()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1)
    {
        Logger::instance().error_log("Socket creation failed");
        exit(1);
    }

    // Set socket options
    int opt = 1;
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        Logger::instance().error_log("setsockopt SO_REUSEADDR failed");
    }
    // --- اضافه کردن SO_REUSEPORT برای multi-process ---
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        Logger::instance().error_log("setsockopt SO_REUSEPORT failed");
    }
    // Set large receive/send buffer (1MB)
    int rcvbuf = 1 << 20;
    int sndbuf = 1 << 20;
    if(setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0)
    {
        Logger::instance().error_log("setsockopt SO_RCVBUF failed");
    }
    if(setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0)
    {
        Logger::instance().error_log("setsockopt SO_SNDBUF failed");
    }

    set_nonblocking(sock);
    Logger::instance().info_log("Socket created");
    return sock;
}

// --- تابع بایند کردن سوکت به آدرس و پورت ---
void bind_socket(int sock, sockaddr_in* addr)
{
    if(bind(sock, reinterpret_cast<struct sockaddr*>(addr), sizeof(*addr)) < 0)
    {
        Logger::instance().error_log("Bind failed");
        close(sock);
        exit(1);
    }
    Logger::instance().info_log("Socket bound to port");
}

// --- تابع گوش دادن برای اتصال کلاینت‌ها ---
void listen_socket(int sock, int backlog)
{
    if(listen(sock, backlog) < 0)
    {
        Logger::instance().error_log("Listen failed");
        close(sock);
        exit(1);
    }
    Logger::instance().info_log("Listening for clients...");
}

// --- تابع بستن اتصال ---
void close_connection(int epoll_fd, Connection& conn)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn.fd(), nullptr);
    close(conn.fd());
    Logger::instance().info_log("Connection closed: " +
                                std::to_string(conn.fd()));
}

// --- تابع پاکسازی اتصال‌های timeout شده ---
void cleanup_timeout_connections(
    int epoll_fd, std::map<int, std::unique_ptr<Connection>>& connections)
{
    time_t           now = time(nullptr);
    std::vector<int> to_remove;

    for(auto& pair : connections)
    {
        if(now - pair.second->last_activity() > CONNECTION_TIMEOUT)
        {
            to_remove.push_back(pair.first);
        }
    }

    for(int fd : to_remove)
    {
        close_connection(epoll_fd, *connections[ fd ]);
        connections.erase(fd);
        Logger::instance().info_log("Timeout connection removed: " +
                                    std::to_string(fd));
    }
}

// --- فلگ سراسری برای graceful shutdown ---
volatile sig_atomic_t stop_server   = 0;
volatile sig_atomic_t reload_server = 0;

// --- سیگنال هندلر برای SIGINT و SIGTERM ---
void handle_signal(int signum)
{
    (void)signum;  // جلوگیری از هشدار unused parameter
    stop_server = 1;
    Logger::instance().info_log("Graceful shutdown signal received");
}

// --- سیگنال هندلر برای SIGCHLD (جلوگیری از zombie process) ---
void handle_sigchld(int signum)
{
    (void)signum;
    // جمع‌آوری همه zombieها بدون بلاک شدن
    while(waitpid(-1, nullptr, WNOHANG) > 0) {}
}

// --- سیگنال هندلر برای SIGUSR1 (Hot Reload/Restart) ---
void handle_sigusr1(int signum)
{
    (void)signum;
    reload_server = 1;
    Logger::instance().info_log("Hot reload signal (SIGUSR1) received");
}

int main()
{
    // --- محدودیت تعداد فایل دیسکریپتورهای باز ---
    struct rlimit rl;
    rl.rlim_cur = 4096;
    rl.rlim_max = 4096;
    if(setrlimit(RLIMIT_NOFILE, &rl) != 0)
    {
        Logger::instance().error_log("setrlimit RLIMIT_NOFILE failed");
    }
    // Ignore SIGPIPE globally
    signal(SIGPIPE, SIG_IGN);
    // --- ثبت سیگنال هندلر برای graceful shutdown ---
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    // --- ثبت سیگنال هندلر برای SIGCHLD ---
    struct sigaction sa_chld;
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, nullptr);
    // --- ثبت سیگنال هندلر برای SIGUSR1 (Hot Reload) ---
    signal(SIGUSR1, handle_sigusr1);
    // --- ساخت سوکت سرور ---
    int server_socket = create_server_socket();

    // --- تنظیمات آدرس سرور ---
    sockaddr_in server_address;
    std::memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family      = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port        = htons(PORT);

    // --- بایند کردن سوکت به آدرس و پورت ---
    bind_socket(server_socket, &server_address);

    // --- گوش دادن برای اتصال کلاینت‌ها ---
    listen_socket(server_socket, BACKLOG);
    Logger::instance().info_log("Server is running on port " +
                                std::to_string(PORT));

    // --- Multi-process: ایجاد چندین process با fork ---
    constexpr int NUM_PROCESSES = 4;  // تعداد processها (برای تست ۴ کافی است)
    for(int i = 1; i < NUM_PROCESSES; ++i)
    {
        pid_t pid = fork();
        if(pid < 0)
        {
            Logger::instance().error_log("fork failed");
            exit(1);
        }
        if(pid == 0)
        {
            // Child process: فقط حلقه سرور را اجرا
            // می‌کند
            break;
        }
        // Parent process: به حلقه بعدی fork می‌رود
    }
    // هر process (parent و child) از اینجا به بعد حلقه epoll خودش را اجرا
    // می‌کند

    // --- ساخت epoll instance ---
    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1)
    {
        Logger::instance().error_log("epoll_create1 failed");
        close(server_socket);
        exit(1);
    }

    // --- اضافه کردن سرور سوکت به epoll ---
    epoll_event ev;
#if defined(EPOLLEXCLUSIVE)
    ev.events = EPOLLIN | EPOLLEXCLUSIVE;  // فقط برای سرور سوکت
#else
    ev.events = EPOLLIN | EPOLLRDHUP;
#endif
    ev.data.fd = server_socket;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &ev) == -1)
    {
        Logger::instance().error_log("epoll_ctl: server_socket");
        close(server_socket);
        close(epoll_fd);
        exit(1);
    }

    // --- نگهداری اتصال‌ها و parser ها ---
    std::map<int, std::unique_ptr<Connection>> connections;
    std::map<int, HttpParser>                  parsers;
    epoll_event                                events[ MAX_EVENTS ];

    // --- حلقه اصلی سرور با epoll ---
    while(!stop_server && !reload_server)  // اگر سیگنال shutdown یا reload آمد،
                                           // حلقه متوقف می‌شود
    {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS,
                              50);  // 50ms: تعادل latency و مصرف CPU
        if(nfds == -1)
        {
            if(errno == EINTR)
            {
                continue;  // Interrupted by signal
            }
            Logger::instance().error_log("epoll_wait failed");
            break;
        }

        // Cleanup timeout connections every 10 seconds
        static time_t last_cleanup = 0;
        time_t        now          = time(nullptr);
        if(now - last_cleanup > 10)
        {
            cleanup_timeout_connections(epoll_fd, connections);
            last_cleanup = now;
        }

        for(int n = 0; n < nfds; ++n)
        {
            if(events[ n ].data.fd == server_socket)
            {
                // اتصال جدید
                sockaddr_in client_addr;
                socklen_t   addrlen = sizeof(client_addr);
                int         client_socket;
#ifdef SOCK_NONBLOCK
                client_socket = accept4(
                    server_socket, reinterpret_cast<sockaddr*>(&client_addr),
                    &addrlen, SOCK_NONBLOCK);
                if(client_socket == -1 && (errno == ENOSYS || errno == EINVAL))
                {
                    // Fallback if accept4 not supported
                    client_socket = accept(
                        server_socket,
                        reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
                    if(client_socket >= 0) set_nonblocking(client_socket);
                }
#else
                client_socket =
                    accept(server_socket,
                           reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
                if(client_socket >= 0) set_nonblocking(client_socket);
#endif
                if(client_socket >= 0)
                {
                    // --- بررسی محدودیت تعداد اتصال ---
                    if(static_cast<int>(connections.size()) >= MAX_CONNECTIONS)
                    {
                        Logger::instance().error_log(
                            "Connection limit reached, "
                            "closing new client: " +
                            std::to_string(client_socket));
                        close(client_socket);
                        continue;
                    }
                    // Set large receive/send buffer (1MB)
                    // for client
                    int rcvbuf = 1 << 20;
                    int sndbuf = 1 << 20;
                    if(setsockopt(client_socket, SOL_SOCKET, SO_RCVBUF, &rcvbuf,
                                  sizeof(rcvbuf)) < 0)
                    {
                        Logger::instance().error_log(
                            "setsockopt SO_RCVBUF "
                            "(client) failed");
                    }
                    if(setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF, &sndbuf,
                                  sizeof(sndbuf)) < 0)
                    {
                        Logger::instance().error_log(
                            "setsockopt SO_SNDBUF "
                            "(client) failed");
                    }
                    // اضافه کردن کلاینت به epoll
                    epoll_event client_ev;
                    client_ev.events =
                        EPOLLIN | EPOLLET | EPOLLRDHUP;  // Edge triggered +
                                                         // تشخیص قطع اتصال
                    client_ev.data.fd = client_socket;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket,
                                 &client_ev) == -1)
                    {
                        Logger::instance().error_log(
                            "epoll_ctl: client_socket");
                        close(client_socket);
                    }
                    else
                    {
                        connections[ client_socket ] =
                            std::make_unique<Connection>(client_socket);
                        parsers[ client_socket ] = HttpParser();
                        Logger::instance().info_log(
                            "Client connected: " +
                            std::to_string(client_socket));
                    }
                }
                else { Logger::instance().error_log("Accept failed"); }
            }
            else
            {
                int  client_fd = events[ n ].data.fd;
                auto conn_it   = connections.find(client_fd);
                auto parser_it = parsers.find(client_fd);

                if(conn_it == connections.end() || parser_it == parsers.end())
                {
                    continue;
                }

                Connection& conn = *conn_it->second;
                // حذف parser_in_map چون دیگر استفاده
                // نمی‌شود HttpParser& parser_in_map =
                // parser_it->second;

                // اگر EPOLLRDHUP فعال شد یا کلاینت قطع شد
                if(events[ n ].events & EPOLLRDHUP)
                {
                    close_connection(epoll_fd, conn);
                    connections.erase(client_fd);
                    parsers.erase(client_fd);
                    Logger::instance().info_log(
                        "Client disconnected (RDHUP): " +
                        std::to_string(client_fd));
                    continue;
                }
                // خواندن داده در حالت EPOLLET: تا جایی که
                // می‌شود بخوان (برای جلوگیری از
                // wakeup بیهوده)
                bool client_closed = false;
                while(true)
                {
                    char    buffer[ BUFFER_SIZE ];
                    ssize_t bytes_read =
                        read(client_fd, buffer, BUFFER_SIZE - 1);
                    if(bytes_read <= 0)
                    {
                        if(bytes_read == 0 ||
                           (bytes_read < 0 && errno != EAGAIN &&
                            errno != EWOULDBLOCK))
                        {
                            // کلاینت قطع شد یا خطای
                            // جدی
                            close_connection(epoll_fd, conn);
                            connections.erase(client_fd);
                            parsers.erase(client_fd);
                            Logger::instance().info_log(
                                "Client "
                                "disconnected: " +
                                std::to_string(client_fd));
                            client_closed = true;
                        }
                        break;
                    }
                    buffer[ bytes_read ] = '\0';
                    conn.buffer() += buffer;
                    conn.last_activity() = time(nullptr);
                }
                if(client_closed) continue;

                // --- استخراج و صف‌بندی همه
                // درخواست‌های کامل (pipelining)
                // ---
                while(true)
                {
                    HttpParser parser_tmp;
                    if(parser_tmp.parse_request(conn.buffer()))
                    {
                        // --- محدودیت pipeline ---
                        if(static_cast<int>(conn.request_queue().size()) >=
                           MAX_PIPELINE)
                        {
                            std::string err_resp =
                                build_http_response(429, "text/plain",
                                                    "Too Many "
                                                    "Pipelined "
                                                    "Requests",
                                                    false);
                            conn.response_queue().push(err_resp);
                            conn.keep_alive() = false;
                            Logger::instance().error_log(
                                "Pipeline "
                                "limit "
                                "exceeded for "
                                "client: " +
                                std::to_string(client_fd));
                            break;
                        }
                        conn.request_queue().push(parser_tmp);
                        // حذف داده مصرف‌شده
                        // از بافر
                        size_t req_len     = conn.buffer().find("\r\n\r\n");
                        size_t content_len = 0;
                        if(req_len != std::string::npos)
                        {
                            req_len += 4;  // طول \r\n\r\n
                            // اگر body هم هست، باید
                            // Content-Length را هم
                            // در نظر بگیریم
                            auto headers = parser_tmp.get_headers();
                            auto it      = headers.find("Content-Length");
                            if(it != headers.end())
                            {
                                content_len = std::stoul(it->second);
                            }
                            // فقط اگر کل داده (هدر
                            // + body) رسیده باشد،
                            // حذف کن
                            if(conn.buffer().size() >= req_len + content_len)
                            {
                                req_len += content_len;
                                conn.buffer() = conn.buffer().substr(req_len);
                            }
                            else
                            {
                                // هنوز کل داده
                                // نرسیده، منتظر
                                // بمان
                                break;
                            }
                        }
                        else { conn.buffer().clear(); }
                    }
                    else
                    {
                        break;  // دیگر درخواست کامل
                                // نداریم
                    }
                }

                // --- پردازش صف درخواست‌ها و
                // تولید پاسخ (pipelining)
                // ---
                while(!conn.request_queue().empty())
                {
                    HttpParser& parser_in_queue = conn.request_queue().front();
                    bool        keep_alive = false, is_chunked_stream = false;
                    std::string response;
                    // --- Access Log: شروع زمان ---
                    auto t_start = std::chrono::steady_clock::now();
                    handle_http_request(parser_in_queue, keep_alive,
                                        is_chunked_stream, response, &conn);
                    // --- Access Log: پایان زمان و ثبت لاگ ---
                    auto t_end = std::chrono::steady_clock::now();
                    auto duration_ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            t_end - t_start)
                            .count();
                    // استخراج status code از response
                    int    status_code = 0;
                    size_t pos         = response.find(' ');
                    if(pos != std::string::npos)
                    {
                        size_t pos2 = response.find(' ', pos + 1);
                        if(pos2 != std::string::npos)
                        {
                            status_code = std::stoi(
                                response.substr(pos + 1, pos2 - pos - 1));
                        }
                    }
                    std::string log_line = parser_in_queue.get_method() + " " +
                                           parser_in_queue.get_path() + " " +
                                           std::to_string(status_code) + " " +
                                           std::to_string(duration_ms) +
                                           "ms fd=" + std::to_string(client_fd);
                    Logger::instance().access_log(log_line);
                    conn.keep_alive() = keep_alive;
                    if(is_chunked_stream)
                    {
                        conn.response_queue().push(response);
                        conn.chunked_streaming() = true;
                        conn.stream_chunk_idx()  = 1;
                        conn.last_stream_time()  = time(nullptr);
                    }
                    else { conn.response_queue().push(response); }
                    conn.request_queue().pop();
                }

                // --- اگر چیزی برای ارسال هست و EPOLLOUT فعال
                // نیست، فعال کن ---
                if(!conn.response_queue().empty() && conn.send_buffer().empty())
                {
                    conn.send_buffer() = conn.response_queue().front();
                    conn.response_queue().pop();
                    epoll_event ev_mod;
                    ev_mod.events  = EPOLLIN | EPOLLOUT | EPOLLET;
                    ev_mod.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev_mod);
                }

                // اگر EPOLLOUT فعال است و
                // داده‌ای برای ارسال داریم
                if(events[ n ].events & EPOLLOUT)
                {
                    // --- ارسال پاسخ‌های صف
                    // (pipelining)
                    // ---
                    while(!conn.send_buffer().empty())
                    {
                        ssize_t sent =
                            send(client_fd, conn.send_buffer().c_str(),
                                 conn.send_buffer().size(), MSG_NOSIGNAL);
                        if(sent < 0) sent = 0;
                        size_t sent_sz =
                            sent > 0 ? static_cast<size_t>(sent) : 0;
                        conn.send_buffer() = conn.send_buffer().substr(sent_sz);
                        if(!conn.send_buffer().empty())
                            break;  // هنوز داده
                                    // باقی مانده
                        // اگر stream فعال است، chunk
                        // بعدی را ارسال کن
                        if(conn.chunked_streaming())
                        {
                            // هر نیم ثانیه یک chunk
                            // بفرست
                            if(conn.stream_chunk_idx() <= 5 &&
                               time(nullptr) - conn.last_stream_time() >= 1)
                            {
                                std::string chunk_data =
                                    "chunk " +
                                    std::to_string(conn.stream_chunk_idx()) +
                                    "\n";
                                std::string chunk  = build_chunk(chunk_data);
                                conn.send_buffer() = chunk;
                                conn.stream_chunk_idx()++;
                                conn.last_stream_time() = time(nullptr);
                            }
                            else if(conn.stream_chunk_idx() > 5)
                            {
                                conn.send_buffer()       = end_chunk;
                                conn.chunked_streaming() = false;
                            }
                            else
                            {
                                break;  // هنوز
                                        // زمان
                                        // chunk
                                        // بعدی
                                        // نرسیده
                            }
                        }
                        else if(!conn.response_queue().empty())
                        {
                            conn.send_buffer() = conn.response_queue().front();
                            conn.response_queue().pop();
                        }
                    }
                    // اگر همه داده‌ها ارسال شد،
                    // EPOLLOUT را حذف کن
                    if(conn.send_buffer().empty())
                    {
                        epoll_event ev_mod;
                        ev_mod.events  = EPOLLIN | EPOLLET;
                        ev_mod.data.fd = client_fd;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev_mod);
                        // اگر keep-alive خاموش است و صف
                        // پاسخ خالی است، connection را
                        // ببند
                        if(!conn.keep_alive() &&
                           conn.response_queue().empty() &&
                           !conn.chunked_streaming())
                        {
                            close_connection(epoll_fd, conn);
                            connections.erase(client_fd);
                            parsers.erase(client_fd);
                        }
                    }
                    // اگر فقط EPOLLOUT بود، ادامه بده
                    if(!(events[ n ].events & EPOLLIN)) continue;
                }
            }
        }
    }

    // --- شروع graceful shutdown: بستن همه منابع و اتصالات ---
    Logger::instance().info_log(
        "Shutting down server, closing all connections...");
    for(auto& pair : connections)
    {
        // Connection destructor will close fd
        pair.second.reset();
    }
    close(server_socket);
    close(epoll_fd);

    // اگر سیگنال reload دریافت شد، process جدید fork کن
    if(reload_server)
    {
        Logger::instance().info_log("Forking new process for hot reload...");
        pid_t pid = fork();
        if(pid == 0)
        {
            // Child: اجرای مجدد main (با همان socket)
            // execv برای جایگزینی کامل process (در اینجا فقط main
            // را دوباره اجرا می‌کنیم)
            char* argv[] = {const_cast<char*>("./aspire"), nullptr};
            execv(argv[ 0 ], argv);
            // اگر execv شکست خورد:
            Logger::instance().error_log("execv failed for hot reload");
            exit(1);
        }
        else if(pid > 0)
        {
            Logger::instance().info_log(
                "New process forked for hot reload (pid=" +
                std::to_string(pid) + ")");
        }
        else { Logger::instance().error_log("fork failed for hot reload"); }
    }

// اگر parent process هستیم، منتظر پایان همه childها بمانیم
#ifdef MULTI_PROCESS
    while(waitpid(-1, nullptr, WNOHANG) > 0) {}
#endif
    Logger::instance().info_log("Server exited gracefully.");
    return 0;
}