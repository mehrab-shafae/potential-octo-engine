/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#include "Config.hpp"

#include <sys/sysinfo.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

// For reading CPU information
#ifdef __linux__
#include <fstream>
#include <string>
#endif

#include "Logger.hpp"

Config& Config::instance()
{
    static Config config;
    return config;
}

bool Config::load_from_file(const std::string& config_path)
{
    std::lock_guard<std::mutex> lock(mtx_);

    std::ifstream file(config_path);
    if(!file.is_open())
    {
        return false;  // File does not exist, use defaults
    }

    std::string line;
    while(std::getline(file, line))
    {
        // Remove comments and whitespace
        size_t comment_pos = line.find('#');
        if(comment_pos != std::string::npos)
        {
            line = line.substr(0, comment_pos);
        }

        // Remove whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);

        if(line.empty()) continue;

        // parse key=value
        size_t equal_pos = line.find('=');
        if(equal_pos != std::string::npos)
        {
            std::string key   = line.substr(0, equal_pos);
            std::string value = line.substr(equal_pos + 1);

            // Remove whitespace from key and value
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            config_[ key ] = value;
        }
    }

    // Apply loaded configuration values to actual variables
    apply_loaded_config();

    return true;
}

void Config::apply_loaded_config()
{
    // Helper function to safely parse integer values
    auto parse_int = [](const std::string& value, int default_val) -> int
    {
        try
        {
            return std::stoi(value);
        }
        catch(...)
        {
            return default_val;
        }
    };

    // Apply loaded values to configuration variables
    if(config_.find("port") != config_.end())
    {
        int new_port = parse_int(config_[ "port" ], 8080);
        if(new_port >= 1 && new_port <= 65535) { port_ = new_port; }
        else
        {
            Logger::instance().error_log(
                "Invalid port number: " + config_[ "port" ] +
                ", using default: 8080");
        }
    }

    if(config_.find("backlog") != config_.end())
    {
        int new_backlog = parse_int(config_[ "backlog" ], 100);
        if(new_backlog >= 1 && new_backlog <= 1000) { backlog_ = new_backlog; }
        else
        {
            Logger::instance().error_log(
                "Invalid backlog value: " + config_[ "backlog" ] +
                ", using default: 100");
        }
    }

    if(config_.find("buffer_size") != config_.end())
    {
        int new_buffer_size = parse_int(config_[ "buffer_size" ], 4096);
        if(new_buffer_size >= 1024 && new_buffer_size <= 65536)
        {
            buffer_size_ = new_buffer_size;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid buffer_size: " + config_[ "buffer_size" ] +
                ", using default: 4096");
        }
    }

    if(config_.find("max_events") != config_.end())
    {
        int new_max_events = parse_int(config_[ "max_events" ], 100);
        if(new_max_events >= 10 && new_max_events <= 1000)
        {
            max_events_ = new_max_events;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid max_events: " + config_[ "max_events" ] +
                ", using default: 100");
        }
    }

    if(config_.find("connection_timeout") != config_.end())
    {
        int new_timeout = parse_int(config_[ "connection_timeout" ], 30);
        if(new_timeout >= 5 && new_timeout <= 300)
        {
            connection_timeout_ = new_timeout;
        }
        else
        {
            Logger::instance().error_log("Invalid connection_timeout: " +
                                         config_[ "connection_timeout" ] +
                                         ", using default: 30");
        }
    }

    if(config_.find("max_headers") != config_.end())
    {
        int new_max_headers = parse_int(config_[ "max_headers" ], 50);
        if(new_max_headers >= 10 && new_max_headers <= 200)
        {
            max_headers_ = new_max_headers;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid max_headers: " + config_[ "max_headers" ] +
                ", using default: 50");
        }
    }

    if(config_.find("max_connections") != config_.end())
    {
        int new_max_connections = parse_int(config_[ "max_connections" ], 1024);
        if(new_max_connections >= 100 && new_max_connections <= 65536)
        {
            max_connections_ = new_max_connections;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid max_connections: " + config_[ "max_connections" ] +
                ", using default: 1024");
        }
    }

    if(config_.find("max_pipeline") != config_.end())
    {
        int new_max_pipeline = parse_int(config_[ "max_pipeline" ], 10);
        if(new_max_pipeline >= 1 && new_max_pipeline <= 50)
        {
            max_pipeline_ = new_max_pipeline;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid max_pipeline: " + config_[ "max_pipeline" ] +
                ", using default: 10");
        }
    }

    if(config_.find("num_processes") != config_.end())
    {
        int new_num_processes = parse_int(config_[ "num_processes" ], 4);
        if(new_num_processes >= 1 && new_num_processes <= 32)
        {
            num_processes_ = new_num_processes;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid num_processes: " + config_[ "num_processes" ] +
                ", using default: 4");
        }
    }

    if(config_.find("socket_rcvbuf") != config_.end())
    {
        int new_socket_rcvbuf = parse_int(config_[ "socket_rcvbuf" ], 1048576);
        if(new_socket_rcvbuf >= 4096 && new_socket_rcvbuf <= 1048576)
        {
            socket_rcvbuf_ = new_socket_rcvbuf;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid socket_rcvbuf: " + config_[ "socket_rcvbuf" ] +
                ", using default: 1048576");
        }
    }

    if(config_.find("socket_sndbuf") != config_.end())
    {
        int new_socket_sndbuf = parse_int(config_[ "socket_sndbuf" ], 1048576);
        if(new_socket_sndbuf >= 4096 && new_socket_sndbuf <= 1048576)
        {
            socket_sndbuf_ = new_socket_sndbuf;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid socket_sndbuf: " + config_[ "socket_sndbuf" ] +
                ", using default: 1048576");
        }
    }

    if(config_.find("epoll_timeout") != config_.end())
    {
        int new_epoll_timeout = parse_int(config_[ "epoll_timeout" ], 50);
        if(new_epoll_timeout >= 10 && new_epoll_timeout <= 1000)
        {
            epoll_timeout_ = new_epoll_timeout;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid epoll_timeout: " + config_[ "epoll_timeout" ] +
                ", using default: 50");
        }
    }

    if(config_.find("cleanup_interval") != config_.end())
    {
        int new_cleanup_interval = parse_int(config_[ "cleanup_interval" ], 10);
        if(new_cleanup_interval >= 5 && new_cleanup_interval <= 60)
        {
            cleanup_interval_ = new_cleanup_interval;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid cleanup_interval: " + config_[ "cleanup_interval" ] +
                ", using default: 10");
        }
    }

    if(config_.find("fd_limit") != config_.end())
    {
        int new_fd_limit = parse_int(config_[ "fd_limit" ], 4096);
        if(new_fd_limit >= 1024 && new_fd_limit <= 65536)
        {
            fd_limit_ = new_fd_limit;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid fd_limit: " + config_[ "fd_limit" ] +
                ", using default: 4096");
        }
    }

    if(config_.find("cpu_cores") != config_.end())
    {
        int new_cpu_cores = parse_int(config_[ "cpu_cores" ], 0);
        if(new_cpu_cores > 0 && new_cpu_cores <= 128)
        {
            cpu_cores_ = new_cpu_cores;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid cpu_cores: " + config_[ "cpu_cores" ] +
                ", will auto-detect");
        }
    }

    if(config_.find("total_memory_mb") != config_.end())
    {
        int new_total_memory_mb = parse_int(config_[ "total_memory_mb" ], 0);
        if(new_total_memory_mb > 0 && new_total_memory_mb <= 1048576)
        {  // Max 1TB
            total_memory_mb_ = new_total_memory_mb;
        }
        else
        {
            Logger::instance().error_log(
                "Invalid total_memory_mb: " + config_[ "total_memory_mb" ] +
                ", will auto-detect");
        }
    }
}

