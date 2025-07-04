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

bool Config::load_from_file(std::string_view config_path)
{
    std::lock_guard<std::mutex> lock(mtx_);

    std::ifstream file{std::string(config_path)};
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

bool Config::config_file_exists(std::string_view config_path) const
{
    std::ifstream file{std::string(config_path)};
    return file.good();
}

bool Config::auto_detect_system()
{
    std::lock_guard<std::mutex> lock(mtx_);

    // Detect CPU cores
    cpu_cores_ = detect_cpu_cores();
    if(cpu_cores_ <= 0)
    {
        (void)Logger::instance().error_log(
            "Failed to detect CPU cores, using default: 4");
        cpu_cores_ = 4;  // Default fallback
    }
    else
    {
        (void)Logger::instance().info_log("Detected CPU cores: " +
                                          std::to_string(cpu_cores_));
    }

    // Detect total memory
    total_memory_mb_ = detect_total_memory();
    if(total_memory_mb_ <= 0)
    {
        (void)Logger::instance().error_log(
            "Failed to detect memory, using default: 8192 MB");
        total_memory_mb_ = 8192;  // Default fallback (8GB)
    }
    else
    {
        (void)Logger::instance().info_log(
            "Detected total memory: " + std::to_string(total_memory_mb_) +
            " MB");
    }

    // Calculate optimal limits based on system capabilities
    calculate_optimal_limits();

    // Validate the configuration
    is_valid_ = (validate_configuration() == ValidationResult::Valid);

    if(!is_valid_)
    {
        (void)Logger::instance().error_log("Configuration validation failed");
        return false;
    }

    (void)Logger::instance().info_log(
        "System auto-detection completed successfully");
    return true;
}

Config::ValidationResult Config::validate_configuration() const
{
    if(!validate_int_range(port_, 1, 65535))
        return ValidationResult::InvalidPort;

    if(!validate_int_range(backlog_, 1, 1000))
        return ValidationResult::InvalidBacklog;

    if(!validate_int_range(buffer_size_, 1024, 65536))
        return ValidationResult::InvalidBufferSize;

    if(!validate_int_range(max_events_, 10, 1000))
        return ValidationResult::InvalidMaxEvents;

    if(!validate_int_range(connection_timeout_, 5, 300))
        return ValidationResult::InvalidTimeout;

    if(!validate_int_range(max_headers_, 10, 200))
        return ValidationResult::InvalidMaxHeaders;

    if(!validate_int_range(max_connections_, 100, 65536))
        return ValidationResult::InvalidMaxConnections;

    if(!validate_int_range(max_pipeline_, 1, 50))
        return ValidationResult::InvalidMaxPipeline;

    if(!validate_int_range(num_processes_, 1, 32))
        return ValidationResult::InvalidProcesses;

    if(!validate_int_range(socket_rcvbuf_, 1024, 1048576))
        return ValidationResult::InvalidSocketBuffers;

    if(!validate_int_range(socket_sndbuf_, 1024, 1048576))
        return ValidationResult::InvalidSocketBuffers;

    if(!validate_int_range(epoll_timeout_, 10, 1000))
        return ValidationResult::InvalidEpollTimeout;

    if(!validate_int_range(cleanup_interval_, 1, 60))
        return ValidationResult::InvalidCleanupInterval;

    if(!validate_int_range(fd_limit_, 1024, 65536))
        return ValidationResult::InvalidFdLimit;

    return ValidationResult::Valid;
}

void Config::apply_loaded_config()
{
    // Apply loaded values to configuration variables
    if(config_.find("port") != config_.end())
    {
        int new_port = parse_int_safe(config_[ "port" ], 8080);
        if(validate_int_range(new_port, 1, 65535)) { port_ = new_port; }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid port number: " + config_[ "port" ] +
                ", using default: 8080");
        }
    }

    if(config_.find("backlog") != config_.end())
    {
        int new_backlog = parse_int_safe(config_[ "backlog" ], 100);
        if(validate_int_range(new_backlog, 1, 1000)) { backlog_ = new_backlog; }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid backlog value: " + config_[ "backlog" ] +
                ", using default: 100");
        }
    }

    if(config_.find("buffer_size") != config_.end())
    {
        int new_buffer_size = parse_int_safe(config_[ "buffer_size" ], 4096);
        if(validate_int_range(new_buffer_size, 1024, 65536))
        {
            buffer_size_ = new_buffer_size;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid buffer_size: " + config_[ "buffer_size" ] +
                ", using default: 4096");
        }
    }

    if(config_.find("max_events") != config_.end())
    {
        int new_max_events = parse_int_safe(config_[ "max_events" ], 100);
        if(validate_int_range(new_max_events, 10, 1000))
        {
            max_events_ = new_max_events;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid max_events: " + config_[ "max_events" ] +
                ", using default: 100");
        }
    }

    if(config_.find("connection_timeout") != config_.end())
    {
        int new_timeout = parse_int_safe(config_[ "connection_timeout" ], 30);
        if(validate_int_range(new_timeout, 5, 300))
        {
            connection_timeout_ = new_timeout;
        }
        else
        {
            (void)Logger::instance().error_log("Invalid connection_timeout: " +
                                               config_[ "connection_timeout" ] +
                                               ", using default: 30");
        }
    }

    if(config_.find("max_headers") != config_.end())
    {
        int new_max_headers = parse_int_safe(config_[ "max_headers" ], 50);
        if(validate_int_range(new_max_headers, 10, 200))
        {
            max_headers_ = new_max_headers;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid max_headers: " + config_[ "max_headers" ] +
                ", using default: 50");
        }
    }

    if(config_.find("max_connections") != config_.end())
    {
        int new_max_connections =
            parse_int_safe(config_[ "max_connections" ], 1024);
        if(validate_int_range(new_max_connections, 100, 65536))
        {
            max_connections_ = new_max_connections;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid max_connections: " + config_[ "max_connections" ] +
                ", using default: 1024");
        }
    }

    if(config_.find("max_pipeline") != config_.end())
    {
        int new_max_pipeline = parse_int_safe(config_[ "max_pipeline" ], 10);
        if(validate_int_range(new_max_pipeline, 1, 50))
        {
            max_pipeline_ = new_max_pipeline;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid max_pipeline: " + config_[ "max_pipeline" ] +
                ", using default: 10");
        }
    }

    if(config_.find("num_processes") != config_.end())
    {
        int new_num_processes = parse_int_safe(config_[ "num_processes" ], 4);
        if(validate_int_range(new_num_processes, 1, 32))
        {
            num_processes_ = new_num_processes;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid num_processes: " + config_[ "num_processes" ] +
                ", using default: 4");
        }
    }

    if(config_.find("socket_rcvbuf") != config_.end())
    {
        int new_socket_rcvbuf =
            parse_int_safe(config_[ "socket_rcvbuf" ], 1048576);
        if(validate_int_range(new_socket_rcvbuf, 1024, 1048576))
        {
            socket_rcvbuf_ = new_socket_rcvbuf;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid socket_rcvbuf: " + config_[ "socket_rcvbuf" ] +
                ", using default: 1048576");
        }
    }

    if(config_.find("socket_sndbuf") != config_.end())
    {
        int new_socket_sndbuf =
            parse_int_safe(config_[ "socket_sndbuf" ], 1048576);
        if(validate_int_range(new_socket_sndbuf, 1024, 1048576))
        {
            socket_sndbuf_ = new_socket_sndbuf;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid socket_sndbuf: " + config_[ "socket_sndbuf" ] +
                ", using default: 1048576");
        }
    }

    if(config_.find("epoll_timeout") != config_.end())
    {
        int new_epoll_timeout = parse_int_safe(config_[ "epoll_timeout" ], 50);
        if(validate_int_range(new_epoll_timeout, 10, 1000))
        {
            epoll_timeout_ = new_epoll_timeout;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid epoll_timeout: " + config_[ "epoll_timeout" ] +
                ", using default: 50");
        }
    }

    if(config_.find("cleanup_interval") != config_.end())
    {
        int new_cleanup_interval =
            parse_int_safe(config_[ "cleanup_interval" ], 10);
        if(validate_int_range(new_cleanup_interval, 1, 60))
        {
            cleanup_interval_ = new_cleanup_interval;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid cleanup_interval: " + config_[ "cleanup_interval" ] +
                ", using default: 10");
        }
    }

    if(config_.find("fd_limit") != config_.end())
    {
        int new_fd_limit = parse_int_safe(config_[ "fd_limit" ], 4096);
        if(validate_int_range(new_fd_limit, 1024, 65536))
        {
            fd_limit_ = new_fd_limit;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid fd_limit: " + config_[ "fd_limit" ] +
                ", using default: 4096");
        }
    }

    // Override auto-detected values if specified in config
    if(config_.find("cpu_cores") != config_.end())
    {
        int new_cpu_cores = parse_int_safe(config_[ "cpu_cores" ], cpu_cores_);
        if(validate_int_range(new_cpu_cores, 1, 64))
        {
            cpu_cores_ = new_cpu_cores;
        }
    }

    if(config_.find("total_memory_mb") != config_.end())
    {
        int new_total_memory_mb =
            parse_int_safe(config_[ "total_memory_mb" ], total_memory_mb_);
        if(validate_int_range(new_total_memory_mb, 1024, 1048576))
        {
            total_memory_mb_ = new_total_memory_mb;
        }
    }
}

