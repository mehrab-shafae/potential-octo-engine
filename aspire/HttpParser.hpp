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
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief HTTP request parser class with modern C++20 features.
 *
 * Parses HTTP requests and provides access to method, path, version, headers,
 * and body. Uses RAII principles and provides better error handling.
 */
class HttpParser
{
   public:
    /**
     * @brief HTTP parsing result status.
     */
    enum class ParseStatus
    {
        Success,
        Incomplete,
        InvalidRequest,
        InvalidHeaders,
        InvalidBody
    };

    /**
     * @brief Construct a new HttpParser object.
     */
    HttpParser();

    /**
     * @brief Parse an HTTP request from data.
     * @param data The raw HTTP request data.
     * @return ParseStatus indicating the result of parsing.
     */
    [[nodiscard]] ParseStatus parse_request(std::string_view data);

    /**
     * @brief Reset the parser state.
     */
    void reset() noexcept;

    /**
     * @brief Get the HTTP method.
     * @return Reference to the method string.
     */
    [[nodiscard]] const std::string& get_method() const noexcept;

    /**
     * @brief Get the HTTP path.
     * @return Reference to the path string.
     */
    [[nodiscard]] const std::string& get_path() const noexcept;

    /**
     * @brief Get the HTTP version.
     * @return Reference to the version string.
     */
    [[nodiscard]] const std::string& get_version() const noexcept;

    /**
     * @brief Get the HTTP headers.
     * @return Reference to the headers map.
     */
    [[nodiscard]] const std::map<std::string, std::string>& get_headers()
        const noexcept;

    /**
     * @brief Get the HTTP body.
     * @return Reference to the body string.
     */
    [[nodiscard]] const std::string& get_body() const noexcept;

    /**
     * @brief Check if parsing is complete.
     * @return true if parsing is complete, false otherwise.
     */
    [[nodiscard]] bool is_complete() const noexcept;

    /**
     * @brief Get a specific header value.
     * @param header_name The name of the header to get.
     * @return Optional string containing the header value.
     */
    [[nodiscard]] std::optional<std::string> get_header(
        std::string_view header_name) const;

    /**
     * @brief Check if a specific header exists.
     * @param header_name The name of the header to check.
     * @return true if header exists, false otherwise.
     */
    [[nodiscard]] bool has_header(std::string_view header_name) const;

    /**
     * @brief Get the content length from headers.
     * @return Optional integer containing the content length.
     */
    [[nodiscard]] std::optional<int> get_content_length() const;

    /**
     * @brief Validate the parsed request.
     * @return true if request is valid, false otherwise.
     */
    [[nodiscard]] bool is_valid_request() const noexcept;

   private:
    std::string                        method_;
    std::string                        path_;
    std::string                        version_;
    std::map<std::string, std::string> headers_;
    std::string                        body_;
    bool                               parsing_complete_;

    /**
     * @brief Parse the request line.
     * @param line The request line to parse.
     * @return true if parsing was successful, false otherwise.
     */
    [[nodiscard]] bool parse_request_line(std::string_view line);

    /**
     * @brief Parse a single header line.
     * @param line The header line to parse.
     * @return true if parsing was successful, false otherwise.
     */
    [[nodiscard]] bool parse_header(std::string_view line);

    /**
     * @brief Validate HTTP method.
     * @param method The method to validate.
     * @return true if method is valid, false otherwise.
     */
    [[nodiscard]] static bool is_valid_method(std::string_view method) noexcept;

    /**
     * @brief Validate HTTP version.
     * @param version The version to validate.
     * @return true if version is valid, false otherwise.
     */
    [[nodiscard]] static bool is_valid_version(
        std::string_view version) noexcept;

    /**
     * @brief Normalize header name (lowercase).
     * @param header_name The header name to normalize.
     * @return Normalized header name.
     */
    [[nodiscard]] static std::string normalize_header_name(
        std::string_view header_name);
};