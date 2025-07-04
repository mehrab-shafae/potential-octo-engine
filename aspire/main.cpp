/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/resource.h>  // For setrlimit
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>  // For measuring processing duration
#include <csignal>
#include <cstring>
#include <iostream>
#include <map>
#include <queue>  // For request and response queues
#include <string>
#include <thread>  // For sleep
#include <vector>

#include "Config.hpp"
#include "Connection.hpp"
#include "HttpParser.hpp"
#include "Logger.hpp"
#include "RequestHandler.hpp"

// External declarations for global functions
extern std::string       build_http_response(int              status_code,
                                             std::string_view content_type,
                                             std::string_view body, bool keep_alive);
extern std::string       build_chunk(std::string_view data);
extern const std::string end_chunk;

// --- Log macros for convenience and readability ---
// #define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
// #define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl
// #define LOG_DEBUG(msg) std::cout << "[DEBUG] " << msg << std::endl

// Configuration system will provide these values dynamically

// --- Function to set non-blocking mode ---
void set_nonblocking(int sock)
{
    int flags = fcntl(sock, F_GETFL, 0);
    if(flags == -1)
    {
        (void)Logger::instance().error_log("fcntl F_GETFL failed");
        return;
    }
    if(fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        (void)Logger::instance().error_log("fcntl F_SETFL failed");
    }
}

// --- Function to create server socket ---
int create_server_socket()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1)
    {
        (void)Logger::instance().error_log("Socket creation failed");
        exit(1);
    }

    // Set socket options
    int opt = 1;
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        (void)Logger::instance().error_log("setsockopt SO_REUSEADDR failed");
    }
    // --- Add SO_REUSEPORT for multi-process ---
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        (void)Logger::instance().error_log("setsockopt SO_REUSEPORT failed");
    }
    // Set dynamic receive/send buffer based on system capabilities
    int rcvbuf = Config::instance().get_socket_rcvbuf();
    int sndbuf = Config::instance().get_socket_sndbuf();
    if(setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0)
    {
        (void)Logger::instance().error_log("setsockopt SO_RCVBUF failed");
    }
    if(setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0)
    {
        (void)Logger::instance().error_log("setsockopt SO_SNDBUF failed");
    }

    set_nonblocking(sock);
    (void)Logger::instance().info_log("Socket created");
    return sock;
}

// --- Function to bind socket to address and port ---
void bind_socket(int sock, sockaddr_in* addr)
{
    if(bind(sock, reinterpret_cast<struct sockaddr*>(addr), sizeof(*addr)) < 0)
    {
        (void)Logger::instance().error_log("Bind failed");
        close(sock);
        exit(1);
    }
    (void)Logger::instance().info_log("Socket bound to port");
}

// --- Function to listen for client connections ---
void listen_socket(int sock, int backlog)
{
    if(listen(sock, backlog) < 0)
    {
        (void)Logger::instance().error_log("Listen failed");
        close(sock);
        exit(1);
    }
    (void)Logger::instance().info_log("Listening for clients...");
}

// --- Function to close connection ---
void close_connection(int epoll_fd, Connection& conn)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn.fd(), nullptr);
    conn.close_connection();
    (void)Logger::instance().info_log("Connection closed: " +
                                      std::to_string(conn.fd()));
}

// --- Function to cleanup timeout connections ---
void cleanup_timeout_connections(
    int epoll_fd, std::map<int, std::unique_ptr<Connection>>& connections)
{
    std::vector<int> to_remove;
    int              timeout = Config::instance().get_connection_timeout();

    for(auto& pair : connections)
    {
        if(pair.second->has_timed_out(timeout))
        {
            to_remove.push_back(pair.first);
        }
    }

    for(int fd : to_remove)
    {
        close_connection(epoll_fd, *connections[ fd ]);
        connections.erase(fd);
        (void)Logger::instance().info_log("Timeout connection removed: " +
                                          std::to_string(fd));
    }
}

