#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

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
void send_http_response (int client_sock, int status_code, const std::string& content_type, const std::string& body, bool keep_alive = false)
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

    send (client_sock, response.c_str (), response.length (), 0);
    LOG_INFO ("Response sent: " << status_code);
}

// --- تابع پردازش درخواست HTTP ---
void handle_http_request (Connection& conn, const HttpParser& parser)
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
            send_http_response (conn.fd, 200, "text/html", body, keep_alive);
        }
        else if (path == "/api/status")
        {
            std::string body = "{\"status\": \"running\", \"server\": \"modular-epoll\"}";
            send_http_response (conn.fd, 200, "application/json", body, keep_alive);
        }
        else
        {
            std::string body = "<html><body><h1>404 Not Found</h1></body></html>";
            send_http_response (conn.fd, 404, "text/html", body, keep_alive);
        }
    }
    else if (method == "POST")
    {
        if (path == "/api/echo")
        {
            std::string body = "{\"message\": \"Echo: " + parser.get_body () + "\"}";
            send_http_response (conn.fd, 200, "application/json", body, keep_alive);
        }
        else
        {
            std::string body = "<html><body><h1>404 Not Found</h1></body></html>";
            send_http_response (conn.fd, 404, "text/html", body, keep_alive);
        }
    }
    else
    {
        std::string body = "<html><body><h1>405 Method Not Allowed</h1></body></html>";
        send_http_response (conn.fd, 405, "text/html", body, keep_alive);
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

int main ()
{
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
    ev.events  = EPOLLIN;
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
    while (true)
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
                int         client_socket = accept (server_socket, reinterpret_cast<sockaddr*> (&client_addr), &addrlen);
                if (client_socket >= 0)
                {
                    set_nonblocking (client_socket);

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
                // داده از کلاینت
                int  client_fd = events[ n ].data.fd;
                auto conn_it   = connections.find (client_fd);
                auto parser_it = parsers.find (client_fd);

                if (conn_it == connections.end () || parser_it == parsers.end ())
                {
                    continue;
                }

                Connection& conn   = conn_it->second;
                HttpParser& parser = parser_it->second;

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
                        handle_http_request (conn, parser);

                        if (!conn.keep_alive)
                        {
                            // Close connection if not keep-alive
                            close_connection (epoll_fd, conn);
                            connections.erase (client_fd);
                            parsers.erase (client_fd);
                        }
                        else
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

    // Cleanup
    for (auto& pair : connections)
    {
        close (pair.first);
    }
    close (server_socket);
    close (epoll_fd);
    return 0;
}