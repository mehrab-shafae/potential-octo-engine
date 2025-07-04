/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#include "RequestHandler.hpp"

#include <ctime>
#include <sstream>

#include "Logger.hpp"
#include "MetricsCollector.hpp"
#include "RateLimiter.hpp"
#include "SlowDown.hpp"

// Global variables
const std::string end_chunk = "0\r\n\r\n";

// Global function implementations
std::string build_http_response(int status_code, std::string_view content_type,
                                std::string_view body, bool keep_alive)
{
    const std::string status_text =
        std::string(RequestHandler::get_status_text(status_code));

    std::string response =
        "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n";
    response += "Content-Type: " + std::string(content_type) + "\r\n";
    response += "Content-Length: " + std::to_string(body.length()) + "\r\n";

    if(keep_alive) { response += "Connection: keep-alive\r\n"; }
    else { response += "Connection: close\r\n"; }

    response += "Server: Aspire/1.0\r\n";
    response += "\r\n";
    response += std::string(body);

    return response;
}

std::string build_chunk(std::string_view data)
{
    std::stringstream ss;
    ss << std::hex << data.length() << "\r\n";
    ss << std::string(data) << "\r\n";
    return ss.str();
}

std::string_view get_status_text(int status_code) noexcept
{
    return RequestHandler::get_status_text(status_code);
}

HttpResponseStatus handle_http_request(const HttpParser& parser,
                                       bool&             keep_alive,
                                       bool&             is_chunked_stream,
                                       std::string& response, Connection* conn,
                                       std::string_view client_ip)
{
    return RequestHandler::handle_http_request(
        parser, keep_alive, is_chunked_stream, response, conn, client_ip);
}

// RequestHandler static member function implementations
std::string_view RequestHandler::get_status_text(int status_code) noexcept
{
    switch(status_code)
    {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 304:
            return "Not Modified";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 408:
            return "Request Timeout";
        case 429:
            return "Too Many Requests";
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        case 505:
            return "HTTP Version Not Supported";
        default:
            return "Unknown";
    }
}

std::string RequestHandler::build_http_response(int              status_code,
                                                std::string_view content_type,
                                                std::string_view body,
                                                bool             keep_alive)
{
    return ::build_http_response(status_code, content_type, body, keep_alive);
}

std::string RequestHandler::build_chunk(std::string_view data)
{
    return ::build_chunk(data);
}

std::string_view RequestHandler::get_end_chunk() noexcept
{
    return end_chunk;
}

HttpResponseStatus RequestHandler::handle_http_request(
    const HttpParser& parser, bool& keep_alive, bool& is_chunked_stream,
    std::string& response, Connection* conn, std::string_view client_ip)
{
    (void)conn;       // Suppress unused parameter warning
    (void)client_ip;  // Suppress unused parameter warning

    // Check for keep-alive header
    auto headers       = parser.get_headers();
    auto connection_it = headers.find("connection");
    if(connection_it != headers.end())
    {
        keep_alive = (connection_it->second == "keep-alive");
    }
    else
    {
        keep_alive = true;  // Default to keep-alive
    }

    is_chunked_stream = false;

    // Handle different HTTP methods
    const std::string method = parser.get_method();
    const std::string path   = parser.get_path();

    if(method == "GET") { return handle_get_request(parser, response); }
    else if(method == "POST") { return handle_post_request(parser, response); }
    else if(method == "HEAD") { return handle_head_request(parser, response); }
    else if(method == "OPTIONS")
    {
        return handle_options_request(parser, response);
    }
    else
    {
        // Method not allowed
        response = build_http_response(405, "text/plain", "Method Not Allowed",
                                       keep_alive);
        return HttpResponseStatus::MethodNotAllowed;
    }
}

