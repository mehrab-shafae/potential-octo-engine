/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#pragma once
#include <optional>
#include <string>
#include <string_view>

#include "Connection.hpp"
#include "HttpParser.hpp"
#include "MetricsCollector.hpp"
#include "RateLimiter.hpp"
#include "SlowDown.hpp"

/**
 * @brief HTTP response status codes for handler functions.
 */
enum class HttpResponseStatus
{
    OK = 0,
    BadRequest,
    NotFound,
    MethodNotAllowed,
    InternalServerError,
    Unknown
};

/**
 * @brief HTTP response structure with modern C++20 features.
 */
struct HttpResponse
{
    int         status_code;
    std::string content_type;
    std::string body;
    bool        keep_alive;
    bool        is_chunked;

    HttpResponse(int code, std::string_view type, std::string_view body_text,
                 bool keep_alive_param = false)
        : status_code(code),
          content_type(std::string(type)),
          body(std::string(body_text)),
          keep_alive(keep_alive_param),
          is_chunked(false)
    {
    }
};

/**
 * @brief HTTP response builder with modern C++20 features.
 *
 * Provides RAII-based response building with automatic
 * header generation and proper error handling.
 */
class HttpResponseBuilder
{
   public:
    /**
     * @brief Construct a new HttpResponseBuilder.
     * @param status_code HTTP status code
     * @param content_type MIME type of the response
     * @param body Response body content
     * @param keep_alive Whether to keep connection alive
     */
    HttpResponseBuilder(int status_code, std::string_view content_type,
                        std::string_view body, bool keep_alive = false)
        : response_(status_code, content_type, body, keep_alive)
    {
    }

    /**
     * @brief Build the HTTP response string.
     * @return Complete HTTP response string
     */
    [[nodiscard]] std::string build() const;

    /**
     * @brief Get the response object.
     * @return Reference to the HttpResponse object
     */
    [[nodiscard]] const HttpResponse& get_response() const noexcept
    {
        return response_;
    }

   private:
    HttpResponse response_;
};

// Forward declarations for global functions
std::string build_http_response(int status_code, std::string_view content_type,
                                std::string_view body, bool keep_alive = false);

std::string build_chunk(std::string_view data);

std::string_view get_status_text(int status_code) noexcept;

HttpResponseStatus handle_http_request(const HttpParser& parser,
                                       bool&             keep_alive,
                                       bool&             is_chunked_stream,
                                       std::string&      response,
                                       Connection*       conn      = nullptr,
                                       std::string_view  client_ip = "");

/**
 * @brief HTTP request handler with modern C++20 features.
 *
 * Handles HTTP requests and generates appropriate responses
 * with proper error handling and RAII principles.
 */
class RequestHandler
{
   public:
    /**
     * @brief Handle an HTTP request and generate a response.
     * @param parser HTTP parser containing the request
     * @param keep_alive Output parameter for keep-alive decision
     * @param is_chunked_stream Output parameter for chunked streaming
     * @param response Output parameter for the response string
     * @param conn Optional connection object for advanced features
     * @param client_ip Optional client IP for rate limiting
     * @return Status of the request handling
     */
    [[nodiscard]] static HttpResponseStatus handle_http_request(
        const HttpParser& parser, bool& keep_alive, bool& is_chunked_stream,
        std::string& response, Connection* conn = nullptr,
        std::string_view client_ip = "");

    /**
     * @brief Build a complete HTTP response.
     * @param status_code HTTP status code
     * @param content_type MIME type
     * @param body Response body
     * @param keep_alive Whether to keep connection alive
     * @return Complete HTTP response string
     */
    [[nodiscard]] static std::string build_http_response(
        int status_code, std::string_view content_type, std::string_view body,
        bool keep_alive = false);

    /**
     * @brief Build a chunked transfer encoding chunk.
     * @param data Chunk data
     * @return Formatted chunk string
     */
    [[nodiscard]] static std::string build_chunk(std::string_view data);

    /**
     * @brief Get the end chunk marker for chunked transfer encoding.
     * @return End chunk string
     */
    [[nodiscard]] static std::string_view get_end_chunk() noexcept;

    /**
     * @brief Get status text for a given status code.
     * @param status_code HTTP status code
     * @return Status text string
     */
    [[nodiscard]] static std::string_view get_status_text(
        int status_code) noexcept;

   private:
    /**
     * @brief Handle GET requests.
     * @param parser HTTP parser
     * @param response Output response
     * @return Status of handling
     */
    [[nodiscard]] static HttpResponseStatus handle_get_request(
        const HttpParser& parser, std::string& response);

    /**
     * @brief Handle POST requests.
     * @param parser HTTP parser
     * @param response Output response
     * @return Status of handling
     */
    [[nodiscard]] static HttpResponseStatus handle_post_request(
        const HttpParser& parser, std::string& response);

    /**
     * @brief Handle HEAD requests.
     * @param parser HTTP parser
     * @param response Output response
     * @return Status of handling
     */
    [[nodiscard]] static HttpResponseStatus handle_head_request(
        const HttpParser& parser, std::string& response);

    /**
     * @brief Handle OPTIONS requests.
     * @param parser HTTP parser
     * @param response Output response
     * @return Status of handling
     */
    [[nodiscard]] static HttpResponseStatus handle_options_request(
        const HttpParser& parser, std::string& response);

    /**
     * @brief Generate a simple HTML response.
     * @param title Page title
     * @param content Page content
     * @return HTML string
     */
    [[nodiscard]] static std::string generate_html_response(
        std::string_view title, std::string_view content);

    /**
     * @brief Generate a JSON response.
     * @param data JSON data
     * @return JSON string
     */
    [[nodiscard]] static std::string generate_json_response(
        std::string_view data);

    /**
     * @brief Generate an error response.
     * @param status_code HTTP status code
     * @param message Error message
     * @return Error response string
     */
    [[nodiscard]] static std::string generate_error_response(
        int status_code, std::string_view message);

    /**
     * @brief Handle health check endpoint.
     * @param response Output response.
     * @return Status of handling.
     */
    [[nodiscard]] static HttpResponseStatus handle_health_request(
        std::string& response);

    /**
     * @brief Handle metrics endpoint.
     * @param response Output response.
     * @return Status of handling.
     */
    [[nodiscard]] static HttpResponseStatus handle_metrics_request(
        std::string& response);

    /**
     * @brief Handle rate limit status endpoint.
     * @param client_ip Client IP address.
     * @param response Output response.
     * @return Status of handling.
     */
    [[nodiscard]] static HttpResponseStatus handle_rate_limit_status(
        std::string_view client_ip, std::string& response);

    /**
     * @brief Handle slow-down status endpoint.
     * @param client_ip Client IP address.
     * @param response Output response.
     * @return Status of handling.
     */
    [[nodiscard]] static HttpResponseStatus handle_slow_down_status(
        std::string_view client_ip, std::string& response);
};

// Global variables for chunked transfer encoding
extern const std::string end_chunk;