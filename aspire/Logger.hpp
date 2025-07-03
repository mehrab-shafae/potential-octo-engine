#pragma once
#include <string>
#include <mutex>

class Logger {
public:
    static Logger& instance();
    void info(const std::string& msg);
    void error(const std::string& msg);
    void debug(const std::string& msg);
    // در آینده: تنظیم مقصد لاگ (فایل/کنسول)
private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    std::mutex mtx;
}; 