bool Config::auto_detect_system()
{
    std::lock_guard<std::mutex> lock(mtx_);

    // Detect CPU cores only if not set in config
    if(cpu_cores_ == 0) { cpu_cores_ = detect_cpu_cores(); }

    // Detect total memory only if not set in config
    if(total_memory_mb_ == 0) { total_memory_mb_ = detect_total_memory(); }

    // Calculate optimal values only if not set in config
    if(num_processes_ == 4)
    {  // Default value, likely not set in config
        num_processes_ = calculate_optimal_workers();
    }

    if(max_connections_ == 1024)
    {  // Default value, likely not set in config
        calculate_optimal_limits();
    }

    // Validate configuration
    is_valid_ = validate_config();

    return is_valid_;
}

int Config::detect_cpu_cores() const
{
    // Method 1: Use std::thread::hardware_concurrency
    unsigned int cores = std::thread::hardware_concurrency();
    if(cores > 0) { return static_cast<int>(cores); }

    // Method 2: Read from /proc/cpuinfo on Linux
#ifdef __linux__
    std::ifstream cpuinfo("/proc/cpuinfo");
    if(cpuinfo.is_open())
    {
        std::string line;
        int         processor_count = 0;
        while(std::getline(cpuinfo, line))
        {
            if(line.find("processor") == 0) { processor_count++; }
        }
        if(processor_count > 0) { return processor_count; }
    }
#endif

    // Method 3: Use sysconf
    long cores_sysconf = sysconf(_SC_NPROCESSORS_ONLN);
    if(cores_sysconf > 0) { return static_cast<int>(cores_sysconf); }

    // Fallback: minimum 1 core
    return 1;
}

