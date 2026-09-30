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
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "Logger.hpp"

/**
 * @brief Slow-down configuration structure.
 */
struct SlowDownConfig
{
    int    window_ms;                 // Time window in milliseconds
    int    delay_after;               // Number of requests before delay starts
    int    delay_ms;                  // Initial delay in milliseconds
    int    max_delay_ms;              // Maximum delay in milliseconds
    double delay_multiplier;          // Multiplier for increasing delay
    bool   skip_successful_requests;  // Whether to skip successful requests
    bool   skip_failed_requests;      // Whether to skip failed requests

    SlowDownConfig(int window = 60000, int delay_after_param = 1,
                   int delay = 1000, int max_delay = 30000,
                   double multiplier = 1.0, bool skip_success = false,
                   bool skip_failed = false)
        : window_ms(window),
          delay_after(delay_after_param),
          delay_ms(delay),
          max_delay_ms(max_delay),
          delay_multiplier(multiplier),
          skip_successful_requests(skip_success),
          skip_failed_requests(skip_failed)
    {
    }
};

/**
 * @brief Client slow-down entry.
 */
struct SlowDownEntry
{
    std::chrono::steady_clock::time_point window_start;
    int                                   request_count;
    int                                   current_delay_ms;
    std::chrono::steady_clock::time_point last_request_time;

    SlowDownEntry()
        : window_start(std::chrono::steady_clock::now()),
          request_count(0),
          current_delay_ms(0),
          last_request_time(std::chrono::steady_clock::now())
    {
    }
};

/**
 * @brief Slow-down result structure.
 */
struct SlowDownResult
{
    bool should_delay;
    int  delay_ms;
    int  remaining_requests;
    int  window_remaining_ms;

    SlowDownResult(bool delay = false, int delay_time = 0, int remaining = 0,
                   int window = 0)
        : should_delay(delay),
          delay_ms(delay_time),
          remaining_requests(remaining),
          window_remaining_ms(window)
    {
    }
};

/**
 * @brief Express-like slow-down implementation.
 *
 * Gradually slows down responses when rate limits are exceeded,
 * similar to express-slow-down in Node.js. Uses RAII principles
 * and provides comprehensive error handling.
 */
class SlowDown
{
   public:
    /**
     * @brief Construct a new SlowDown object.
     * @param config Slow-down configuration.
     */
    explicit SlowDown(const SlowDownConfig& config = SlowDownConfig());

    /**
     * @brief Destroy the SlowDown object.
     */
    ~SlowDown() = default;

    // Delete copy operations to prevent resource sharing
    SlowDown(const SlowDown&)            = delete;
    SlowDown& operator=(const SlowDown&) = delete;

    // Move operations for efficient resource transfer
    SlowDown(SlowDown&& other) noexcept;
    SlowDown& operator=(SlowDown&& other) noexcept;

    /**
     * @brief Check if a request should be delayed.
     * @param client_id Client identifier.
     * @param status_code HTTP status code of the request.
     * @return SlowDownResult with delay information.
     */
    [[nodiscard]] SlowDownResult check_request(std::string_view client_id,
                                               int status_code = 200);

    /**
     * @brief Update slow-down configuration.
     * @param config New configuration.
     * @return true if configuration was updated successfully.
     */
    [[nodiscard]] bool update_config(const SlowDownConfig& config);

    /**
     * @brief Get current slow-down statistics for a client.
     * @param client_id Client identifier.
     * @return Optional string containing statistics.
     */
    [[nodiscard]] std::optional<std::string> get_client_stats(
        std::string_view client_id) const;

    /**
     * @brief Reset slow-down for a specific client.
     * @param client_id Client identifier.
     * @return true if client was reset successfully.
     */
    [[nodiscard]] bool reset_client(std::string_view client_id);

    /**
     * @brief Get total number of tracked clients.
     * @return Number of clients being tracked.
     */
    [[nodiscard]] size_t get_client_count() const noexcept;

    /**
     * @brief Clean up expired client entries.
     * @return Number of clients removed.
     */
    [[nodiscard]] size_t cleanup_expired();

    /**
     * @brief Get current configuration.
     * @return Current slow-down configuration.
     */
    [[nodiscard]] const SlowDownConfig& get_config() const noexcept;

    /**
     * @brief Get the singleton instance of SlowDown.
     * @return Reference to SlowDown instance.
     */
    static SlowDown& instance();

    mutable std::mutex                   mtx_;
    std::map<std::string, SlowDownEntry> clients_;
    SlowDownConfig                       config_;

    /**
     * @brief Check if a request should be skipped based on status code.
     * @param status_code HTTP status code.
     * @return true if request should be skipped.
     */
    [[nodiscard]] bool should_skip_request(int status_code) const;

    /**
     * @brief Calculate delay for a client.
     * @param entry Client slow-down entry.
     * @return Calculated delay in milliseconds.
     */
    [[nodiscard]] int calculate_delay(const SlowDownEntry& entry) const;

    /**
     * @brief Check if a client entry has expired.
     * @param entry Client slow-down entry.
     * @param now Current time.
     * @return true if entry has expired.
     */
    [[nodiscard]] bool is_expired(
        const SlowDownEntry&                         entry,
        const std::chrono::steady_clock::time_point& now) const;

    /**
     * @brief Get window remaining time in milliseconds.
     * @param entry Client slow-down entry.
     * @param now Current time.
     * @return Remaining time in milliseconds.
     */
    [[nodiscard]] int get_window_remaining(
        const SlowDownEntry&                         entry,
        const std::chrono::steady_clock::time_point& now) const;
};