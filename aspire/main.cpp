#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

// --- ماکروهای لاگ برای راحتی و خوانایی ---
#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl

// ثابت‌ها برای خوانایی بیشتر
constexpr int PORT = 8080;                // پورت سرور
constexpr int BACKLOG = 5;                // تعداد کلاینت‌هایی که در صف انتظار می‌مانند
constexpr int BUFFER_SIZE = 3000;         // اندازه بافر برای خواندن داده

// --- تابع ساخت سوکت سرور ---
// خروجی: عدد صحیح (دسکریپتور سوکت)
int create_server_socket() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        LOG_ERROR("Socket creation failed");
        exit(1);
    }
    LOG_INFO("Socket created");
    return sock;
}

// --- تابع بایند کردن سوکت به آدرس و پورت ---
void bind_socket(int sock, sockaddr_in* addr) {
    if (bind(sock, reinterpret_cast<struct sockaddr*>(addr), sizeof(*addr)) < 0) {
        LOG_ERROR("Bind failed");
        close(sock);
        exit(1);
    }
    LOG_INFO("Socket bound to port");
}

// --- تابع گوش دادن برای اتصال کلاینت‌ها ---
void listen_socket(int sock, int backlog) {
    if (listen(sock, backlog) < 0) {
        LOG_ERROR("Listen failed");
        close(sock);
        exit(1);
    }
    LOG_INFO("Listening for clients...");
}

// --- تابع پذیرش کلاینت جدید ---
int accept_client(int server_sock, sockaddr_in* addr, int* addrlen) {
    int client_sock = accept(server_sock, reinterpret_cast<struct sockaddr*>(addr), reinterpret_cast<socklen_t*>(addrlen));
    if (client_sock < 0) {
        LOG_ERROR("Accept failed");
        return -1;
    }
    LOG_INFO("Client connected");
    return client_sock;
}

// --- تابع ارسال پاسخ HTTP ساده ---
inline void send_http_response(int client_sock, const char* body) {
    const char* header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n";
    size_t body_len = std::strlen(body);
    char response[1024];
    int n = std::snprintf(response, sizeof(response),
        "%sContent-Length: %zu\r\n\r\n%s", header, body_len, body);
    if (n > 0) {
        send(client_sock, response, static_cast<size_t>(n), 0);
    }
    LOG_INFO("Response sent");
}

int main() {
    // --- ساخت سوکت سرور ---
    int server_socket = create_server_socket();

    // --- تنظیمات آدرس سرور ---
    sockaddr_in server_address;
    std::memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    // --- بایند کردن سوکت به آدرس و پورت ---
    bind_socket(server_socket, &server_address);

    // --- گوش دادن برای اتصال کلاینت‌ها ---
    listen_socket(server_socket, BACKLOG);
    LOG_INFO("Server is running on port " << PORT);

    // --- حلقه اصلی سرور ---
    while (true) {
        int address_length = sizeof(server_address);
        int client_socket = accept_client(server_socket, &server_address, &address_length);
        if (client_socket < 0) continue;

        // --- خواندن داده از کلاینت ---
        char buffer[BUFFER_SIZE] = {0};
        ssize_t bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            LOG_INFO("Received request:\n" << buffer);
        }

        // --- ارسال پاسخ HTTP ---
        send_http_response(client_socket, "Hello, Modular World!");

        // --- بستن اتصال با کلاینت ---
        close(client_socket);
    }

    close(server_socket);
    return 0;
}