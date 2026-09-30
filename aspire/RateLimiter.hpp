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
 * @brief Rate limiting configuration structure.
 */
struct RateLimitConfig
{
    int  max_requests;     // Maximum requests per window
    int  window_seconds;   // Time window in seconds
    int  burst_size;       // Burst allowance
    bool block_on_exceed;  // Whether to block or throttle

    RateLimitConfig(int requests = 100, int window = 60, int burst = 10,
                    bool block = true)
        : max_requests(requests),
          window_seconds(window),
          burst_size(burst),
          block_on_exceed(block)
    {
    }
};

/**
 * @brief Token bucket entry for rate limiting.
 */
struct TokenBucket
{
    double                                tokens;       // Current token count
    std::chrono::steady_clock::time_point last_refill;  // Last refill time
    int  consecutive_violations;  // Consecutive violations count
    bool is_blocked;              // Whether this client is blocked

    TokenBucket()
        : tokens(100.0),  // Start with full tokens for new clients
          last_refill(std::chrono::steady_clock::now()),
          consecutive_violations(0),
          is_blocked(false)
    {
    }
};

/**
 * @brief Rate limiter implementation using token bucket algorithm.
 *
 * Provides per-client rate limiting with configurable limits,
 * burst handling, and automatic blocking of abusive clients.
 * Uses RAII principles and provides comprehensive error handling.
 */
class RateLimiter
{
   public:
    /**
     * @brief Construct a new RateLimiter object.
     * @param config Rate limiting configuration.
     */
    explicit RateLimiter(const RateLimitConfig& config = RateLimitConfig());

    /**
     * @brief Destroy the RateLimiter object.
     */
    ~RateLimiter() = default;

    // Delete copy operations to prevent resource sharing
    RateLimiter(const RateLimiter&)            = delete;
    RateLimiter& operator=(const RateLimiter&) = delete;

    // Move operations for efficient resource transfer
    RateLimiter(RateLimiter&& other) noexcept;
    RateLimiter& operator=(RateLimiter&& other) noexcept;

    /**
     * @brief Check if a request from a client is allowed.
     * @param client_id Client identifier (IP address or similar).
     * @return true if request is allowed, false if rate limited.
     */
    [[nodiscard]] bool is_allowed(std::string_view client_id);

    /**
     * @brief Update rate limiting configuration.
     * @param config New configuration.
     * @return true if configuration was updated successfully.
     */
    [[nodiscard]] bool update_config(const RateLimitConfig& config);

    /**
     * @brief Get current rate limiting statistics for a client.
     * @param client_id Client identifier.
     * @return Optional string containing statistics.
     */
    [[nodiscard]] std::optional<std::string> get_client_stats(
        std::string_view client_id) const;

    /**
     * @brief Reset rate limiting for a specific client.
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
     * @return Current rate limiting configuration.
     */
    [[nodiscard]] const RateLimitConfig& get_config() const noexcept;

    /**
     * @brief Get the singleton instance of RateLimiter.
     * @return Reference to RateLimiter instance.
     */
    static RateLimiter& instance();

    mutable std::mutex                 mtx_;
    std::map<std::string, TokenBucket> clients_;
    RateLimitConfig                    config_;

    /**
     * @brief Refill tokens for a client's bucket.
     * @param bucket Token bucket to refill.
     * @param now Current time.
     */
    void refill_tokens(TokenBucket&                                 bucket,
                       const std::chrono::steady_clock::time_point& now) const;

    /**
     * @brief Check if a client should be blocked.
     * @param bucket Token bucket to check.
     * @return true if client should be blocked.
     */
    [[nodiscard]] bool should_block_client(const TokenBucket& bucket) const;

    /**
     * @brief Update violation count for a client.
     * @param bucket Token bucket to update.
     */
    void update_violation_count(TokenBucket& bucket) const;

    /**
     * @brief Check if a client entry has expired.
     * @param bucket Token bucket to check.
     * @param now Current time.
     * @return true if entry has expired.
     */
    [[nodiscard]] bool is_expired(
        const TokenBucket&                           bucket,
        const std::chrono::steady_clock::time_point& now) const;
};