void Config::calculate_optimal_limits()
{
    // Calculate optimal number of worker processes
    num_processes_ = calculate_optimal_workers();

    // Calculate optimal connection limits based on memory
    max_connections_ =
        std::min(65536, total_memory_mb_ * 8);  // 8 connections per MB

    // Calculate optimal buffer sizes
    buffer_size_ = std::min(
        65536, std::max(4096, total_memory_mb_ * 4));  // 4KB per MB, max 64KB

    // Calculate optimal socket buffers
    socket_rcvbuf_ = std::min(
        1048576,
        std::max(65536, total_memory_mb_ * 64));  // 64KB per MB, max 1MB
    socket_sndbuf_ = socket_rcvbuf_;

    // Calculate optimal epoll timeout
    epoll_timeout_ = std::max(
        10, std::min(100, cpu_cores_ * 5));  // 5ms per core, between 10-100ms

    // Calculate optimal cleanup interval
    cleanup_interval_ = std::max(
        5, std::min(30, max_connections_ / 1000));  // Based on connection count

    // Calculate optimal file descriptor limit - ensure it's within valid range
    fd_limit_ = std::min(
        65536, std::max(4096, max_connections_ *
                                  2));  // 2 FDs per connection, max 65536

    // Ensure all values are within valid ranges
    max_connections_  = std::min(65536, std::max(100, max_connections_));
    buffer_size_      = std::min(65536, std::max(1024, buffer_size_));
    socket_rcvbuf_    = std::min(1048576, std::max(1024, socket_rcvbuf_));
    socket_sndbuf_    = std::min(1048576, std::max(1024, socket_sndbuf_));
    epoll_timeout_    = std::min(1000, std::max(10, epoll_timeout_));
    cleanup_interval_ = std::min(60, std::max(1, cleanup_interval_));
    fd_limit_         = std::min(65536, std::max(1024, fd_limit_));
    num_processes_    = std::min(32, std::max(1, num_processes_));
}

