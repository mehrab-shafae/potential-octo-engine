/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#include "MetricsCollector.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>

MetricsCollector& MetricsCollector::instance()
{
    static MetricsCollector instance;
    return instance;
}

RequestTiming MetricsCollector::record_request(std::string_view method,
                                               std::string_view path)
{
    RequestTiming timing;
    timing.start_time = std::chrono::steady_clock::now();
    timing.method     = std::string(method);
    timing.path       = std::string(path);

    metrics_.total_requests.fetch_add(1, std::memory_order_relaxed);
    metrics_.last_request_time = timing.start_time;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        method_counts_[ timing.method ]++;
        path_counts_[ timing.path ]++;
    }

    return timing;
}

void MetricsCollector::record_request_completion(const RequestTiming& timing,
                                                 int    status_code,
                                                 size_t response_size,
                                                 size_t bytes_received)
{
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - timing.start_time);
    double response_time_ms = static_cast<double>(duration.count()) / 1000.0;

    metrics_.bytes_sent.fetch_add(response_size, std::memory_order_relaxed);
    metrics_.bytes_received.fetch_add(bytes_received,
                                      std::memory_order_relaxed);

    if(status_code >= 200 && status_code < 400)
    {
        metrics_.successful_requests.fetch_add(1, std::memory_order_relaxed);
    }
    else if(status_code >= 400 && status_code < 500)
    {
        metrics_.errors_4xx.fetch_add(1, std::memory_order_relaxed);
        metrics_.failed_requests.fetch_add(1, std::memory_order_relaxed);
    }
    else if(status_code >= 500)
    {
        metrics_.errors_5xx.fetch_add(1, std::memory_order_relaxed);
        metrics_.failed_requests.fetch_add(1, std::memory_order_relaxed);
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        status_code_counts_[ status_code ]++;
        response_times_.push_back(response_time_ms);

        // Keep only last 1000 response times to prevent memory growth
        if(response_times_.size() > 1000)
        {
            response_times_.erase(response_times_.begin());
        }
    }
}

void MetricsCollector::record_rate_limited_request()
{
    metrics_.rate_limited_requests.fetch_add(1, std::memory_order_relaxed);
    metrics_.failed_requests.fetch_add(1, std::memory_order_relaxed);
}

void MetricsCollector::record_connection_event(bool connected)
{
    if(connected)
    {
        metrics_.total_connections.fetch_add(1, std::memory_order_relaxed);
    }
}

void MetricsCollector::update_active_connections(size_t count)
{
    metrics_.active_connections.store(count, std::memory_order_relaxed);
}

MetricsSnapshot MetricsCollector::get_metrics() const
{
    MetricsSnapshot snapshot;

    // Load all atomic values
    snapshot.total_requests =
        metrics_.total_requests.load(std::memory_order_relaxed);
    snapshot.successful_requests =
        metrics_.successful_requests.load(std::memory_order_relaxed);
    snapshot.failed_requests =
        metrics_.failed_requests.load(std::memory_order_relaxed);
    snapshot.bytes_sent = metrics_.bytes_sent.load(std::memory_order_relaxed);
    snapshot.bytes_received =
        metrics_.bytes_received.load(std::memory_order_relaxed);
    snapshot.active_connections =
        metrics_.active_connections.load(std::memory_order_relaxed);
    snapshot.total_connections =
        metrics_.total_connections.load(std::memory_order_relaxed);
    snapshot.rate_limited_requests =
        metrics_.rate_limited_requests.load(std::memory_order_relaxed);
    snapshot.errors_4xx = metrics_.errors_4xx.load(std::memory_order_relaxed);
    snapshot.errors_5xx = metrics_.errors_5xx.load(std::memory_order_relaxed);

    // Copy time points
    snapshot.start_time        = metrics_.start_time;
    snapshot.last_request_time = metrics_.last_request_time;

    return snapshot;
}

uint64_t MetricsCollector::get_uptime_seconds() const
{
    auto now    = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - metrics_.start_time);
    return static_cast<uint64_t>(uptime.count());
}

double MetricsCollector::get_requests_per_second() const
{
    uint64_t uptime = get_uptime_seconds();
    if(uptime == 0) { return 0.0; }

    uint64_t total_requests =
        metrics_.total_requests.load(std::memory_order_relaxed);
    return static_cast<double>(total_requests) / static_cast<double>(uptime);
}

double MetricsCollector::get_average_response_time() const
{
    return calculate_average_response_time();
}

std::string MetricsCollector::get_metrics_json() const
{
    std::lock_guard<std::mutex> lock(mtx_);

    std::stringstream ss;
    ss << "{";
    ss << "\"uptime_seconds\":" << get_uptime_seconds() << ",";
    ss << "\"total_requests\":"
       << metrics_.total_requests.load(std::memory_order_relaxed) << ",";
    ss << "\"successful_requests\":"
       << metrics_.successful_requests.load(std::memory_order_relaxed) << ",";
    ss << "\"failed_requests\":"
       << metrics_.failed_requests.load(std::memory_order_relaxed) << ",";
    ss << "\"rate_limited_requests\":"
       << metrics_.rate_limited_requests.load(std::memory_order_relaxed) << ",";
    ss << "\"errors_4xx\":"
       << metrics_.errors_4xx.load(std::memory_order_relaxed) << ",";
    ss << "\"errors_5xx\":"
       << metrics_.errors_5xx.load(std::memory_order_relaxed) << ",";
    ss << "\"bytes_sent\":"
       << metrics_.bytes_sent.load(std::memory_order_relaxed) << ",";
    ss << "\"bytes_received\":"
       << metrics_.bytes_received.load(std::memory_order_relaxed) << ",";
    ss << "\"active_connections\":"
       << metrics_.active_connections.load(std::memory_order_relaxed) << ",";
    ss << "\"total_connections\":"
       << metrics_.total_connections.load(std::memory_order_relaxed) << ",";
    ss << "\"requests_per_second\":" << std::fixed << std::setprecision(2)
       << get_requests_per_second() << ",";
    ss << "\"average_response_time_ms\":" << std::fixed << std::setprecision(2)
       << get_average_response_time();
    ss << "}";

    return ss.str();
}

