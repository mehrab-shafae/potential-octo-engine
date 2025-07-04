/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#include "SlowDown.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "Config.hpp"

SlowDown& SlowDown::instance()
{
    static SlowDown instance(
        SlowDownConfig(Config::instance().get_slow_down_window_ms(),
                       Config::instance().get_slow_down_delay_after(),
                       Config::instance().get_slow_down_delay_ms(),
                       Config::instance().get_slow_down_max_delay_ms(),
                       Config::instance().get_slow_down_delay_multiplier(),
                       Config::instance().get_slow_down_skip_successful(),
                       Config::instance().get_slow_down_skip_failed()));
    return instance;
}

SlowDown::SlowDown(const SlowDownConfig& config) : config_(config)
{
    if(config.window_ms <= 0 || config.delay_after < 0 || config.delay_ms < 0 ||
       config.max_delay_ms < config.delay_ms || config.delay_multiplier <= 0.0)
    {
        (void)Logger::instance().error_log("Invalid slow-down configuration");
        // Use safe defaults
        config_ = SlowDownConfig(60000, 10, 100, 5000, 1.0, false, false);
    }
}

SlowDown::SlowDown(SlowDown&& other) noexcept
    : mtx_(), clients_(std::move(other.clients_)), config_(other.config_)
{
}

SlowDown& SlowDown::operator=(SlowDown&& other) noexcept
{
    if(this != &other)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        clients_ = std::move(other.clients_);
        config_  = other.config_;
    }
    return *this;
}

SlowDownResult SlowDown::check_request(std::string_view client_id,
                                       int              status_code)
{
    if(client_id.empty())
    {
        (void)Logger::instance().error_log(
            "Empty client ID provided to slow-down");
        return SlowDownResult();
    }

    std::lock_guard<std::mutex> lock(mtx_);
    auto                        now = std::chrono::steady_clock::now();

    // Get or create client entry
    auto& entry = clients_[ std::string(client_id) ];

    // Check if window has expired
    auto window_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - entry.window_start);

    if(window_elapsed.count() >= config_.window_ms)
    {
        // Reset window
        entry              = SlowDownEntry();
        entry.window_start = now;
    }

    // Check if request should be skipped
    if(should_skip_request(status_code))
    {
        return SlowDownResult(
            false, 0, std::max(0, config_.delay_after - entry.request_count),
            std::max(0, config_.window_ms -
                            static_cast<int>(window_elapsed.count())));
    }

    // Increment request count
    entry.request_count++;
    entry.last_request_time = now;

    // Check if we should apply delay
    if(entry.request_count > config_.delay_after)
    {
        int delay              = calculate_delay(entry);
        entry.current_delay_ms = delay;

        (void)Logger::instance().info_log(
            "Slow-down applied to client: " + std::string(client_id) +
            " delay: " + std::to_string(delay) + "ms");

        return SlowDownResult(
            true, delay, 0,
            std::max(0, config_.window_ms -
                            static_cast<int>(window_elapsed.count())));
    }

    return SlowDownResult(
        false, 0, std::max(0, config_.delay_after - entry.request_count),
        std::max(0,
                 config_.window_ms - static_cast<int>(window_elapsed.count())));
}

bool SlowDown::update_config(const SlowDownConfig& config)
{
    if(config.window_ms <= 0 || config.delay_after < 0 || config.delay_ms < 0 ||
       config.max_delay_ms < config.delay_ms || config.delay_multiplier <= 0.0)
    {
        (void)Logger::instance().error_log(
            "Invalid slow-down configuration update");
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    config_ = config;
    (void)Logger::instance().info_log("Slow-down configuration updated");
    return true;
}

std::optional<std::string> SlowDown::get_client_stats(
    std::string_view client_id) const
{
    if(client_id.empty()) { return std::nullopt; }

    std::lock_guard<std::mutex> lock(mtx_);
    auto                        it = clients_.find(std::string(client_id));
    if(it == clients_.end()) { return std::nullopt; }

    const auto& entry = it->second;
    auto        now   = std::chrono::steady_clock::now();

    std::stringstream ss;
    ss << "Client: " << client_id << "\n";
    ss << "Request Count: " << entry.request_count << "\n";
    ss << "Current Delay: " << entry.current_delay_ms << "ms\n";
    ss << "Delay After: " << config_.delay_after << " requests\n";

    auto window_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - entry.window_start);
    ss << "Window Elapsed: " << window_elapsed.count() << "ms\n";
    ss << "Window Remaining: "
       << std::max(0,
                   config_.window_ms - static_cast<int>(window_elapsed.count()))
       << "ms\n";

    auto time_since_last =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - entry.last_request_time);
    ss << "Time Since Last Request: " << time_since_last.count() << "ms\n";

    return ss.str();
}

bool SlowDown::reset_client(std::string_view client_id)
{
    if(client_id.empty()) { return false; }

    std::lock_guard<std::mutex> lock(mtx_);
    auto                        it = clients_.find(std::string(client_id));
    if(it == clients_.end()) { return false; }

    it->second = SlowDownEntry();
    (void)Logger::instance().info_log("Slow-down reset for client: " +
                                      std::string(client_id));
    return true;
}

size_t SlowDown::get_client_count() const noexcept
{
    std::lock_guard<std::mutex> lock(mtx_);
    return clients_.size();
}

size_t SlowDown::cleanup_expired()
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
        (void)Logger::instance().info_log("Cleaned up " +
                                          std::to_string(removed_count) +
                                          " expired slow-down clients");
    }

    return removed_count;
}

const SlowDownConfig& SlowDown::get_config() const noexcept
{
    return config_;
}

bool SlowDown::should_skip_request(int status_code) const
{
    if(config_.skip_successful_requests && status_code >= 200 &&
       status_code < 400)
    {
        return true;
    }

    if(config_.skip_failed_requests && status_code >= 400) { return true; }

    return false;
}

int SlowDown::calculate_delay(const SlowDownEntry& entry) const
{
    if(entry.request_count <= config_.delay_after) { return 0; }

    // Calculate delay based on request count and multiplier
    int    excess_requests = entry.request_count - config_.delay_after;
    double delay           = static_cast<double>(config_.delay_ms) *
                   std::pow(config_.delay_multiplier,
                            static_cast<double>(excess_requests - 1));

    // Cap at maximum delay
    delay = std::min(delay, static_cast<double>(config_.max_delay_ms));

    return static_cast<int>(delay);
}

bool SlowDown::is_expired(
    const SlowDownEntry&                         entry,
    const std::chrono::steady_clock::time_point& now) const
{
    // Consider expired if no activity for 5 times the window period
    auto expiry_duration     = std::chrono::milliseconds(config_.window_ms * 5);
    auto time_since_activity = now - entry.last_request_time;

    return time_since_activity > expiry_duration;
}

int SlowDown::get_window_remaining(
    const SlowDownEntry&                         entry,
    const std::chrono::steady_clock::time_point& now) const
{
    auto window_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - entry.window_start);

    return std::max(
        0, config_.window_ms - static_cast<int>(window_elapsed.count()));
}