int Config::get_port() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return port_;
}

int Config::get_backlog() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return backlog_;
}

int Config::get_buffer_size() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return buffer_size_;
}

int Config::get_max_events() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return max_events_;
}

int Config::get_connection_timeout() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return connection_timeout_;
}

int Config::get_max_headers() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return max_headers_;
}

int Config::get_max_connections() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return max_connections_;
}

int Config::get_max_pipeline() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return max_pipeline_;
}

int Config::get_num_processes() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return num_processes_;
}

int Config::get_socket_rcvbuf() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return socket_rcvbuf_;
}

int Config::get_socket_sndbuf() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return socket_sndbuf_;
}

int Config::get_epoll_timeout() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return epoll_timeout_;
}

int Config::get_cleanup_interval() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return cleanup_interval_;
}

int Config::get_fd_limit() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return fd_limit_;
}

int Config::get_cpu_cores() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return cpu_cores_;
}

int Config::get_total_memory_mb() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return total_memory_mb_;
}

bool Config::is_valid() const noexcept
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

bool Config::set(std::string_view key, std::string_view value)
{
    if(key.empty()) { return false; }

    std::lock_guard<std::mutex> lock(mtx_);
    config_[ std::string(key) ] = std::string(value);
    return true;
}

std::string Config::get(std::string_view key,
                        std::string_view default_value) const
{
    std::lock_guard<std::mutex> lock(mtx_);

    const auto it = config_.find(std::string(key));
    if(it != config_.end()) { return it->second; }

    return std::string(default_value);
}

