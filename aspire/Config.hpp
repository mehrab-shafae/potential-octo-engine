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
#include <optional>
#include <string>
#include <string_view>

/**
 * @brief Smart configuration system for Aspire HTTP Server with modern C++20
 * features.
 *
 * Provides auto-detection of system capabilities and intelligent
 * default values based on hardware specifications. Uses RAII principles
 * and provides better error handling.
 */
class Config
{
   public:
    /**
     * @brief Configuration validation result.
     */
    enum class ValidationResult
    {
        Valid,
        InvalidPort,
        InvalidBacklog,
        InvalidBufferSize,
        InvalidMaxEvents,
        InvalidTimeout,
        InvalidMaxHeaders,
        InvalidMaxConnections,
        InvalidMaxPipeline,
        InvalidProcesses,
        InvalidSocketBuffers,
        InvalidEpollTimeout,
        InvalidCleanupInterval,
        InvalidFdLimit
    };

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
    [[nodiscard]] bool load_from_file(
        std::string_view config_path = "aspire.conf");

    /**
     * @brief Check if configuration file exists.
     * @param config_path Path to configuration file.
     * @return true if file exists, false otherwise.
     */
    [[nodiscard]] bool config_file_exists(
        std::string_view config_path = "aspire.conf") const;

    /**
     * @brief Auto-detect system capabilities and set optimal defaults.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool auto_detect_system();

    /**
     * @brief Validate current configuration.
     * @return ValidationResult indicating the validation status.
     */
    [[nodiscard]] ValidationResult validate_configuration() const;

    /**
     * @brief Get server port.
     * @return Server port number.
     */
    [[nodiscard]] int get_port() const noexcept;

    /**
     * @brief Get connection backlog.
     * @return Connection backlog value.
     */
    [[nodiscard]] int get_backlog() const noexcept;

    /**
     * @brief Get buffer size for I/O operations.
     * @return Buffer size in bytes.
     */
    [[nodiscard]] int get_buffer_size() const noexcept;

    /**
     * @brief Get maximum epoll events.
     * @return Maximum epoll events.
     */
    [[nodiscard]] int get_max_events() const noexcept;

    /**
     * @brief Get connection timeout.
     * @return Connection timeout in seconds.
     */
    [[nodiscard]] int get_connection_timeout() const noexcept;

    /**
     * @brief Get maximum headers per request.
     * @return Maximum headers count.
     */
    [[nodiscard]] int get_max_headers() const noexcept;

    /**
     * @brief Get maximum concurrent connections.
     * @return Maximum connections count.
     */
    [[nodiscard]] int get_max_connections() const noexcept;

    /**
     * @brief Get maximum pipelined requests.
     * @return Maximum pipelined requests.
     */
    [[nodiscard]] int get_max_pipeline() const noexcept;

    /**
     * @brief Get number of worker processes.
     * @return Number of worker processes.
     */
    [[nodiscard]] int get_num_processes() const noexcept;

    /**
     * @brief Get socket receive buffer size.
     * @return Socket receive buffer size in bytes.
     */
    [[nodiscard]] int get_socket_rcvbuf() const noexcept;

    /**
     * @brief Get socket send buffer size.
     * @return Socket send buffer size in bytes.
     */
    [[nodiscard]] int get_socket_sndbuf() const noexcept;

    /**
     * @brief Get epoll timeout in milliseconds.
     * @return Epoll timeout in milliseconds.
     */
    [[nodiscard]] int get_epoll_timeout() const noexcept;

    /**
     * @brief Get cleanup interval in seconds.
     * @return Cleanup interval in seconds.
     */
    [[nodiscard]] int get_cleanup_interval() const noexcept;

    /**
     * @brief Get file descriptor limit.
     * @return File descriptor limit.
     */
    [[nodiscard]] int get_fd_limit() const noexcept;

    /**
     * @brief Get rate limiting max requests per window.
     * @return Maximum requests per window.
     */
    [[nodiscard]] int get_rate_limit_max_requests() const noexcept;

    /**
     * @brief Get rate limiting window seconds.
     * @return Window time in seconds.
     */
    [[nodiscard]] int get_rate_limit_window_seconds() const noexcept;

    /**
     * @brief Get rate limiting burst size.
     * @return Burst size allowance.
     */
    [[nodiscard]] int get_rate_limit_burst_size() const noexcept;

    /**
     * @brief Get rate limiting block on exceed setting.
     * @return Whether to block on exceed.
     */
    [[nodiscard]] bool get_rate_limit_block_on_exceed() const noexcept;

    /**
     * @brief Get slow-down window milliseconds.
     * @return Window time in milliseconds.
     */
    [[nodiscard]] int get_slow_down_window_ms() const noexcept;

