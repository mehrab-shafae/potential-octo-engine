/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules/*. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#include "Logger.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::info_log(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(mtx);
    std::cout << "[INFO]  " << msg << std::endl;
}

void Logger::error_log(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(mtx);
    std::cerr << "[ERROR] " << msg << std::endl;
}

void Logger::debug_log(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(mtx);
    std::cout << "[DEBUG] " << msg << std::endl;
}

bool Logger::access_log(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(access_log_mtx);
    std::ofstream               ofs("access.log", std::ios::app);
    if(ofs.is_open())
    {
        ofs << msg << std::endl;
        return true;
    }
    return false;
}