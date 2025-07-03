#pragma once
#include <string>
#include <mutex>

class Logger {
public:
    static Logger& instance();
    void info(const std::string& msg);
    void error(const std::string& msg);
    void debug(const std::string& msg);
    void access_log(const std::string& msg);
    // در آینده: تنظیم مقصد لاگ (فایل/کنسول)
private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    std::mutex mtx;
    std::mutex access_log_mtx;
}; 