#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <csignal>
#include <sys/wait.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// --- ماکروهای لاگ برای راحتی و خوانایی ---
#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl
#define LOG_DEBUG(msg) std::cout << "[DEBUG] " << msg << std::endl

// ثابت‌ها برای خوانایی بیشتر
constexpr int PORT               = 8080;  // پورت سرور
constexpr int BACKLOG            = 100;   // تعداد کلاینت‌هایی که در صف انتظار می‌مانند
constexpr int BUFFER_SIZE        = 4096;  // اندازه بافر برای خواندن داده
constexpr int MAX_EVENTS         = 100;   // حداکثر رویدادهای epoll در هر بار انتظار
constexpr int CONNECTION_TIMEOUT = 30;    // timeout اتصال (ثانیه)
constexpr int MAX_HEADERS        = 50;    // حداکثر تعداد header ها

// --- ساختار برای نگهداری اطلاعات اتصال ---
struct Connection
{
    int         fd;
    std::string buffer;
    std::string send_buffer; // بافر برای داده‌های باقی‌مانده جهت ارسال (EPOLLOUT)
    time_t      last_activity;
    bool        keep_alive;

    Connection () : fd (-1), last_activity (0), keep_alive (false) {}
    Connection (int socket_fd) : fd (socket_fd), last_activity (time (nullptr)), keep_alive (false) {}
};

// --- کلاس HTTP Parser ---
class HttpParser
{
   private:
    std::string                        method;
    std::string                        path;
    std::string                        version;
    std::map<std::string, std::string> headers;
    std::string                        body;
    bool                               parsing_complete;

   public:
    HttpParser () : parsing_complete (false) {}

    bool parse_request (const std::string& data)
    {
        size_t pos      = 0;
        size_t line_end = data.find ("\r\n");

        if (line_end == std::string::npos)
        {
            return false;  // هنوز کامل نشده
        }

        // Parse request line
        std::string request_line = data.substr (0, line_end);
        if (!parse_request_line (request_line))
        {
            return false;
        }

        pos = line_end + 2;

        // Parse headers
        while (pos < data.length ())
        {
            line_end = data.find ("\r\n", pos);
            if (line_end == std::string::npos)
            {
                return false;  // هنوز کامل نشده
            }

            if (line_end == pos)
            {
                // Empty line - end of headers
                pos = line_end + 2;
                break;
            }

            std::string header_line = data.substr (pos, line_end - pos);
            if (!parse_header (header_line))
            {
                return false;
            }

            pos = line_end + 2;
        }

        // Check if we have body
        if (pos < data.length ())
        {
            body = data.substr (pos);
        }

        parsing_complete = true;
        return true;
    }

    void reset ()
    {
        method.clear ();
        path.clear ();
        version.clear ();
        headers.clear ();
        body.clear ();
        parsing_complete = false;
    }

    // Getters
    const std::string&                        get_method () const { return method; }
    const std::string&                        get_path () const { return path; }
    const std::string&                        get_version () const { return version; }
    const std::map<std::string, std::string>& get_headers () const { return headers; }
    const std::string&                        get_body () const { return body; }
    bool                                      is_complete () const { return parsing_complete; }

   private:
    bool parse_request_line (const std::string& line)
    {
        std::vector<std::string> parts;
        size_t                   start = 0;
        size_t                   end   = line.find (' ');

        while (end != std::string::npos && parts.size () < 3)
        {
            parts.push_back (line.substr (start, end - start));
            start = end + 1;
            end   = line.find (' ', start);
        }

        if (parts.size () < 2)
        {
            return false;
        }

        method  = parts[ 0 ];
        path    = parts[ 1 ];
        version = (parts.size () > 2) ? parts[ 2 ] : "HTTP/1.0";

        return true;
    }

    bool parse_header (const std::string& line)
    {
        size_t colon_pos = line.find (':');
        if (colon_pos == std::string::npos)
        {
            return false;
        }

        std::string key   = line.substr (0, colon_pos);
        std::string value = line.substr (colon_pos + 1);

        // Trim whitespace
        while (!value.empty () && (value[ 0 ] == ' ' || value[ 0 ] == '\t'))
        {
            value.erase (0, 1);
        }

        headers[ key ] = value;
        return true;
    }
};

