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
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "Logger.hpp"

/**
 * @brief Performance metrics structure.
 */
struct PerformanceMetrics
{
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> successful_requests{0};
    std::atomic<uint64_t> failed_requests{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> active_connections{0};
    std::atomic<uint64_t> total_connections{0};
    std::atomic<uint64_t> rate_limited_requests{0};
    std::atomic<uint64_t> errors_4xx{0};
    std::atomic<uint64_t> errors_5xx{0};

    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_request_time;

    PerformanceMetrics()
        : start_time(std::chrono::steady_clock::now()),
          last_request_time(std::chrono::steady_clock::now())
    {
    }

    // Cannot copy atomic types, so delete copy operations
    PerformanceMetrics(const PerformanceMetrics&)            = delete;
    PerformanceMetrics& operator=(const PerformanceMetrics&) = delete;

    // Allow move constructor and assignment
    PerformanceMetrics(PerformanceMetrics&&)            = default;
    PerformanceMetrics& operator=(PerformanceMetrics&&) = default;
};

/**
 * @brief Snapshot of performance metrics for returning data.
 */
struct MetricsSnapshot
{
    uint64_t total_requests{0};
    uint64_t successful_requests{0};
    uint64_t failed_requests{0};
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    uint64_t active_connections{0};
    uint64_t total_connections{0};
    uint64_t rate_limited_requests{0};
    uint64_t errors_4xx{0};
    uint64_t errors_5xx{0};

    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_request_time;

    MetricsSnapshot()                                  = default;
    MetricsSnapshot(const MetricsSnapshot&)            = default;
    MetricsSnapshot& operator=(const MetricsSnapshot&) = default;
    MetricsSnapshot(MetricsSnapshot&&)                 = default;
    MetricsSnapshot& operator=(MetricsSnapshot&&)      = default;
};

/**
 * @brief Request timing information.
 */
struct RequestTiming
{
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::string                           method;
    std::string                           path;
    int                                   status_code;
    size_t                                response_size;

    RequestTiming()
        : start_time(std::chrono::steady_clock::now()),
          end_time(std::chrono::steady_clock::now()),
          method(),
          path(),
          status_code(0),
          response_size(0)
    {
    }
};

/**
 * @brief Metrics collector for monitoring server performance.
 *
 * Provides comprehensive metrics collection including request/response
 * statistics, performance counters, memory usage tracking, and connection
 * statistics. Uses RAII principles and provides thread-safe operations.
 */
class MetricsCollector
{
   public:
    /**
     * @brief Get the singleton instance of MetricsCollector.
     * @return Reference to MetricsCollector instance.
     */
    static MetricsCollector& instance();

    /**
     * @brief Record a new request.
     * @param method HTTP method.
     * @param path Request path.
     * @return Request timing object for tracking.
     */
    [[nodiscard]] RequestTiming record_request(std::string_view method,
                                               std::string_view path);

    /**
     * @brief Record request completion.
     * @param timing Request timing information.
     * @param status_code HTTP status code.
     * @param response_size Size of response in bytes.
     * @param bytes_received Size of request in bytes.
     */
    void record_request_completion(const RequestTiming& timing, int status_code,
                                   size_t response_size, size_t bytes_received);

    /**
     * @brief Record a rate limited request.
     */
    void record_rate_limited_request();

    /**
     * @brief Record connection events.
     * @param connected true if connection established, false if closed.
     */
    void record_connection_event(bool connected);

    /**
     * @brief Update active connection count.
     * @param count Current number of active connections.
     */
    void update_active_connections(size_t count);

    /**
     * @brief Get current performance metrics.
     * @return Copy of current metrics.
     */
    [[nodiscard]] MetricsSnapshot get_metrics() const;

    /**
     * @brief Get server uptime in seconds.
     * @return Uptime in seconds.
     */
    [[nodiscard]] uint64_t get_uptime_seconds() const;

    /**
     * @brief Get requests per second.
     * @return Requests per second rate.
     */
    [[nodiscard]] double get_requests_per_second() const;

    /**
     * @brief Get average response time in milliseconds.
     * @return Average response time.
     */
    [[nodiscard]] double get_average_response_time() const;

    /**
     * @brief Get metrics as JSON string.
     * @return JSON formatted metrics.
     */
    [[nodiscard]] std::string get_metrics_json() const;

    /**
     * @brief Get metrics as human-readable string.
     * @return Formatted metrics string.
     */
    [[nodiscard]] std::string get_metrics_text() const;

    /**
     * @brief Reset all metrics.
     */
    void reset_metrics();

    /**
     * @brief Get memory usage information.
     * @return Memory usage string.
     */
    [[nodiscard]] std::string get_memory_usage() const;

    /**
     * @brief Get system resource information.
     * @return System resource string.
     */
    [[nodiscard]] std::string get_system_resources() const;

   private:
    MetricsCollector()                                   = default;
    ~MetricsCollector()                                  = default;
    MetricsCollector(const MetricsCollector&)            = delete;
    MetricsCollector& operator=(const MetricsCollector&) = delete;

    mutable std::mutex              mtx_;
    PerformanceMetrics              metrics_;
    std::map<std::string, uint64_t> method_counts_;
    std::map<std::string, uint64_t> path_counts_;
    std::map<int, uint64_t>         status_code_counts_;
    std::vector<double>             response_times_;

    /**
     * @brief Calculate average response time.
     * @return Average response time in milliseconds.
     */
    [[nodiscard]] double calculate_average_response_time() const;

    /**
     * @brief Get current memory usage from system.
     * @return Memory usage in bytes.
     */
    [[nodiscard]] uint64_t get_current_memory_usage() const;

    /**
     * @brief Get CPU usage percentage.
     * @return CPU usage percentage.
     */
    [[nodiscard]] double get_cpu_usage() const;
};