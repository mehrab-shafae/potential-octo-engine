#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

// ثابت‌ها برای خوانایی بیشتر
constexpr int PORT = 8080;                // پورت سرور
constexpr int BACKLOG = 5;                // تعداد کلاینت‌هایی که در صف انتظار می‌مانند
constexpr int BUFFER_SIZE = 3000;         // اندازه بافر برای خواندن داده

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_address; // ساختار آدرس سرور
    int address_length = sizeof(server_address);

    // 1. ساخت سوکت (Socket)
    // سوکت یک واسط ارتباطی است. اینجا یک سوکت TCP/IPv4 می‌سازیم.
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

    // 2. تنظیمات آدرس سرور
    std::memset(&server_address, 0, sizeof(server_address)); // مقداردهی اولیه صفر
    server_address.sin_family = AF_INET;                     // خانواده آدرس: IPv4
    server_address.sin_addr.s_addr = INADDR_ANY;             // پذیرش اتصال از همه آدرس‌ها
    server_address.sin_port = htons(PORT);                   // تبدیل پورت به فرمت شبکه

    // 3. بایند کردن سوکت به آدرس و پورت
    // باید اشاره‌گر به ساختار آدرس را به تابع بدهیم (تبدیل نوع با reinterpret_cast)
    if (bind(server_socket, reinterpret_cast<struct sockaddr*>(&server_address), sizeof(server_address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_socket);
        return 1;
    }

    // 4. گوش دادن برای اتصال کلاینت‌ها
    if (listen(server_socket, BACKLOG) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_socket);
        return 1;
    }
    std::cout << "Listening on port " << PORT << "..." << std::endl;

    // 5. حلقه اصلی سرور: پذیرش و پاسخ به کلاینت‌ها
    while (true) {
        // پذیرش اتصال جدید
        // این تابع یک اشاره‌گر به ساختار آدرس و طول آن می‌گیرد
        client_socket = accept(server_socket, reinterpret_cast<struct sockaddr*>(&server_address), reinterpret_cast<socklen_t*>(&address_length));
        if (client_socket < 0) {
            std::cerr << "Accept failed" << std::endl;
            continue; // تلاش برای پذیرش کلاینت بعدی
        }

        // خواندن داده از کلاینت
        char buffer[BUFFER_SIZE] = {0}; // بافر برای ذخیره داده دریافتی
        ssize_t bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1); // یک بایت برای پایان رشته
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0'; // اطمینان از پایان رشته
            std::cout << "Received request:\n" << buffer << std::endl;
        }

        // ساخت پاسخ HTTP ساده
        const char* http_response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 13\r\n"
            "\r\n"
            "Hello, World!";

        // ارسال پاسخ به کلاینت
        send(client_socket, http_response, std::strlen(http_response), 0);
        std::cout << "Response sent.\n" << std::endl;

        // بستن اتصال با کلاینت (اما سرور باز می‌ماند)
        close(client_socket);
    }

    // این خط عملاً اجرا نمی‌شود، اما برای کامل بودن کد:
    close(server_socket);
    return 0;
}