// --- Global flags for graceful shutdown ---
volatile sig_atomic_t stop_server   = 0;
volatile sig_atomic_t reload_server = 0;

// --- Signal handler for SIGINT and SIGTERM ---
void handle_signal(int signum)
{
    (void)signum;  // Prevent unused parameter warning
    stop_server = 1;
    (void)Logger::instance().info_log("Graceful shutdown signal received");
}

// --- Signal handler for SIGCHLD (prevent zombie processes) ---
void handle_sigchld(int signum)
{
    (void)signum;
    // Collect all zombies without blocking
    while(waitpid(-1, nullptr, WNOHANG) > 0) {}
}

// --- Signal handler for SIGUSR1 (Hot Reload/Restart) ---
void handle_sigusr1(int signum)
{
    (void)signum;
    reload_server = 1;
    (void)Logger::instance().info_log("Hot reload signal (SIGUSR1) received");
}

int main()
{
    // --- Initialize Configuration System ---
    Config& config = Config::instance();

    // Load configuration from file first
    if(config.config_file_exists("aspire.conf"))
    {
        if(config.load_from_file("aspire.conf"))
        {
            (void)Logger::instance().info_log(
                "Configuration loaded from aspire.conf");
        }
        else
        {
            (void)Logger::instance().error_log(
                "Failed to load aspire.conf, using defaults");
        }
    }
    else
    {
        (void)Logger::instance().info_log(
            "No configuration file found, using defaults");
    }

    // Auto-detect system capabilities and set optimal defaults
    if(!config.auto_detect_system())
    {
        (void)Logger::instance().error_log(
            "Failed to auto-detect system capabilities");
        return 1;
    }

    // Print configuration for debugging
    config.print_config();

    // --- Limit number of open file descriptors ---
    struct rlimit rl;
    rl.rlim_cur = static_cast<rlim_t>(config.get_fd_limit());
    rl.rlim_max = static_cast<rlim_t>(config.get_fd_limit());
    if(setrlimit(RLIMIT_NOFILE, &rl) != 0)
    {
        (void)Logger::instance().error_log("setrlimit RLIMIT_NOFILE failed");
    }
    // Ignore SIGPIPE globally
    signal(SIGPIPE, SIG_IGN);
    // --- Register signal handler for graceful shutdown ---
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    // --- Register signal handler for SIGCHLD ---
    struct sigaction sa_chld;
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, nullptr);
    // --- Register signal handler for SIGUSR1 (Hot Reload) ---
    signal(SIGUSR1, handle_sigusr1);
    // --- Create server socket ---
    int server_socket = create_server_socket();

    // --- Server address configuration ---
    sockaddr_in server_address;
    std::memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family      = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(static_cast<uint16_t>(config.get_port()));

    // --- Bind socket to address and port ---
    bind_socket(server_socket, &server_address);

    // --- Listen for client connections ---
    listen_socket(server_socket, config.get_backlog());
    (void)Logger::instance().info_log("Server is running on port " +
                                      std::to_string(config.get_port()));

    // --- Multi-process: create multiple processes with fork ---
    int num_processes = config.get_num_processes();
    for(int i = 1; i < num_processes; ++i)
    {
        pid_t pid = fork();
        if(pid < 0)
        {
            (void)Logger::instance().error_log("fork failed");
            exit(1);
        }
        if(pid == 0)
        {
            // Child process: only runs the server loop
            break;
        }
        // Parent process: continues to next fork iteration
    }
    // Each process (parent and child) runs its own epoll loop from here

    // --- Create epoll instance ---
    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1)
    {
        (void)Logger::instance().error_log("epoll_create1 failed");
        close(server_socket);
        exit(1);
    }

    // --- Add server socket to epoll ---
    epoll_event ev;
#if defined(EPOLLEXCLUSIVE)
    ev.events = EPOLLIN | EPOLLEXCLUSIVE;  // Only for server socket
#else
    ev.events = EPOLLIN | EPOLLRDHUP;
