/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#include "Logger.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

bool Logger::info_log(std::string_view msg)
{
    if(!logging_enabled_) { return false; }

    return write_to_console("INFO", msg);
}

bool Logger::error_log(std::string_view msg)
{
    if(!logging_enabled_) { return false; }

    return write_to_console("ERROR", msg);
}

bool Logger::debug_log(std::string_view msg)
{
    if(!logging_enabled_) { return false; }

    return write_to_console("DEBUG", msg);
}

bool Logger::access_log(std::string_view msg)
{
    if(!logging_enabled_) { return false; }

    return write_to_file("ACCESS", msg);
}

bool Logger::write_to_console(std::string_view level, std::string_view msg)
{
    try
    {
        const std::string timestamp = get_timestamp();
        std::cout << "[" << timestamp << "] [" << level << "] " << msg
                  << std::endl;
        return true;
    }
    catch(...)
    {
        return false;
    }
}

bool Logger::write_to_file(std::string_view level, std::string_view msg)
{
    try
    {
        const std::string timestamp = get_timestamp();
        const std::string log_entry = "[" + timestamp + "] [" +
                                      std::string(level) + "] " +
                                      std::string(msg) + "\n";

        std::ofstream log_file{"access.log", std::ios::app};
        if(log_file.is_open())
        {
            log_file << log_entry;
            return true;
        }
        return false;
    }
    catch(...)
    {
        return false;
    }
}

std::string Logger::get_timestamp() const
{
    const auto now        = std::chrono::system_clock::now();
    const auto time_point = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_point), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void Logger::enable_logging(bool enable) noexcept
{
    logging_enabled_ = enable;
}

bool Logger::is_logging_enabled() const noexcept
{
    return logging_enabled_;
}