std::string MetricsCollector::get_metrics_text() const
{
    std::lock_guard<std::mutex> lock(mtx_);

    std::stringstream ss;
    ss << "=== Aspire Server Metrics ===" << std::endl;
    ss << "Uptime: " << get_uptime_seconds() << " seconds" << std::endl;
    ss << "Total Requests: "
       << metrics_.total_requests.load(std::memory_order_relaxed) << std::endl;
    ss << "Successful Requests: "
       << metrics_.successful_requests.load(std::memory_order_relaxed)
       << std::endl;
    ss << "Failed Requests: "
       << metrics_.failed_requests.load(std::memory_order_relaxed) << std::endl;
    ss << "Rate Limited Requests: "
       << metrics_.rate_limited_requests.load(std::memory_order_relaxed)
       << std::endl;
    ss << "4xx Errors: " << metrics_.errors_4xx.load(std::memory_order_relaxed)
       << std::endl;
    ss << "5xx Errors: " << metrics_.errors_5xx.load(std::memory_order_relaxed)
       << std::endl;
    ss << "Bytes Sent: " << metrics_.bytes_sent.load(std::memory_order_relaxed)
       << std::endl;
    ss << "Bytes Received: "
       << metrics_.bytes_received.load(std::memory_order_relaxed) << std::endl;
    ss << "Active Connections: "
       << metrics_.active_connections.load(std::memory_order_relaxed)
       << std::endl;
    ss << "Total Connections: "
       << metrics_.total_connections.load(std::memory_order_relaxed)
       << std::endl;
    ss << "Requests/Second: " << std::fixed << std::setprecision(2)
       << get_requests_per_second() << std::endl;
    ss << "Average Response Time: " << std::fixed << std::setprecision(2)
       << get_average_response_time() << " ms" << std::endl;

    return ss.str();
}

void MetricsCollector::reset_metrics()
{
    std::lock_guard<std::mutex> lock(mtx_);

    // Reset atomic counters
    metrics_.total_requests.store(0);
    metrics_.successful_requests.store(0);
    metrics_.failed_requests.store(0);
    metrics_.bytes_sent.store(0);
    metrics_.bytes_received.store(0);
    metrics_.active_connections.store(0);
    metrics_.total_connections.store(0);
    metrics_.rate_limited_requests.store(0);
    metrics_.errors_4xx.store(0);
    metrics_.errors_5xx.store(0);

    // Reset time points
    metrics_.start_time        = std::chrono::steady_clock::now();
    metrics_.last_request_time = std::chrono::steady_clock::now();

    method_counts_.clear();
    path_counts_.clear();
    status_code_counts_.clear();
    response_times_.clear();

    (void)Logger::instance().info_log("Metrics reset");
}

std::string MetricsCollector::get_memory_usage() const
{
    uint64_t memory_usage = get_current_memory_usage();

    std::stringstream ss;
    ss << "Memory Usage: " << memory_usage << " bytes (";
    ss << std::fixed << std::setprecision(2)
       << (static_cast<double>(memory_usage) / 1024.0 / 1024.0) << " MB)";

    return ss.str();
}

std::string MetricsCollector::get_system_resources() const
{
    double   cpu_usage    = get_cpu_usage();
    uint64_t memory_usage = get_current_memory_usage();

    std::stringstream ss;
    ss << "=== System Resources ===" << std::endl;
    ss << "CPU Usage: " << std::fixed << std::setprecision(2) << cpu_usage
       << "%" << std::endl;
    ss << "Memory Usage: " << memory_usage << " bytes (";
    ss << std::fixed << std::setprecision(2)
       << (static_cast<double>(memory_usage) / 1024.0 / 1024.0) << " MB)"
       << std::endl;

    return ss.str();
}

double MetricsCollector::calculate_average_response_time() const
{
    if(response_times_.empty()) { return 0.0; }

    double sum =
        std::accumulate(response_times_.begin(), response_times_.end(), 0.0);
    return sum / static_cast<double>(response_times_.size());
}

uint64_t MetricsCollector::get_current_memory_usage() const
{
    // Read memory usage from /proc/self/status
    std::ifstream status_file("/proc/self/status");
    if(!status_file.is_open()) { return 0; }

    std::string line;
    while(std::getline(status_file, line))
    {
        if(line.substr(0, 6) == "VmRSS:")
        {
            // Extract memory usage in KB and convert to bytes
            size_t pos = line.find_first_of("0123456789");
            if(pos != std::string::npos)
            {
                uint64_t kb = std::stoull(line.substr(pos));
                return kb * 1024;
            }
        }
    }

    return 0;
}

double MetricsCollector::get_cpu_usage() const
{
    // Simple CPU usage estimation based on process time
    // This is a simplified implementation
    static std::chrono::steady_clock::time_point last_check =
        std::chrono::steady_clock::now();
    static double last_cpu_usage = 0.0;

    auto now = std::chrono::steady_clock::now();
    auto time_diff =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_check);

    if(time_diff.count() > 1000)  // Update every second
    {
        // Simplified CPU usage calculation
        // In a real implementation, this would read from /proc/stat
        last_cpu_usage = 0.0;  // Placeholder
        last_check     = now;
    }

    return last_cpu_usage;
}