std::optional<int> Config::get_int(std::string_view key,
                                   int              default_value) const
{
    (void)default_value;  // Suppress unused parameter warning
    const std::string value = get(key, "");
    if(value.empty()) { return std::nullopt; }

    try
    {
        return std::stoi(value);
    }
    catch(...)
    {
        return std::nullopt;
    }
}

int Config::detect_cpu_cores() const
{
#ifdef __linux__
    // Try to read from /proc/cpuinfo
    std::ifstream cpuinfo{"/proc/cpuinfo"};
    if(cpuinfo.is_open())
    {
        std::string line;
        int         cores = 0;
        while(std::getline(cpuinfo, line))
        {
            if(line.find("processor") == 0) { cores++; }
        }
        if(cores > 0)
        {
            (void)Logger::instance().info_log("Detected " +
                                              std::to_string(cores) +
                                              " CPU cores from /proc/cpuinfo");
            return cores;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Failed to parse CPU cores from /proc/cpuinfo");
        }
    }
    else { (void)Logger::instance().error_log("Cannot open /proc/cpuinfo"); }
#endif

    // Fallback to sysconf
    const int cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    if(cores > 0)
    {
        (void)Logger::instance().info_log("Detected " + std::to_string(cores) +
                                          " CPU cores from sysconf");
        return cores;
    }
    else
    {
        (void)Logger::instance().error_log(
            "sysconf(_SC_NPROCESSORS_ONLN) failed");
    }

    (void)Logger::instance().error_log(
        "All CPU detection methods failed, using default: 4");
    return 4;  // Default fallback
}

int Config::detect_total_memory() const
{
#ifdef __linux__
    // Try to read from /proc/meminfo
    std::ifstream meminfo{"/proc/meminfo"};
    if(meminfo.is_open())
    {
        std::string line;
        while(std::getline(meminfo, line))
        {
            if(line.find("MemTotal:") == 0)
            {
                std::istringstream iss(line);
                std::string        label;
                long               mem_kb;
                iss >> label >> mem_kb;
                if(mem_kb > 0)
                {
                    const int mem_mb =
                        static_cast<int>(mem_kb / 1024);  // Convert KB to MB
                    (void)Logger::instance().info_log(
                        "Detected " + std::to_string(mem_mb) +
                        " MB memory from /proc/meminfo");
                    return mem_mb;
                }
                else
                {
                    (void)Logger::instance().error_log(
                        "Invalid memory value in /proc/meminfo");
                }
            }
        }
        (void)Logger::instance().error_log(
            "MemTotal not found in /proc/meminfo");
    }
    else { (void)Logger::instance().error_log("Cannot open /proc/meminfo"); }
#endif

    // Fallback to sysinfo
    struct sysinfo si;
    if(sysinfo(&si) == 0)
    {
        const auto mem_mb =
            static_cast<int>((si.totalram * si.mem_unit) / (1024 * 1024));
        if(mem_mb > 0)
        {
            (void)Logger::instance().info_log("Detected " +
                                              std::to_string(mem_mb) +
                                              " MB memory from sysinfo");
            return mem_mb;
        }
        else
        {
            (void)Logger::instance().error_log(
                "Invalid memory value from sysinfo");
        }
    }
    else { (void)Logger::instance().error_log("sysinfo() failed"); }

    (void)Logger::instance().error_log(
        "All memory detection methods failed, using default: 8192 MB");
    return 8192;  // Default fallback (8GB)
}

int Config::calculate_optimal_workers() const
{
    // Use CPU cores as base, but limit to reasonable range
    const int optimal = std::min(32, std::max(1, cpu_cores_));
    return optimal;
}

int Config::parse_int_safe(std::string_view value, int default_val) noexcept
{
    try
    {
        return std::stoi(std::string(value));
    }
    catch(...)
    {
        return default_val;
    }
}

bool Config::validate_int_range(int value, int min, int max) noexcept
{
    return (value >= min && value <= max);
}