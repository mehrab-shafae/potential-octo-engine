/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules.md. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#pragma once
#include <mutex>
#include <string>

/**
 * @brief Thread-safe logger class (singleton).
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
     */
    void info_log(const std::string& msg);
    /**
     * @brief Log an error message.
     * @param msg The message to log.
     */
    void error_log(const std::string& msg);
    /**
     * @brief Log a debug message.
     * @param msg The message to log.
     */
    void debug_log(const std::string& msg);
    /**
     * @brief Log an access message to access.log.
     * @param msg The message to log.
     * @return true if log was successful, false otherwise.
     */
    bool access_log(const std::string& msg);
    // در آینده: تنظیم مقصد لاگ (فایل/کنسول)

   private:
    Logger()                            = default;
    Logger(const Logger&)               = delete;
    Logger&    operator=(const Logger&) = delete;
    std::mutex mtx;
    std::mutex access_log_mtx;
};