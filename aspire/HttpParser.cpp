/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#include "HttpParser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

HttpParser::HttpParser() : parsing_complete_(false) {}

HttpParser::ParseStatus HttpParser::parse_request(std::string_view data)
{
    if(data.empty()) { return ParseStatus::Incomplete; }

    reset();

    size_t pos      = 0;
    size_t line_end = data.find("\r\n");

    if(line_end == std::string_view::npos) { return ParseStatus::Incomplete; }

    std::string_view request_line = data.substr(0, line_end);
    if(!parse_request_line(request_line))
    {
        return ParseStatus::InvalidRequest;
    }

    pos = line_end + 2;

    // Parse headers
    while(pos < data.length())
    {
        line_end = data.find("\r\n", pos);
        if(line_end == std::string_view::npos)
        {
            return ParseStatus::Incomplete;
        }

        if(line_end == pos)
        {
            pos = line_end + 2;
            break;
        }

        std::string_view header_line = data.substr(pos, line_end - pos);
        if(!parse_header(header_line)) { return ParseStatus::InvalidHeaders; }

        pos = line_end + 2;
    }

    // Parse body if present
    if(pos < data.length())
    {
        body_ = std::string(data.substr(pos));

        // Validate content length if present
        if(auto content_length = get_content_length())
        {
            if(static_cast<int>(body_.length()) != *content_length)
            {
                return ParseStatus::InvalidBody;
            }
        }
    }

    parsing_complete_ = true;
    return ParseStatus::Success;
}

void HttpParser::reset() noexcept
{
    method_.clear();
    path_.clear();
    version_.clear();
    headers_.clear();
    body_.clear();
    parsing_complete_ = false;
}

const std::string& HttpParser::get_method() const noexcept
{
    return method_;
}

const std::string& HttpParser::get_path() const noexcept
{
    return path_;
}

const std::string& HttpParser::get_version() const noexcept
{
    return version_;
}

const std::map<std::string, std::string>& HttpParser::get_headers()
    const noexcept
{
    return headers_;
}

const std::string& HttpParser::get_body() const noexcept
{
    return body_;
}

bool HttpParser::is_complete() const noexcept
{
    return parsing_complete_;
}

std::optional<std::string> HttpParser::get_header(
    std::string_view header_name) const
{
    const std::string normalized_name = normalize_header_name(header_name);
    const auto        it              = headers_.find(normalized_name);
    return (it != headers_.end()) ? std::optional<std::string>(it->second)
                                  : std::nullopt;
}

bool HttpParser::has_header(std::string_view header_name) const
{
    const std::string normalized_name = normalize_header_name(header_name);
    return headers_.find(normalized_name) != headers_.end();
}

std::optional<int> HttpParser::get_content_length() const
{
    if(auto content_length_str = get_header("content-length"))
    {
        try
        {
            return std::stoi(*content_length_str);
        }
        catch(...)
        {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool HttpParser::is_valid_request() const noexcept
{
    return parsing_complete_ && !method_.empty() && !path_.empty() &&
           !version_.empty() && is_valid_method(method_) &&
           is_valid_version(version_);
}

bool HttpParser::parse_request_line(std::string_view line)
{
    std::vector<std::string> parts;
    size_t                   start = 0;
    size_t                   end   = line.find(' ');

    while(end != std::string_view::npos && parts.size() < 3)
    {
        parts.emplace_back(line.substr(start, end - start));
        start = end + 1;
        end   = line.find(' ', start);
    }

    if(parts.size() < 2) { return false; }

    method_  = parts[ 0 ];
    path_    = parts[ 1 ];
    version_ = (parts.size() > 2) ? parts[ 2 ] : "HTTP/1.0";

    return is_valid_method(method_) && is_valid_version(version_);
}

bool HttpParser::parse_header(std::string_view line)
{
    size_t colon_pos = line.find(':');
    if(colon_pos == std::string_view::npos) { return false; }

    std::string      key      = std::string(line.substr(0, colon_pos));
    std::string_view value_sv = line.substr(colon_pos + 1);

    // Trim whitespace from value
    value_sv.remove_prefix(
        std::min(value_sv.find_first_not_of(" \t"), value_sv.size()));

    if(key.empty()) { return false; }

    const std::string normalized_key = normalize_header_name(key);
    headers_[ normalized_key ]       = std::string(value_sv);

    return true;
}

bool HttpParser::is_valid_method(std::string_view method) noexcept
{
    static const std::vector<std::string_view> valid_methods = {
        "GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS", "PATCH"};

    return std::find(valid_methods.begin(), valid_methods.end(), method) !=
           valid_methods.end();
}

bool HttpParser::is_valid_version(std::string_view version) noexcept
{
    return version == "HTTP/1.0" || version == "HTTP/1.1" ||
           version == "HTTP/2.0";
}

std::string HttpParser::normalize_header_name(std::string_view header_name)
{
    std::string result(header_name);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}