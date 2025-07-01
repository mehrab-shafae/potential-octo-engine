#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    const int PORT = 8080;

    // 1. ساخت سوکت
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        std::cerr << "Socket failed" << std::endl;
        return 1;
    }

    // 2. تنظیمات آدرس
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 3. بایند کردن سوکت به پورت
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        return 1;
    }

    // 4. گوش دادن برای اتصال
    if (listen(server_fd, 1) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        return 1;
    }
    std::cout << "Listening on port " << PORT << "..." << std::endl;

    // 5. پذیرش اولین اتصال
    client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (client_fd < 0) {
        std::cerr << "Accept failed" << std::endl;
        close(server_fd);
        return 1;
    }

    // 6. خواندن درخواست کلاینت
    char buffer[3000] = {0};
    int valread = read(client_fd, buffer, 2999);
    std::cout << "Received request:\n" << buffer << std::endl;

    // 7. ساخت پاسخ HTTP ساده
    const char* http_response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "Hello, World!";

    // 8. ارسال پاسخ
    send(client_fd, http_response, strlen(http_response), 0);
    std::cout << "Response sent." << std::endl;

    // 9. بستن اتصال
    close(client_fd);
    close(server_fd);
    return 0;
}