    /**
     * @brief Get slow-down delay after requests.
     * @return Number of requests before delay starts.
     */
    [[nodiscard]] int get_slow_down_delay_after() const noexcept;

    /**
     * @brief Get slow-down delay milliseconds.
     * @return Initial delay in milliseconds.
     */
    [[nodiscard]] int get_slow_down_delay_ms() const noexcept;

    /**
     * @brief Get slow-down max delay milliseconds.
     * @return Maximum delay in milliseconds.
     */
    [[nodiscard]] int get_slow_down_max_delay_ms() const noexcept;

    /**
     * @brief Get slow-down delay multiplier.
     * @return Delay multiplier.
     */
    [[nodiscard]] double get_slow_down_delay_multiplier() const noexcept;

    /**
     * @brief Get slow-down skip successful requests setting.
     * @return Whether to skip successful requests.
     */
    [[nodiscard]] bool get_slow_down_skip_successful() const noexcept;

    /**
     * @brief Get slow-down skip failed requests setting.
     * @return Whether to skip failed requests.
     */
    [[nodiscard]] bool get_slow_down_skip_failed() const noexcept;

    /**
     * @brief Get CPU core count.
     * @return Number of CPU cores.
     */
    [[nodiscard]] int get_cpu_cores() const noexcept;

    /**
     * @brief Get total system memory in MB.
     * @return Total memory in MB.
     */
    [[nodiscard]] int get_total_memory_mb() const noexcept;

    /**
     * @brief Check if configuration is valid.
     * @return true if valid, false otherwise.
     */
    [[nodiscard]] bool is_valid() const noexcept;

    /**
     * @brief Print current configuration.
     */
    void print_config() const;

    /**
     * @brief Set a configuration value.
     * @param key Configuration key.
     * @param value Configuration value.
     * @return true if set successfully, false otherwise.
     */
    [[nodiscard]] bool set(std::string_view key, std::string_view value);

    /**
     * @brief Get a configuration value.
     * @param key Configuration key.
     * @param default_value Default value if key not found.
     * @return Configuration value.
     */
    [[nodiscard]] std::string get(std::string_view key,
                                  std::string_view default_value = "") const;

    /**
     * @brief Get configuration value as integer.
     * @param key Configuration key.
     * @param default_value Default value if key not found or invalid.
     * @return Optional integer value.
     */
    [[nodiscard]] std::optional<int> get_int(std::string_view key,
                                             int default_value = 0) const;

   private:
    Config()                         = default;
    ~Config()                        = default;
    Config(const Config&)            = delete;
    Config& operator=(const Config&) = delete;

    /**
     * @brief Detect CPU core count.
     * @return Number of CPU cores.
     */
    [[nodiscard]] int detect_cpu_cores() const;

    /**
     * @brief Detect total system memory.
     * @return Total memory in MB.
     */
    [[nodiscard]] int detect_total_memory() const;

    /**
     * @brief Calculate optimal worker processes count.
     * @return Optimal number of worker processes.
     */
    [[nodiscard]] int calculate_optimal_workers() const;

    /**
     * @brief Calculate optimal connection limits.
     */
    void calculate_optimal_limits();

    /**
     * @brief Apply loaded configuration values to actual variables.
     */
    void apply_loaded_config();

    /**
     * @brief Parse integer value safely.
     * @param value String value to parse.
     * @param default_val Default value if parsing fails.
     * @return Parsed integer value.
     */
    [[nodiscard]] static int parse_int_safe(std::string_view value,
                                            int default_val) noexcept;

    /**
     * @brief Validate integer value within range.
     * @param value Value to validate.
     * @param min Minimum allowed value.
     * @param max Maximum allowed value.
     * @return true if valid, false otherwise.
     */
    [[nodiscard]] static bool validate_int_range(int value, int min,
                                                 int max) noexcept;

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
    int socket_rcvbuf_      = 1048576;
    int socket_sndbuf_      = 1048576;
    int epoll_timeout_      = 50;
    int cleanup_interval_   = 10;
    int fd_limit_           = 4096;

    // Rate Limiting configuration
    int  rate_limit_max_requests_    = 1000;
    int  rate_limit_window_seconds_  = 60;
    int  rate_limit_burst_size_      = 100;
    bool rate_limit_block_on_exceed_ = false;

    // Slow-Down configuration
    int    slow_down_window_ms_        = 60000;
    int    slow_down_delay_after_      = 10;
    int    slow_down_delay_ms_         = 100;
    int    slow_down_max_delay_ms_     = 5000;
    double slow_down_delay_multiplier_ = 1.0;
    bool   slow_down_skip_successful_  = false;
    bool   slow_down_skip_failed_      = false;

    bool is_valid_ = false;
};