// --- تابع تنظیم non-blocking mode ---
void set_nonblocking (int sock)
{
    int flags = fcntl (sock, F_GETFL, 0);
    if (flags == -1)
    {
        LOG_ERROR ("fcntl F_GETFL failed");
        return;
    }
    if (fcntl (sock, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        LOG_ERROR ("fcntl F_SETFL failed");
    }
}

// --- تابع ساخت سوکت سرور ---
int create_server_socket ()
{
    int sock = socket (AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        LOG_ERROR ("Socket creation failed");
        exit (1);
    }

    // Set socket options
    int opt = 1;
    if (setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)) < 0)
    {
        LOG_ERROR ("setsockopt SO_REUSEADDR failed");
    }
    // --- اضافه کردن SO_REUSEPORT برای multi-process ---
    if (setsockopt (sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof (opt)) < 0)
    {
        LOG_ERROR ("setsockopt SO_REUSEPORT failed");
    }
    // Set large receive/send buffer (1MB)
    int rcvbuf = 1 << 20;
    int sndbuf = 1 << 20;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
        LOG_ERROR("setsockopt SO_RCVBUF failed");
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
        LOG_ERROR("setsockopt SO_SNDBUF failed");
    }

    set_nonblocking (sock);
    LOG_INFO ("Socket created");
    return sock;
}

// --- تابع بایند کردن سوکت به آدرس و پورت ---
void bind_socket (int sock, sockaddr_in* addr)
{
    if (bind (sock, reinterpret_cast<struct sockaddr*> (addr), sizeof (*addr)) < 0)
    {
        LOG_ERROR ("Bind failed");
        close (sock);
        exit (1);
    }
    LOG_INFO ("Socket bound to port");
}

// --- تابع گوش دادن برای اتصال کلاینت‌ها ---
void listen_socket (int sock, int backlog)
{
    if (listen (sock, backlog) < 0)
    {
        LOG_ERROR ("Listen failed");
        close (sock);
        exit (1);
    }
    LOG_INFO ("Listening for clients...");
}

// --- تابع ارسال پاسخ HTTP بهبود یافته ---
void send_http_response (int client_sock, int status_code, const std::string& content_type, const std::string& body, bool keep_alive = false, Connection* conn_ptr = nullptr, int epoll_fd = -1)
{
    std::string status_text;
    switch (status_code)
    {
        case 200:
            status_text = "OK";
            break;
        case 400:
            status_text = "Bad Request";
            break;
        case 404:
            status_text = "Not Found";
            break;
        case 500:
            status_text = "Internal Server Error";
            break;
        default:
            status_text = "Unknown";
            break;
    }

    std::string response = "HTTP/1.1 " + std::to_string (status_code) + " " + status_text + "\r\n";
    response += "Content-Type: " + content_type + "\r\n";
    response += "Content-Length: " + std::to_string (body.length ()) + "\r\n";

    if (keep_alive)
    {
        response += "Connection: keep-alive\r\n";
    }
    else
    {
        response += "Connection: close\r\n";
    }

    response += "\r\n" + body;

    ssize_t sent = send (client_sock, response.c_str (), response.length (), MSG_NOSIGNAL);
    if (sent < 0)
        sent = 0;
    size_t sent_sz = sent > 0 ? static_cast<size_t>(sent) : 0;
    if (conn_ptr && sent_sz < response.length()) {
        // اگر همه داده ارسال نشد، باقی‌مانده را در send_buffer ذخیره کن و EPOLLOUT فعال کن
        conn_ptr->send_buffer = response.substr(sent_sz);
        epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ev.data.fd = client_sock;
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_sock, &ev);
    } else {
        if (conn_ptr) conn_ptr->send_buffer.clear();
        LOG_INFO ("Response sent: " << status_code);
    }
}