HttpResponseStatus RequestHandler::handle_get_request(const HttpParser& parser,
                                                      std::string& response)
{
    const std::string path = parser.get_path();

    if(path == "/" || path == "/index.html")
    {
        std::string html_content = generate_html_response(
            "Aspire Server",
            "<h1>Welcome to Aspire HTTP Server</h1>"
            "<p>This is a high-performance HTTP server built with modern "
            "C++20.</p>"
            "<ul>"
            "<li><a href='/status'>Server Status</a></li>"
            "<li><a href='/info'>Server Info</a></li>"
            "<li><a href='/stream'>Chunked Stream</a></li>"
            "</ul>");

        response = build_http_response(200, "text/html", html_content, true);
        return HttpResponseStatus::OK;
    }
    else if(path == "/status")
    {
        std::string json_content = generate_json_response(
            "{\"status\":\"running\",\"uptime\":\"0\",\"connections\":0}");

        response =
            build_http_response(200, "application/json", json_content, true);
        return HttpResponseStatus::OK;
    }
    else if(path == "/info")
    {
        std::string html_content = generate_html_response(
            "Server Info",
            "<h1>Server Information</h1>"
            "<p><strong>Server:</strong> Aspire HTTP Server</p>"
            "<p><strong>Version:</strong> 1.0</p>"
            "<p><strong>Language:</strong> C++20</p>"
            "<p><strong>Architecture:</strong> Event-driven, Non-blocking "
            "I/O</p>");

        response = build_http_response(200, "text/html", html_content, true);
        return HttpResponseStatus::OK;
    }
    else if(path == "/stream")
    {
        // Start chunked transfer encoding
        std::string header = "HTTP/1.1 200 OK\r\n";
        header += "Content-Type: text/plain\r\n";
        header += "Transfer-Encoding: chunked\r\n";
        header += "Connection: keep-alive\r\n";
        header += "Server: Aspire/1.0\r\n";
        header += "\r\n";

        response = header;
        return HttpResponseStatus::OK;
    }
    else if(path == "/health") { return handle_health_request(response); }
    else if(path == "/metrics") { return handle_metrics_request(response); }
    else if(path == "/rate-limit-status")
    {
        return handle_rate_limit_status("127.0.0.1", response);
    }
    else if(path == "/slow-down-status")
    {
        return handle_slow_down_status("127.0.0.1", response);
    }
    else
    {
        std::string error_content =
            generate_error_response(404, "Page not found");
        response = build_http_response(404, "text/html", error_content, false);
        return HttpResponseStatus::NotFound;
    }
}

HttpResponseStatus RequestHandler::handle_post_request(const HttpParser& parser,
                                                       std::string& response)
{
    const std::string path = parser.get_path();

    if(path == "/echo")
    {
        // Echo back the request body
        std::string body = parser.get_body();
        response         = build_http_response(200, "text/plain", body, true);
        return HttpResponseStatus::OK;
    }
    else
    {
        std::string error_content =
            generate_error_response(404, "Endpoint not found");
        response = build_http_response(404, "text/html", error_content, false);
        return HttpResponseStatus::NotFound;
    }
}

HttpResponseStatus RequestHandler::handle_head_request(const HttpParser& parser,
                                                       std::string& response)
{
    const std::string path = parser.get_path();

    if(path == "/" || path == "/index.html")
    {
        // Return headers only for HEAD request
        response = "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: text/html\r\n";
        response += "Content-Length: 0\r\n";
        response += "Connection: keep-alive\r\n";
        response += "Server: Aspire/1.0\r\n";
        response += "\r\n";

        return HttpResponseStatus::OK;
    }
    else
    {
        response = "HTTP/1.1 404 Not Found\r\n";
        response += "Content-Type: text/html\r\n";
        response += "Content-Length: 0\r\n";
        response += "Connection: close\r\n";
        response += "Server: Aspire/1.0\r\n";
        response += "\r\n";

        return HttpResponseStatus::NotFound;
    }
}

HttpResponseStatus RequestHandler::handle_options_request(
    const HttpParser& parser, std::string& response)
{
    (void)parser;  // Suppress unused parameter warning

    response = "HTTP/1.1 200 OK\r\n";
    response += "Allow: GET, POST, HEAD, OPTIONS\r\n";
    response += "Content-Length: 0\r\n";
    response += "Connection: keep-alive\r\n";
    response += "Server: Aspire/1.0\r\n";
    response += "\r\n";

    return HttpResponseStatus::OK;
}