int Config::detect_total_memory() const
{
    // Method 1: Use sysinfo
    struct sysinfo si;
    if(sysinfo(&si) == 0)
    {
        // sysinfo.totalram is in pages
        // Each page is typically 4096 bytes
        unsigned long total_mem_kb = (si.totalram * si.mem_unit) / 1024;
        return static_cast<int>(total_mem_kb / 1024);  // Convert to MB
    }

    // Method 2: Read from /proc/meminfo on Linux
#ifdef __linux__
    std::ifstream meminfo("/proc/meminfo");
    if(meminfo.is_open())
    {
        std::string line;
        while(std::getline(meminfo, line))
        {
            if(line.find("MemTotal:") == 0)
            {
                // MemTotal: 16384 kB
                size_t pos = line.find(':');
                if(pos != std::string::npos)
                {
                    std::string value = line.substr(pos + 1);
                    // Remove "kB" and whitespace
                    value.erase(value.find("kB"), 2);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);

                    try
                    {
                        int mem_kb = std::stoi(value);
                        return mem_kb / 1024;  // Convert to MB
                    }
                    catch(...)
                    {
                        // ignore parsing errors
                    }
                }
            }
        }
    }
#endif

    // Fallback: 1024 MB
    return 1024;
}

int Config::calculate_optimal_workers() const
{
    // Strategy: Number of workers equals CPU cores
    // But maximum 8 workers to prevent overhead
    int optimal_workers = cpu_cores_;

    // If memory is low, reduce number of workers
    if(total_memory_mb_ < 2048)
    {  // Less than 2GB
        optimal_workers = std::min(optimal_workers, 2);
    }
    else if(total_memory_mb_ < 4096)
    {  // Less than 4GB
        optimal_workers = std::min(optimal_workers, 4);
    }

    // Minimum 1 worker
    return std::max(optimal_workers, 1);
}

void Config::calculate_optimal_limits()
{
    // Number of worker processes
    num_processes_ = calculate_optimal_workers();

    // Maximum concurrent connections: based on memory and CPU
    // Each connection needs approximately 8KB memory
    int max_conn_per_worker = total_memory_mb_ * 1024 / (8 * num_processes_);
    max_connections_ = std::min(max_conn_per_worker, 8192);  // Maximum 8192

    // Buffer size: based on system memory
    if(total_memory_mb_ >= 8192)
    {  // 8GB or more
        buffer_size_ = 8192;
    }
    else if(total_memory_mb_ >= 4096)
    {  // 4GB or more
        buffer_size_ = 4096;
    }
    else { buffer_size_ = 2048; }

    // Socket buffer size: based on memory
    int socket_buffer =
        std::min(total_memory_mb_ * 1024 / 4, 1048576);  // Maximum 1MB
    socket_rcvbuf_ = socket_buffer;
    socket_sndbuf_ = socket_buffer;

    // Epoll timeout: based on number of connections
    if(max_connections_ > 4096)
    {
        epoll_timeout_ = 25;  // Lower for responsiveness
    }
    else { epoll_timeout_ = 50; }

    // Connection timeout: based on usage type
    if(max_connections_ > 2048)
    {
        connection_timeout_ = 15;  // Shorter for high-load
    }
    else { connection_timeout_ = 30; }

    // File descriptor limit: based on number of connections
    fd_limit_ = max_connections_ * 2;  // Each connection may need 2 FDs
}

