/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules/*. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#pragma once
#include <map>
#include <string>
#include <string_view>

/**
 * @brief HTTP request parser class.
 *
 * Parses HTTP requests and provides access to method, path, version, headers,
 * and body.
 */
class HttpParser
{
   private:
    std::string                        method_;
    std::string                        path_;
    std::string                        version_;
    std::map<std::string, std::string> headers_;
    std::string                        body_;
    bool                               parsing_complete_;

   public:
    /**
     * @brief Construct a new HttpParser object.
     */
    HttpParser();
    /**
     * @brief Parse an HTTP request from data.
     * @param data The raw HTTP request data.
     * @return true if parsing was successful, false otherwise.
     */
    bool parse_request(std::string_view data);
    /**
     * @brief Reset the parser state.
     */
    void reset();
    /**
     * @brief Get the HTTP method.
     * @return Reference to the method string.
     */
    const std::string& get_method() const;
    /**
     * @brief Get the HTTP path.
     * @return Reference to the path string.
     */
    const std::string& get_path() const;
    /**
     * @brief Get the HTTP version.
     * @return Reference to the version string.
     */
    const std::string& get_version() const;
    /**
     * @brief Get the HTTP headers.
     * @return Reference to the headers map.
     */
    const std::map<std::string, std::string>& get_headers() const;
    /**
     * @brief Get the HTTP body.
     * @return Reference to the body string.
     */
    const std::string& get_body() const;
    /**
     * @brief Check if parsing is complete.
     * @return true if parsing is complete, false otherwise.
     */
    bool is_complete() const;

   private:
    bool parse_request_line(std::string_view line);
    bool parse_header(std::string_view line);
};