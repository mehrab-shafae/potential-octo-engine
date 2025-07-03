#pragma once
#include <string>
#include <string_view>
#include <map>

class HttpParser {
private:
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
    bool parsing_complete;
public:
    HttpParser();
    bool parse_request(std::string_view data);
    void reset();
    const std::string& get_method() const;
    const std::string& get_path() const;
    const std::string& get_version() const;
    const std::map<std::string, std::string>& get_headers() const;
    const std::string& get_body() const;
    bool is_complete() const;
private:
    bool parse_request_line(std::string_view line);
    bool parse_header(std::string_view line);
}; 