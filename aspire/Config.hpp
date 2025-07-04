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
#include <mutex>
#include <string>

/**
 * @brief Smart configuration system for Aspire HTTP Server.
 *
 * Provides auto-detection of system capabilities and intelligent
 * default values based on hardware specifications.
 */
class Config
{
   public:
    /**
     * @brief Get the singleton instance of Config.
     * @return Reference to Config instance.
     */
    static Config& instance();

    /**
     * @brief Load configuration from file (optional).
     * @param config_path Path to configuration file.
     * @return true if successful, false otherwise.
     */
    bool load_from_file(const std::string& config_path = "aspire.conf");

    /**
     * @brief Check if configuration file exists.
     * @param config_path Path to configuration file.
     * @return true if file exists, false otherwise.
     */
    bool config_file_exists(
        const std::string& config_path = "aspire.conf") const;

    /**
     * @brief Auto-detect system capabilities and set optimal defaults.
     * @return true if successful, false otherwise.
     */
    bool auto_detect_system();

    /**
     * @brief Get server port.
     * @return Server port number.
     */
    int get_port() const;

    /**
     * @brief Get connection backlog.
     * @return Connection backlog value.
     */
    int get_backlog() const;

    /**
     * @brief Get buffer size for I/O operations.
     * @return Buffer size in bytes.
     */
    int get_buffer_size() const;

    /**
     * @brief Get maximum epoll events.
     * @return Maximum epoll events.
     */
    int get_max_events() const;

    /**
     * @brief Get connection timeout.
     * @return Connection timeout in seconds.
     */
    int get_connection_timeout() const;

    /**
     * @brief Get maximum headers per request.
     * @return Maximum headers count.
     */
    int get_max_headers() const;

    /**
     * @brief Get maximum concurrent connections.
     * @return Maximum connections count.
     */
    int get_max_connections() const;

    /**
     * @brief Get maximum pipelined requests.
     * @return Maximum pipelined requests.
     */
    int get_max_pipeline() const;

    /**
     * @brief Get number of worker processes.
     * @return Number of worker processes.
     */
    int get_num_processes() const;

    /**
     * @brief Get socket receive buffer size.
     * @return Socket receive buffer size in bytes.
     */
    int get_socket_rcvbuf() const;

    /**
     * @brief Get socket send buffer size.
     * @return Socket send buffer size in bytes.
     */
    int get_socket_sndbuf() const;

    /**
     * @brief Get epoll timeout in milliseconds.
     * @return Epoll timeout in milliseconds.
     */
    int get_epoll_timeout() const;

    /**
     * @brief Get cleanup interval in seconds.
     * @return Cleanup interval in seconds.
     */
    int get_cleanup_interval() const;

    /**
     * @brief Get file descriptor limit.
     * @return File descriptor limit.
     */
    int get_fd_limit() const;

    /**
     * @brief Get CPU core count.
     * @return Number of CPU cores.
     */
    int get_cpu_cores() const;

    /**
     * @brief Get total system memory in MB.
     * @return Total memory in MB.
     */
    int get_total_memory_mb() const;

    /**
     * @brief Check if configuration is valid.
     * @return true if valid, false otherwise.
     */
    bool is_valid() const;

    /**
     * @brief Print current configuration.
     */
    void print_config() const;

    /**
     * @brief Set a configuration value.
     * @param key Configuration key.
     * @param value Configuration value.
     */
    void set(const std::string& key, const std::string& value);

    /**
     * @brief Get a configuration value.
     * @param key Configuration key.
     * @param default_value Default value if key not found.
     * @return Configuration value.
     */
    std::string get(const std::string& key,
                    const std::string& default_value = "") const;

   private:
    Config()                         = default;
    ~Config()                        = default;
    Config(const Config&)            = delete;
    Config& operator=(const Config&) = delete;

    /**
     * @brief Detect CPU core count.
     * @return Number of CPU cores.
     */
    int detect_cpu_cores() const;

    /**
     * @brief Detect total system memory.
     * @return Total memory in MB.
     */
    int detect_total_memory() const;

    /**
     * @brief Calculate optimal worker processes count.
     * @return Optimal number of worker processes.
     */
    int calculate_optimal_workers() const;

    /**
     * @brief Calculate optimal connection limits.
     */
    void calculate_optimal_limits();

    /**
     * @brief Validate configuration values.
     * @return true if valid, false otherwise.
     */
    bool validate_config() const;

    /**
     * @brief Apply loaded configuration values to actual variables.
     */
    void apply_loaded_config();

    mutable std::mutex                 mtx_;
    std::map<std::string, std::string> config_;

    // System capabilities
    int cpu_cores_       = 0;
    int total_memory_mb_ = 0;

    // Server configuration
    int port_               = 8080;
    int backlog_            = 100;
    int buffer_size_        = 4096;
    int max_events_         = 100;
    int connection_timeout_ = 30;
    int max_headers_        = 50;
    int max_connections_    = 1024;
    int max_pipeline_       = 10;
    int num_processes_      = 4;
    int socket_rcvbuf_      = 1048576;  // 1MB
    int socket_sndbuf_      = 1048576;  // 1MB
    int epoll_timeout_      = 50;       // 50ms
    int cleanup_interval_   = 10;       // 10 seconds
    int fd_limit_           = 4096;

    bool is_valid_ = false;
};