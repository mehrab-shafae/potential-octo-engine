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
#include <string>
#include <string_view>

#include "Connection.hpp"
#include "HttpParser.hpp"

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
 * @brief Build a standard HTTP response string.
 * @param status_code HTTP status code.
 * @param content_type Content-Type header value.
 * @param body Response body.
 * @param keep_alive Whether to keep the connection alive.
 * @return HTTP response string.
 */
std::string build_http_response(int status_code, std::string_view content_type,
                                std::string_view body, bool keep_alive = false);

/**
 * @brief Build a chunked transfer encoding HTTP header.
 * @param keep_alive Whether to keep the connection alive.
 * @return HTTP response header string.
 */
std::string build_chunked_header(bool keep_alive = false);

/**
 * @brief Build a chunk for chunked transfer encoding.
 * @param data The chunk data.
 * @return The chunk string.
 */
std::string build_chunk(std::string_view data);

/**
 * @brief End chunk marker for chunked transfer encoding.
 */
extern const std::string end_chunk;

/**
 * @brief Handle an HTTP request and generate a response.
 * @param parser The parsed HTTP request.
 * @param keep_alive Output: whether to keep the connection alive.
 * @param is_chunked_stream Output: whether the response is chunked.
 * @param conn Optional: pointer to the connection object.
 * @param response Output: the generated response string.
 * @return HttpResponseStatus indicating the result.
 */
HttpResponseStatus handle_http_request(const HttpParser& parser,
                                       bool&             keep_alive,
                                       bool&             is_chunked_stream,
                                       std::string&      response,
                                       Connection*       conn = nullptr);