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
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

/**
 * @brief Thread-safe logger class (singleton) with modern C++20 features.
 *
 * Provides RAII-based logging with automatic file management and
 * thread-safe operations.
 */
class Logger
{
   public:
    /**
     * @brief Get the singleton instance of Logger.
     * @return Reference to Logger instance.
     */
    static Logger& instance();

    /**
     * @brief Log an info message.
     * @param msg The message to log.
     * @return true if logged successfully, false otherwise.
     */
    [[nodiscard]] bool info_log(std::string_view msg);

    /**
     * @brief Log an error message.
     * @param msg The message to log.
     * @return true if logged successfully, false otherwise.
     */
    [[nodiscard]] bool error_log(std::string_view msg);

    /**
     * @brief Log a debug message.
     * @param msg The message to log.
     * @return true if logged successfully, false otherwise.
     */
    [[nodiscard]] bool debug_log(std::string_view msg);

    /**
     * @brief Log an access message to access.log.
     * @param msg The message to log.
     * @return true if log was successful, false otherwise.
     */
    [[nodiscard]] bool access_log(std::string_view msg);

    /**
     * @brief Set the log file path for access logs.
     * @param file_path The path to the log file.
     * @return true if set successfully, false otherwise.
     */
    [[nodiscard]] bool set_access_log_file(std::string_view file_path);

    /**
     * @brief Get the current access log file path.
     * @return The current access log file path.
     */
    [[nodiscard]] std::string get_access_log_file() const;

    /**
     * @brief Enable or disable logging.
     * @param enable Whether to enable logging.
     */
    void enable_logging(bool enable) noexcept;

    /**
     * @brief Check if logging is enabled.
     * @return true if logging is enabled, false otherwise.
     */
    [[nodiscard]] bool is_logging_enabled() const noexcept;

   private:
    Logger()                         = default;
    ~Logger()                        = default;
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief Get current timestamp as string.
     * @return Current timestamp string.
     */
    [[nodiscard]] std::string get_timestamp() const;

    /**
     * @brief Write message to console with timestamp.
     * @param level The log level.
     * @param msg The message to log.
     * @return true if logged successfully, false otherwise.
     */
    [[nodiscard]] bool write_to_console(std::string_view level,
                                        std::string_view msg);

    /**
     * @brief Write message to file with timestamp.
     * @param file_path The file path.
     * @param msg The message to log.
     * @return true if logged successfully, false otherwise.
     */
    [[nodiscard]] bool write_to_file(std::string_view file_path,
                                     std::string_view msg);

    mutable std::mutex mtx_;
    mutable std::mutex access_log_mtx_;
    std::string        access_log_file_ = "access.log";
    bool               logging_enabled_ = true;
};