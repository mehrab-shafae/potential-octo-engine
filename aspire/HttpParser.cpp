#include "HttpParser.hpp"
#include <vector>
#include <algorithm>

HttpParser::HttpParser() : parsing_complete(false) {}

bool HttpParser::parse_request(std::string_view data) {
    size_t pos = 0;
    size_t line_end = data.find("\r\n");
    if (line_end == std::string_view::npos) return false;
    std::string_view request_line = data.substr(0, line_end);
    if (!parse_request_line(request_line)) return false;
    pos = line_end + 2;
    // Parse headers
    while (pos < data.length()) {
        line_end = data.find("\r\n", pos);
        if (line_end == std::string_view::npos) return false;
        if (line_end == pos) {
            pos = line_end + 2;
            break;
        }
        std::string_view header_line = data.substr(pos, line_end - pos);
        if (!parse_header(header_line)) return false;
        pos = line_end + 2;
    }
    if (pos < data.length()) body = std::string(data.substr(pos));
    parsing_complete = true;
    return true;
}

void HttpParser::reset() {
    method.clear();
    path.clear();
    version.clear();
    headers.clear();
    body.clear();
    parsing_complete = false;
}

const std::string& HttpParser::get_method() const { return method; }
const std::string& HttpParser::get_path() const { return path; }
const std::string& HttpParser::get_version() const { return version; }
const std::map<std::string, std::string>& HttpParser::get_headers() const { return headers; }
const std::string& HttpParser::get_body() const { return body; }
bool HttpParser::is_complete() const { return parsing_complete; }

bool HttpParser::parse_request_line(std::string_view line) {
    std::vector<std::string> parts;
    size_t start = 0;
    size_t end = line.find(' ');
    while (end != std::string_view::npos && parts.size() < 3) {
        parts.emplace_back(line.substr(start, end - start));
        start = end + 1;
        end = line.find(' ', start);
    }
    if (parts.size() < 2) return false;
    method = parts[0];
    path = parts[1];
    version = (parts.size() > 2) ? parts[2] : "HTTP/1.0";
    return true;
}

bool HttpParser::parse_header(std::string_view line) {
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string_view::npos) return false;
    std::string key = std::string(line.substr(0, colon_pos));
    std::string_view value_sv = line.substr(colon_pos + 1);
    value_sv.remove_prefix(std::min(value_sv.find_first_not_of(" \t"), value_sv.size()));
    headers[key] = std::string(value_sv);
    return true;
} 