bool Config::validate_config() const
{
    // Validate logical values
    if(port_ < 1 || port_ > 65535) return false;
    if(backlog_ < 1 || backlog_ > 1000) return false;
    if(buffer_size_ < 1024 || buffer_size_ > 65536) return false;
    if(max_events_ < 10 || max_events_ > 1000) return false;
    if(connection_timeout_ < 5 || connection_timeout_ > 300) return false;
    if(max_headers_ < 10 || max_headers_ > 200) return false;
    if(max_connections_ < 100 || max_connections_ > 65536) return false;
    if(max_pipeline_ < 1 || max_pipeline_ > 50) return false;
    if(num_processes_ < 1 || num_processes_ > 32) return false;
    if(socket_rcvbuf_ < 4096 || socket_rcvbuf_ > 1048576) return false;
    if(socket_sndbuf_ < 4096 || socket_sndbuf_ > 1048576) return false;
    if(epoll_timeout_ < 10 || epoll_timeout_ > 1000) return false;
    if(cleanup_interval_ < 5 || cleanup_interval_ > 60) return false;
    if(fd_limit_ < 1024 || fd_limit_ > 65536) return false;

    return true;
}

// Getter methods
int Config::get_port() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return port_;
}
int Config::get_backlog() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return backlog_;
}
int Config::get_buffer_size() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return buffer_size_;
}
int Config::get_max_events() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return max_events_;
}
int Config::get_connection_timeout() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return connection_timeout_;
}
int Config::get_max_headers() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return max_headers_;
}
int Config::get_max_connections() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return max_connections_;
}
int Config::get_max_pipeline() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return max_pipeline_;
}
int Config::get_num_processes() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return num_processes_;
}
int Config::get_socket_rcvbuf() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return socket_rcvbuf_;
}
int Config::get_socket_sndbuf() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return socket_sndbuf_;
}
int Config::get_epoll_timeout() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return epoll_timeout_;
}
int Config::get_cleanup_interval() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return cleanup_interval_;
}
int Config::get_fd_limit() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return fd_limit_;
}
int Config::get_cpu_cores() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return cpu_cores_;
}
int Config::get_total_memory_mb() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return total_memory_mb_;
}
bool Config::is_valid() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return is_valid_;
}

void Config::print_config() const
{
    std::lock_guard<std::mutex> lock(mtx_);

    std::cout << "=== Aspire Server Configuration ===" << std::endl;
    std::cout << "System Capabilities:" << std::endl;
    std::cout << "  CPU Cores: " << cpu_cores_ << std::endl;
    std::cout << "  Total Memory: " << total_memory_mb_ << " MB" << std::endl;
    std::cout << std::endl;

    std::cout << "Server Settings:" << std::endl;
    std::cout << "  Port: " << port_ << std::endl;
    std::cout << "  Backlog: " << backlog_ << std::endl;
    std::cout << "  Worker Processes: " << num_processes_ << std::endl;
    std::cout << "  Max Connections: " << max_connections_ << std::endl;
    std::cout << "  Connection Timeout: " << connection_timeout_ << "s"
              << std::endl;
    std::cout << "  Buffer Size: " << buffer_size_ << " bytes" << std::endl;
    std::cout << "  Max Events: " << max_events_ << std::endl;
    std::cout << "  Max Headers: " << max_headers_ << std::endl;
    std::cout << "  Max Pipeline: " << max_pipeline_ << std::endl;
    std::cout << "  Socket RCV Buffer: " << socket_rcvbuf_ << " bytes"
              << std::endl;
    std::cout << "  Socket SND Buffer: " << socket_sndbuf_ << " bytes"
              << std::endl;
    std::cout << "  Epoll Timeout: " << epoll_timeout_ << "ms" << std::endl;
    std::cout << "  Cleanup Interval: " << cleanup_interval_ << "s"
              << std::endl;
    std::cout << "  FD Limit: " << fd_limit_ << std::endl;
    std::cout << std::endl;

    std::cout << "Configuration Valid: " << (is_valid_ ? "Yes" : "No")
              << std::endl;
    std::cout << "================================" << std::endl;
}

void Config::set(const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(mtx_);
    config_[ key ] = value;
}

std::string Config::get(const std::string& key,
                        const std::string& default_value) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto                        it = config_.find(key);
    return (it != config_.end()) ? it->second : default_value;
}

bool Config::config_file_exists(const std::string& config_path) const
{
    std::ifstream file(config_path);
    return file.good();
}