#endif
    ev.data.fd = server_socket;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &ev) == -1)
    {
        (void)Logger::instance().error_log("epoll_ctl: server_socket");
        close(server_socket);
        close(epoll_fd);
        exit(1);
    }

    // --- Maintain connections and parsers ---
    std::map<int, std::unique_ptr<Connection>> connections;
    std::map<int, HttpParser>                  parsers;
    std::vector<epoll_event>                   events(
        static_cast<size_t>(config.get_max_events()));

    // --- Main server loop with epoll ---
    while(!stop_server && !reload_server)  // If shutdown or reload signal
                                           // received, loop stops
    {
        int nfds = epoll_wait(
            epoll_fd, events.data(), static_cast<int>(events.size()),
            config.get_epoll_timeout());  // Dynamic timeout based on system
        if(nfds == -1)
        {
            if(errno == EINTR)
            {
                continue;  // Interrupted by signal
            }
            (void)Logger::instance().error_log("epoll_wait failed");
            break;
        }

        // Cleanup timeout connections every N seconds
        static time_t last_cleanup = 0;
        time_t        now          = time(nullptr);
        if(now - last_cleanup > config.get_cleanup_interval())
        {
            cleanup_timeout_connections(epoll_fd, connections);
            last_cleanup = now;
        }

        for(int n = 0; n < nfds; ++n)
        {
            if(events[ static_cast<size_t>(n) ].data.fd == server_socket)
            {
                // New connection
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
                    // --- Check connection limit ---
                    if(static_cast<int>(connections.size()) >=
                       config.get_max_connections())
                    {
                        (void)Logger::instance().error_log(
                            "Connection limit reached, "
                            "closing new client: " +
                            std::to_string(client_socket));
                        close(client_socket);
                        continue;
                    }
                    // Set dynamic receive/send buffer based on system
                    // capabilities
                    int rcvbuf = config.get_socket_rcvbuf();
                    int sndbuf = config.get_socket_sndbuf();
                    if(setsockopt(client_socket, SOL_SOCKET, SO_RCVBUF, &rcvbuf,
                                  sizeof(rcvbuf)) < 0)
                    {
                        (void)Logger::instance().error_log(
                            "setsockopt SO_RCVBUF "
                            "(client) failed");
                    }
                    if(setsockopt(client_socket, SOL_SOCKET, SO_SNDBUF, &sndbuf,
                                  sizeof(sndbuf)) < 0)
                    {
                        (void)Logger::instance().error_log(
                            "setsockopt SO_SNDBUF "
                            "(client) failed");
                    }
                    // Add client to epoll
                    epoll_event client_ev;
                    client_ev.events = EPOLLIN | EPOLLET |
                                       EPOLLRDHUP;  // Edge triggered +
                                                    // Detect connection close
                    client_ev.data.fd = client_socket;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket,
                                 &client_ev) == -1)
                    {
                        (void)Logger::instance().error_log(
                            "epoll_ctl: client_socket");
                        close(client_socket);
                    }
                    else
                    {
                        connections[ client_socket ] =
                            std::make_unique<Connection>(client_socket);
                        parsers[ client_socket ] = HttpParser();
                        (void)Logger::instance().info_log(
                            "Client connected: " +
                            std::to_string(client_socket));
                    }
                }
                else { (void)Logger::instance().error_log("Accept failed"); }
            }
            else
            {
                int  client_fd = events[ static_cast<size_t>(n) ].data.fd;
                auto conn_it   = connections.find(client_fd);
                auto parser_it = parsers.find(client_fd);

                if(conn_it == connections.end() || parser_it == parsers.end())
                {
                    continue;
                }

                Connection& conn = *conn_it->second;

                // If EPOLLRDHUP is active or client disconnected
                if(events[ static_cast<size_t>(n) ].events & EPOLLRDHUP)
                {
                    close_connection(epoll_fd, conn);
                    connections.erase(client_fd);
                    parsers.erase(client_fd);
                    (void)Logger::instance().info_log(
                        "Client disconnected (RDHUP): " +
                        std::to_string(client_fd));
                    continue;
                }
                // Read data in EPOLLET mode: read as much as possible
                // (to prevent unnecessary wakeups)
                bool client_closed = false;
                while(true)
                {
                    std::vector<char> buffer(static_cast<size_t>(
                        Config::instance().get_buffer_size()));
                    ssize_t           bytes_read =
                        read(client_fd, buffer.data(),
                             static_cast<size_t>(
                                 Config::instance().get_buffer_size() - 1));
                    if(bytes_read <= 0)
                    {
                        if(bytes_read == 0 ||
                           (bytes_read < 0 && errno != EAGAIN &&
                            errno != EWOULDBLOCK))
                        {
                            // Client disconnected or serious error
                            close_connection(epoll_fd, conn);
                            connections.erase(client_fd);
                            parsers.erase(client_fd);
                            (void)Logger::instance().info_log(
                                "Client "
                                "disconnected: " +
                                std::to_string(client_fd));
                            client_closed = true;
                        }
                        break;
                    }
                    buffer[ static_cast<size_t>(bytes_read) ] = '\0';
                    conn.buffer() += std::string(
                        buffer.data(), static_cast<size_t>(bytes_read));
                    conn.last_activity() = time(nullptr);
                }
                if(client_closed) continue;

                // --- Extract and queue all complete requests (pipelining) ---
                while(true)
                {
                    HttpParser parser_tmp;
                    if(parser_tmp.parse_request(conn.buffer()) ==
                       HttpParser::ParseStatus::Success)
                    {
                        // --- Pipeline limit ---
                        if(static_cast<int>(conn.request_queue().size()) >=
                           config.get_max_pipeline())
                        {
                            std::string err_resp =
                                build_http_response(429, "text/plain",
                                                    "Too Many "
                                                    "Pipelined "
                                                    "Requests",
                                                    false);
                            conn.response_queue().push(err_resp);
                            conn.keep_alive() = false;
                            (void)Logger::instance().error_log(
                                "Pipeline "
                                "limit "
                                "exceeded for "
                                "client: " +
                                std::to_string(client_fd));
                            break;
                        }
                        conn.request_queue().push(parser_tmp);
                        // Remove consumed data from buffer
                        size_t req_len     = conn.buffer().find("\r\n\r\n");
                        size_t content_len = 0;
                        if(req_len != std::string::npos)
                        {
                            req_len += 4;  // Length of \r\n\r\n
                            // If body exists, must also consider Content-Length
                            auto headers = parser_tmp.get_headers();
                            auto it      = headers.find("content-length");
                            if(it != headers.end())
                            {
                                try
                                {
                                    content_len = std::stoul(it->second);
                                }
                                catch(...)
                                {
                                    content_len = 0;
                                }
                            }
                            // Only remove if all data (header + body) has
                            // arrived
                            if(conn.buffer().size() >= req_len + content_len)
                            {
                                req_len += content_len;
                                conn.buffer() = conn.buffer().substr(req_len);
                            }
                            else
                            {
                                // Not all data has arrived yet, wait
                                break;
                            }
                        }
                        else { conn.buffer().clear(); }
                    }
                    else
                    {
                        break;  // No more complete requests
                    }
                }

                // --- Process request queue and generate responses (pipelining)
                // ---
                while(!conn.request_queue().empty())
                {
                    HttpParser& parser_in_queue = conn.request_queue().front();
                    bool        keep_alive = false, is_chunked_stream = false;
                    std::string response;
                    // --- Access Log: start time ---
                    auto t_start = std::chrono::steady_clock::now();
                    handle_http_request(parser_in_queue, keep_alive,
                                        is_chunked_stream, response, &conn);
                    // --- Access Log: end time and log entry ---
                    auto t_end = std::chrono::steady_clock::now();
                    auto duration_ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            t_end - t_start)
                            .count();
                    // Extract status code from response
                    int    status_code = 0;
                    size_t pos         = response.find(' ');
                    if(pos != std::string::npos)
                    {
                        size_t pos2 = response.find(' ', pos + 1);
                        if(pos2 != std::string::npos)
                        {
                            try
                            {
                                status_code = std::stoi(
                                    response.substr(pos + 1, pos2 - pos - 1));
                            }
                            catch(...)
                            {
                                status_code = 0;
                            }
                        }
                    }
                    std::string log_line = parser_in_queue.get_method() + " " +
                                           parser_in_queue.get_path() + " " +
                                           std::to_string(status_code) + " " +
                                           std::to_string(duration_ms) +
                                           "ms fd=" + std::to_string(client_fd);
                    (void)Logger::instance().access_log(log_line);
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

                // --- If there's something to send and EPOLLOUT is not active,
                // activate it ---
                if(!conn.response_queue().empty() && conn.send_buffer().empty())
                {
                    conn.send_buffer() = conn.response_queue().front();
                    conn.response_queue().pop();
                    epoll_event ev_mod;
                    ev_mod.events  = EPOLLIN | EPOLLOUT | EPOLLET;
                    ev_mod.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev_mod);
                }

                // If EPOLLOUT is active and we have data to send
                if(events[ static_cast<size_t>(n) ].events & EPOLLOUT)
                {
                    // --- Send queued responses (pipelining) ---
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
                            break;  // Still have data remaining
                        // If stream is active, send next chunk
                        if(conn.chunked_streaming())
                        {
                            // Send one chunk every half second
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
                                break;  // Not yet time for next chunk
                            }
                        }
                        else if(!conn.response_queue().empty())
                        {
                            conn.send_buffer() = conn.response_queue().front();
                            conn.response_queue().pop();
                        }
                    }
                    // If all data sent, remove EPOLLOUT
                    if(conn.send_buffer().empty())
                    {
                        epoll_event ev_mod;
                        ev_mod.events  = EPOLLIN | EPOLLET;
                        ev_mod.data.fd = client_fd;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev_mod);
                        // If keep-alive is off and response queue is empty,
                        // close connection
                        if(!conn.keep_alive() &&
                           conn.response_queue().empty() &&
                           !conn.chunked_streaming())
                        {
                            close_connection(epoll_fd, conn);
                            connections.erase(client_fd);
                            parsers.erase(client_fd);
                        }
                    }
                    // If only EPOLLOUT was active, continue
                    if(!(events[ static_cast<size_t>(n) ].events & EPOLLIN))
                        continue;
                }
            }
        }
    }

    // --- Start graceful shutdown: close all resources and connections ---
    (void)Logger::instance().info_log(
        "Shutting down server, closing all connections...");
    for(auto& pair : connections)
    {
        // Connection destructor will close fd
        pair.second.reset();
    }
    close(server_socket);
    close(epoll_fd);

    // If reload signal received, fork new process
    if(reload_server)
    {
        (void)Logger::instance().info_log(
            "Forking new process for hot reload...");
        pid_t pid = fork();
        if(pid == 0)
        {
            // Child: restart main (with same socket)
            // execv for complete process replacement (here we just restart
            // main)
            char* argv[] = {const_cast<char*>("./aspire"), nullptr};
            execv(argv[ 0 ], argv);
            // If execv fails:
            (void)Logger::instance().error_log("execv failed for hot reload");
            exit(1);
        }
        else if(pid > 0)
        {
            (void)Logger::instance().info_log(
                "New process forked for hot reload (pid=" +
                std::to_string(pid) + ")");
        }
        else
        {
            (void)Logger::instance().error_log("fork failed for hot reload");
        }
    }

// If we are parent process, wait for all children to finish
#ifdef MULTI_PROCESS
    while(waitpid(-1, nullptr, WNOHANG) > 0) {}
#endif
    (void)Logger::instance().info_log("Server exited gracefully.");
    return 0;
}