std::string RequestHandler::generate_html_response(std::string_view title,
                                                   std::string_view content)
{
    std::string html = "<!DOCTYPE html>\n<html>\n<head>\n";
    html += "<title>" + std::string(title) + "</title>\n";
    html += "<meta charset=\"UTF-8\">\n";
    html += "<style>\n";
    html += "body { font-family: Arial, sans-serif; margin: 40px; }\n";
    html += "h1 { color: #333; }\n";
    html += "a { color: #0066cc; text-decoration: none; }\n";
    html += "a:hover { text-decoration: underline; }\n";
    html += "</style>\n";
    html += "</head>\n<body>\n";
    html += std::string(content);
    html += "\n</body>\n</html>";

    return html;
}

std::string RequestHandler::generate_json_response(std::string_view data)
{
    return std::string(data);
}

std::string RequestHandler::generate_error_response(int status_code,
                                                    std::string_view message)
{
    std::string html = "<!DOCTYPE html>\n<html>\n<head>\n";
    html += "<title>Error " + std::to_string(status_code) + "</title>\n";
    html += "<meta charset=\"UTF-8\">\n";
    html += "<style>\n";
    html +=
        "body { font-family: Arial, sans-serif; margin: 40px; text-align: "
        "center; }\n";
    html += "h1 { color: #d32f2f; }\n";
    html += "p { color: #666; }\n";
    html += "</style>\n";
    html += "</head>\n<body>\n";
    html += "<h1>Error " + std::to_string(status_code) + "</h1>\n";
    html += "<p>" + std::string(message) + "</p>\n";
    html += "<p><a href='/'>Return to Home</a></p>\n";
    html += "</body>\n</html>";

    return html;
}

HttpResponseStatus RequestHandler::handle_health_request(std::string& response)
{
    // Simple health check endpoint
    std::string json_content =
        generate_json_response("{\"status\":\"healthy\",\"timestamp\":\"" +
                               std::to_string(std::time(nullptr)) + "\"}");

    response = build_http_response(200, "application/json", json_content, true);
    return HttpResponseStatus::OK;
}

HttpResponseStatus RequestHandler::handle_metrics_request(std::string& response)
{
    // Return metrics in JSON format
    std::string metrics_json = MetricsCollector::instance().get_metrics_json();
    response = build_http_response(200, "application/json", metrics_json, true);
    return HttpResponseStatus::OK;
}

HttpResponseStatus RequestHandler::handle_rate_limit_status(
    std::string_view client_ip, std::string& response)
{
    if(client_ip.empty())
    {
        std::string error_content =
            generate_error_response(400, "Client IP required");
        response = build_http_response(400, "text/html", error_content, false);
        return HttpResponseStatus::BadRequest;
    }

    // Get rate limit statistics for the client
    auto stats = RateLimiter::instance().get_client_stats(client_ip);
    if(!stats)
    {
        std::string json_content = generate_json_response(
            "{\"client\":\"" + std::string(client_ip) +
            "\",\"status\":\"not_found\",\"message\":\"No rate limiting data "
            "for this client\"}");
        response =
            build_http_response(404, "application/json", json_content, true);
        return HttpResponseStatus::NotFound;
    }

    // Format the statistics as JSON
    std::string json_content = "{\"client\":\"" + std::string(client_ip) +
                               "\",\"status\":\"found\",\"data\":\"" +
                               std::string(*stats) + "\"}";
    response = build_http_response(200, "application/json", json_content, true);
    return HttpResponseStatus::OK;
}

HttpResponseStatus RequestHandler::handle_slow_down_status(
    std::string_view client_ip, std::string& response)
{
    if(client_ip.empty())
    {
        std::string error_content =
            generate_error_response(400, "Client IP required");
        response = build_http_response(400, "text/html", error_content, false);
        return HttpResponseStatus::BadRequest;
    }

    // Get slow-down statistics for the client
    auto stats = SlowDown::instance().get_client_stats(client_ip);
    if(!stats)
    {
        std::string json_content =
            generate_json_response("{\"client\":\"" + std::string(client_ip) +
                                   "\",\"status\":\"not_found\",\"message\":"
                                   "\"No slow-down data for this client\"}");
        response =
            build_http_response(404, "application/json", json_content, true);
        return HttpResponseStatus::NotFound;
    }

    // Format the statistics as JSON
    std::string json_content = "{\"client\":\"" + std::string(client_ip) +
                               "\",\"status\":\"found\",\"data\":\"" +
                               std::string(*stats) + "\"}";
    response = build_http_response(200, "application/json", json_content, true);
    return HttpResponseStatus::OK;
}