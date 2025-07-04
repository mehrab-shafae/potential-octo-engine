/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#include "RateLimiter.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

#include "Config.hpp"

RateLimiter& RateLimiter::instance()
{
    static RateLimiter instance(
        RateLimitConfig(Config::instance().get_rate_limit_max_requests(),
                        Config::instance().get_rate_limit_window_seconds(),
                        Config::instance().get_rate_limit_burst_size(),
                        Config::instance().get_rate_limit_block_on_exceed()));
    return instance;
}

RateLimiter::RateLimiter(const RateLimitConfig& config) : config_(config)
{
    if(config.max_requests <= 0 || config.window_seconds <= 0 ||
       config.burst_size < 0)
    {
        (void)Logger::instance().error_log(
            "Invalid rate limiter configuration");
        // Use safe defaults
        config_ = RateLimitConfig(1000, 60, 100, false);
    }
}

RateLimiter::RateLimiter(RateLimiter&& other) noexcept
    : mtx_(), clients_(std::move(other.clients_)), config_(other.config_)
{
}

RateLimiter& RateLimiter::operator=(RateLimiter&& other) noexcept
{
    if(this != &other)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        clients_ = std::move(other.clients_);
        config_  = other.config_;
    }
    return *this;
}

bool RateLimiter::is_allowed(std::string_view client_id)
{
    if(client_id.empty())
    {
        (void)Logger::instance().error_log("Empty client ID provided");
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    auto                        now = std::chrono::steady_clock::now();

    // Get or create client bucket
    auto& bucket = clients_[ std::string(client_id) ];

    // Refill tokens based on time elapsed
    refill_tokens(bucket, now);

    // Check if client is blocked
    if(bucket.is_blocked)
    {
        (void)Logger::instance().info_log("Request blocked for client: " +
                                          std::string(client_id));
        return false;
    }

    // Check if we have enough tokens
    if(bucket.tokens >= 1.0)
    {
        bucket.tokens -= 1.0;
        bucket.consecutive_violations = 0;
        return true;
    }

    // Rate limit exceeded
    update_violation_count(bucket);
    (void)Logger::instance().info_log("Rate limit exceeded for client: " +
                                      std::string(client_id));
    return false;
}

bool RateLimiter::update_config(const RateLimitConfig& config)
{
    if(config.max_requests <= 0 || config.window_seconds <= 0 ||
       config.burst_size < 0)
    {
        (void)Logger::instance().error_log(
            "Invalid rate limiter configuration update");
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    config_ = config;
    (void)Logger::instance().info_log("Rate limiter configuration updated");
    return true;
}

std::optional<std::string> RateLimiter::get_client_stats(
    std::string_view client_id) const
{
    if(client_id.empty()) { return std::nullopt; }

    std::lock_guard<std::mutex> lock(mtx_);
    auto                        it = clients_.find(std::string(client_id));
    if(it == clients_.end()) { return std::nullopt; }

    const auto& bucket = it->second;
    auto        now    = std::chrono::steady_clock::now();

    std::stringstream ss;
    ss << "Client: " << client_id << "\n";
    ss << "Tokens: " << bucket.tokens << "\n";
    ss << "Consecutive Violations: " << bucket.consecutive_violations << "\n";
    ss << "Blocked: " << (bucket.is_blocked ? "Yes" : "No") << "\n";

    auto time_since_refill = std::chrono::duration_cast<std::chrono::seconds>(
        now - bucket.last_refill);
    ss << "Time Since Last Refill: " << time_since_refill.count() << "s\n";

    return ss.str();
}

bool RateLimiter::reset_client(std::string_view client_id)
{
    if(client_id.empty()) { return false; }

    std::lock_guard<std::mutex> lock(mtx_);
    auto                        it = clients_.find(std::string(client_id));
    if(it == clients_.end()) { return false; }

    it->second = TokenBucket();
    (void)Logger::instance().info_log("Rate limiting reset for client: " +
                                      std::string(client_id));
    return true;
}

size_t RateLimiter::get_client_count() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return clients_.size();
}

size_t RateLimiter::cleanup_expired()
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto                        now = std::chrono::steady_clock::now();

    size_t removed_count = 0;
    auto   it            = clients_.begin();
    while(it != clients_.end())
    {
        if(is_expired(it->second, now))
        {
            it = clients_.erase(it);
            removed_count++;
        }
        else { ++it; }
    }

    if(removed_count > 0)
    {
        (void)Logger::instance().info_log(
            "Cleaned up " + std::to_string(removed_count) + " expired clients");
    }

    return removed_count;
}

const RateLimitConfig& RateLimiter::get_config() const noexcept
{
    return config_;
}

void RateLimiter::refill_tokens(
    TokenBucket& bucket, const std::chrono::steady_clock::time_point& now) const
{
    auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(
        now - bucket.last_refill);

    if(time_diff.count() <= 0) { return; }

    // Calculate tokens to add based on time elapsed
    double tokens_to_add = static_cast<double>(time_diff.count()) *
                           static_cast<double>(config_.max_requests) /
                           static_cast<double>(config_.window_seconds);

    // For new clients or after long periods, start with full tokens
    if(bucket.tokens < 1.0 && time_diff.count() >= config_.window_seconds)
    {
        bucket.tokens = static_cast<double>(config_.max_requests);
    }
    else
    {
        bucket.tokens = std::min(
            bucket.tokens + tokens_to_add,
            static_cast<double>(config_.max_requests + config_.burst_size));
    }

    bucket.last_refill = now;
}

bool RateLimiter::should_block_client(const TokenBucket& bucket) const
{
    if(!config_.block_on_exceed) { return false; }

    // Block if too many consecutive violations
    return bucket.consecutive_violations >= 5;
}

void RateLimiter::update_violation_count(TokenBucket& bucket) const
{
    bucket.consecutive_violations++;

    if(should_block_client(bucket))
    {
        bucket.is_blocked = true;
        (void)Logger::instance().error_log(
            "Client blocked due to excessive violations");
    }
}

bool RateLimiter::is_expired(
    const TokenBucket&                           bucket,
    const std::chrono::steady_clock::time_point& now) const
{
    // Consider expired if no activity for 10 times the window period
    auto expiry_duration = std::chrono::seconds(config_.window_seconds * 10);
    auto time_since_activity = now - bucket.last_refill;

    return time_since_activity > expiry_duration;
}