// --- تابع پردازش درخواست HTTP ---
void handle_http_request (Connection& conn, const HttpParser& parser, int epoll_fd)
{
    const std::string&                        method  = parser.get_method ();
    const std::string&                        path    = parser.get_path ();
    const std::map<std::string, std::string>& headers = parser.get_headers ();

    LOG_INFO ("Request: " << method << " " << path);

    // Check for keep-alive
    auto it         = headers.find ("Connection");
    bool keep_alive = (it != headers.end () && it->second == "keep-alive");

    // Simple routing
    if (method == "GET")
    {
        if (path == "/" || path == "/index.html")
        {
            std::string body =
                "<html><body><h1>Welcome to Modular HTTP Server</h1>"
                "<p>This is an event-driven HTTP server using epoll.</p>"
                "<p>Current time: " +
                std::to_string (time (nullptr)) +
                "</p>"
                "</body></html>";
            send_http_response (conn.fd, 200, "text/html", body, keep_alive, &conn, epoll_fd);
        }
        else if (path == "/api/status")
        {
            std::string body = "{\"status\": \"running\", \"server\": \"modular-epoll\"}";
            send_http_response (conn.fd, 200, "application/json", body, keep_alive, &conn, epoll_fd);
        }
        else
        {
            std::string body = "<html><body><h1>404 Not Found</h1></body></html>";
            send_http_response (conn.fd, 404, "text/html", body, keep_alive, &conn, epoll_fd);
        }
    }
    else if (method == "POST")
    {
        if (path == "/api/echo")
        {
            std::string body = "{\"message\": \"Echo: " + parser.get_body () + "\"}";
            send_http_response (conn.fd, 200, "application/json", body, keep_alive, &conn, epoll_fd);
        }
        else
        {
            std::string body = "<html><body><h1>404 Not Found</h1></body></html>";
            send_http_response (conn.fd, 404, "text/html", body, keep_alive, &conn, epoll_fd);
        }
    }
    else
    {
        std::string body = "<html><body><h1>405 Method Not Allowed</h1></body></html>";
        send_http_response (conn.fd, 405, "text/html", body, keep_alive, &conn, epoll_fd);
    }

    conn.keep_alive    = keep_alive;
    conn.last_activity = time (nullptr);
}

// --- تابع بستن اتصال ---
void close_connection (int epoll_fd, Connection& conn)
{
    epoll_ctl (epoll_fd, EPOLL_CTL_DEL, conn.fd, nullptr);
    close (conn.fd);
    LOG_INFO ("Connection closed: " << conn.fd);
}

// --- تابع پاکسازی اتصال‌های timeout شده ---
void cleanup_timeout_connections (int epoll_fd, std::map<int, Connection>& connections)
{
    time_t           now = time (nullptr);
    std::vector<int> to_remove;

    for (auto& pair : connections)
    {
        if (now - pair.second.last_activity > CONNECTION_TIMEOUT)
        {
            to_remove.push_back (pair.first);
        }
    }

    for (int fd : to_remove)
    {
        close_connection (epoll_fd, connections[ fd ]);
        connections.erase (fd);
        LOG_INFO ("Timeout connection removed: " << fd);
    }
}

// --- فلگ سراسری برای graceful shutdown ---
volatile sig_atomic_t stop_server = 0;

// --- سیگنال هندلر برای SIGINT و SIGTERM ---
void handle_signal(int signum) {
    (void)signum; // جلوگیری از هشدار unused parameter
    stop_server = 1;
    LOG_INFO("Graceful shutdown signal received");
}

