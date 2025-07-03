#pragma once
#include <string>
#include <string_view>
#include "HttpParser.hpp"
#include "Connection.hpp"

std::string build_http_response(int status_code, std::string_view content_type, std::string_view body, bool keep_alive = false);
std::string build_chunked_header(bool keep_alive = false);
std::string build_chunk(std::string_view data);
extern const std::string end_chunk;
std::string handle_http_request(const HttpParser& parser, bool& keep_alive, bool& is_chunked_stream, Connection* conn = nullptr); 