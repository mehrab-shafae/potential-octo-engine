#include "RequestHandler.hpp"

#include <ctime>

std::string build_http_response(int status_code, std::string_view content_type,
                                std::string_view body, bool keep_alive)
{
    std::string status_text;
    switch(status_code)
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
        case 405:
            status_text = "Method Not Allowed";
            break;
        case 500:
            status_text = "Internal Server Error";
            break;
        default:
            status_text = "Unknown";
            break;
    }
    std::string response =
        "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n";
    response += "Content-Type: " + std::string(content_type) + "\r\n";
    response += "Content-Length: " + std::to_string(body.length()) + "\r\n";
    response +=
        keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    response += "\r\n" + std::string(body);
    return response;
}

std::string build_chunked_header(bool keep_alive)
{
    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Transfer-Encoding: chunked\r\n";
    response +=
        keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    response += "\r\n";
    return response;
}

std::string build_chunk(std::string_view data)
{
    return std::to_string(data.size()) + "\r\n" + std::string(data) + "\r\n";
}

const std::string end_chunk = "0\r\n\r\n";

std::string handle_http_request(const HttpParser& parser, bool& keep_alive,
                                bool& is_chunked_stream, Connection* conn)
{
    const std::string& method  = parser.get_method();
    const std::string& path    = parser.get_path();
    const auto&        headers = parser.get_headers();
    // Check for keep-alive
    auto it           = headers.find("Connection");
    keep_alive        = (it != headers.end() && it->second == "keep-alive");
    is_chunked_stream = false;
    if(method == "GET")
    {
        if(path == "/" || path == "/index.html")
        {
            std::string body =
                "<html><body><h1>Welcome to Modular HTTP "
                "Server</h1>"
                "<p>This is an event-driven HTTP server using "
                "epoll.</p>"
                "<p>Current time: " +
                std::to_string(time(nullptr)) +
                "</p>"
                "</body></html>";
            return build_http_response(200, "text/html", body, keep_alive);
        }
        else if(path == "/api/status")
        {
            std::string body =
                "{\"status\": \"running\", \"server\": "
                "\"modular-epoll\"}";
            return build_http_response(200, "application/json", body,
                                       keep_alive);
        }
        else if(path == "/api/stream")
        {
            is_chunked_stream = true;
            if(conn) conn->chunked_streaming() = true;
            return build_chunked_header(keep_alive);
        }
        else
        {
            std::string body =
                "<html><body><h1>404 Not Found</h1></body></html>";
            return build_http_response(404, "text/html", body, keep_alive);
        }
    }
    else if(method == "POST")
    {
        if(path == "/api/echo")
        {
            std::string body =
                "{\"message\": \"Echo: " + parser.get_body() + "\"}";
            return build_http_response(200, "application/json", body,
                                       keep_alive);
        }
        else
        {
            std::string body =
                "<html><body><h1>404 Not Found</h1></body></html>";
            return build_http_response(404, "text/html", body, keep_alive);
        }
    }
    else
    {
        std::string body =
            "<html><body><h1>405 Method Not Allowed</h1></body></html>";
        return build_http_response(405, "text/html", body, keep_alive);
    }
}