int main ()
{
    // Ignore SIGPIPE globally
    signal(SIGPIPE, SIG_IGN);
    // --- ثبت سیگنال هندلر برای graceful shutdown ---
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    // --- ساخت سوکت سرور ---
    int server_socket = create_server_socket ();

    // --- تنظیمات آدرس سرور ---
    sockaddr_in server_address;
    std::memset (&server_address, 0, sizeof (server_address));
    server_address.sin_family      = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port        = htons (PORT);

    // --- بایند کردن سوکت به آدرس و پورت ---
    bind_socket (server_socket, &server_address);

    // --- گوش دادن برای اتصال کلاینت‌ها ---
    listen_socket (server_socket, BACKLOG);
    LOG_INFO ("Server is running on port " << PORT);

    // --- Multi-process: ایجاد چندین process با fork ---
    constexpr int NUM_PROCESSES = 4; // تعداد processها (برای تست ۴ کافی است)
    for (int i = 1; i < NUM_PROCESSES; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            LOG_ERROR("fork failed");
            exit(1);
        }
        if (pid == 0) {
            // Child process: فقط حلقه سرور را اجرا می‌کند
            break;
        }
        // Parent process: به حلقه بعدی fork می‌رود
    }
    // هر process (parent و child) از اینجا به بعد حلقه epoll خودش را اجرا می‌کند

    // --- ساخت epoll instance ---
    int epoll_fd = epoll_create1 (0);
    if (epoll_fd == -1)
    {
        LOG_ERROR ("epoll_create1 failed");
        close (server_socket);
        exit (1);
    }

    // --- اضافه کردن سرور سوکت به epoll ---
    epoll_event ev;
#if defined(EPOLLEXCLUSIVE)
    ev.events  = EPOLLIN | EPOLLEXCLUSIVE; // فقط برای سرور سوکت
#else
    ev.events  = EPOLLIN;
