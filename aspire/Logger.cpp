#include "Logger.hpp"
#include <iostream>
#include <chrono>
#include <ctime>
#include <fstream>

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::info(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx);
    std::cout << "[INFO]  " << msg << std::endl;
}

void Logger::error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx);
    std::cerr << "[ERROR] " << msg << std::endl;
}

void Logger::debug(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx);
    std::cout << "[DEBUG] " << msg << std::endl;
}

void Logger::access_log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(access_log_mtx);
    std::ofstream ofs("access.log", std::ios::app);
    if (ofs.is_open()) {
        ofs << msg << std::endl;
    }
} 