#endif
    ev.data.fd = server_socket;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, server_socket, &ev) == -1)
    {
        LOG_ERROR ("epoll_ctl: server_socket");
        close (server_socket);
        close (epoll_fd);
        exit (1);
    }

    // --- نگهداری اتصال‌ها و parser ها ---
    std::map<int, Connection> connections;
    std::map<int, HttpParser> parsers;
    epoll_event               events[ MAX_EVENTS ];

    // --- حلقه اصلی سرور با epoll ---
    while (!stop_server) // اگر سیگنال shutdown آمد، حلقه متوقف می‌شود
    {
        int nfds = epoll_wait (epoll_fd, events, MAX_EVENTS, 1000);  // 1 second timeout
        if (nfds == -1)
        {
            if (errno == EINTR)
            {
                continue;  // Interrupted by signal
            }
            LOG_ERROR ("epoll_wait failed");
            break;
        }

        // Cleanup timeout connections every 10 seconds
        static time_t last_cleanup = 0;
        time_t        now          = time (nullptr);
        if (now - last_cleanup > 10)
        {
            cleanup_timeout_connections (epoll_fd, connections);
            last_cleanup = now;
        }

        for (int n = 0; n < nfds; ++n)
        {
            if (events[ n ].data.fd == server_socket)
            {
                // اتصال جدید
                sockaddr_in client_addr;
                socklen_t   addrlen       = sizeof (client_addr);
                int         client_socket;
                #ifdef SOCK_NONBLOCK
                client_socket = accept4 (server_socket, reinterpret_cast<sockaddr*> (&client_addr), &addrlen, SOCK_NONBLOCK);
                if (client_socket == -1 && (errno == ENOSYS || errno == EINVAL))
                {
                    // Fallback if accept4 not supported
                    client_socket = accept (server_socket, reinterpret_cast<sockaddr*> (&client_addr), &addrlen);
                    if (client_socket >= 0) set_nonblocking(client_socket);
                }
                #else
                client_socket = accept (server_socket, reinterpret_cast<sockaddr*> (&client_addr), &addrlen);
                if (client_socket >= 0) set_nonblocking(client_socket);
                #endif
                if (client_socket >= 0)
                {
                    // Set large receive/send buffer (1MB) for client
                    int rcvbuf = 1 << 20;
                    int sndbuf = 1 << 20;
                    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
                        LOG_ERROR("setsockopt SO_RCVBUF (client) failed");
                    }
                    if (setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
                        LOG_ERROR("setsockopt SO_SNDBUF (client) failed");
                    }
                    // اضافه کردن کلاینت به epoll
                    epoll_event client_ev;
                    client_ev.events  = EPOLLIN | EPOLLET;  // Edge triggered
                    client_ev.data.fd = client_socket;
                    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, client_socket, &client_ev) == -1)
                    {
                        LOG_ERROR ("epoll_ctl: client_socket");
                        close (client_socket);
                    }
                    else
                    {
                        connections[ client_socket ] = Connection (client_socket);
                        parsers[ client_socket ]     = HttpParser ();
                        LOG_INFO ("Client connected: " << client_socket);
                    }
                }
                else
                {
                    LOG_ERROR ("Accept failed");
                }
            }
            else
            {
                int  client_fd = events[ n ].data.fd;
                auto conn_it   = connections.find (client_fd);
                auto parser_it = parsers.find (client_fd);

                if (conn_it == connections.end () || parser_it == parsers.end ())
                {
                    continue;
                }

                Connection& conn   = conn_it->second;
                HttpParser& parser = parser_it->second;

                // اگر EPOLLOUT فعال است و داده‌ای برای ارسال داریم
                if (events[n].events & EPOLLOUT) {
                    if (!conn.send_buffer.empty()) {
                        ssize_t sent = send(client_fd, conn.send_buffer.c_str(), conn.send_buffer.size(), MSG_NOSIGNAL);
                        if (sent < 0) sent = 0;
                        size_t sent_sz = sent > 0 ? static_cast<size_t>(sent) : 0;
                        conn.send_buffer = conn.send_buffer.substr(sent_sz);
                        if (conn.send_buffer.empty()) {
                            // همه داده ارسال شد، EPOLLOUT را حذف کن
                            epoll_event ev_mod;
                            ev_mod.events = EPOLLIN | EPOLLET;
                            ev_mod.data.fd = client_fd;
                            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev_mod);
                        }
                    }
                    // اگر فقط EPOLLOUT بود، ادامه بده (نیازی به خواندن نیست)
                    if (!(events[n].events & EPOLLIN))
                        continue;
                }

                // داده از کلاینت
                char    buffer[ BUFFER_SIZE ];
                ssize_t bytes_read = read (client_fd, buffer, BUFFER_SIZE - 1);

                if (bytes_read <= 0)
                {
                    // کلاینت قطع شد یا خطا
                    close_connection (epoll_fd, conn);
                    connections.erase (client_fd);
                    parsers.erase (client_fd);
                    LOG_INFO ("Client disconnected: " << client_fd);
                }
                else
                {
                    buffer[ bytes_read ] = '\0';
                    conn.buffer += buffer;
                    conn.last_activity = time (nullptr);

                    // Parse HTTP request
                    if (parser.parse_request (conn.buffer))
                    {
                        handle_http_request (conn, parser, epoll_fd);

                        if (!conn.keep_alive && conn.send_buffer.empty())
                        {
                            // اگر keep-alive نیست و چیزی برای ارسال نمانده، اتصال را ببند
                            close_connection (epoll_fd, conn);
                            connections.erase (client_fd);
                            parsers.erase (client_fd);
                        }
                        else if (conn.send_buffer.empty())
                        {
                            // Reset for next request
                            conn.buffer.clear ();
                            parser.reset ();
                        }
                    }
                    else if (conn.buffer.length () > BUFFER_SIZE * 2)
                    {
                        // Buffer too large - close connection
                        LOG_ERROR ("Buffer too large, closing connection: " << client_fd);
                        close_connection (epoll_fd, conn);
                        connections.erase (client_fd);
                        parsers.erase (client_fd);
                    }
                }
            }
        }
    }

    // --- شروع graceful shutdown: بستن همه منابع و اتصالات ---
    LOG_INFO("Shutting down server, closing all connections...");
    for (auto& pair : connections)
    {
        close (pair.first);
    }
    close (server_socket);
    close (epoll_fd);

    // اگر parent process هستیم، منتظر پایان همه childها بمانیم
    #ifdef MULTI_PROCESS
    while (waitpid(-1, nullptr, WNOHANG) > 0) {}
    #endif
    LOG_INFO("Server exited gracefully.");
    return 0;
}