/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#include "Connection.hpp"

#include <unistd.h>

#include <stdexcept>

#include "Logger.hpp"

Connection::Connection(int socket_fd)
    : fd_(socket_fd), last_activity_(time(nullptr)), keep_alive_(false)
{
    if(!is_valid_socket(socket_fd))
    {
        throw std::runtime_error("Invalid socket file descriptor: " +
                                 std::to_string(socket_fd));
    }
}

Connection::~Connection()
{
    close_connection();
}

Connection::Connection(Connection&& other) noexcept
    : fd_(other.fd_),
      buffer_(std::move(other.buffer_)),
      send_buffer_(std::move(other.send_buffer_)),
      last_activity_(other.last_activity_),
      keep_alive_(other.keep_alive_),
      request_queue_(std::move(other.request_queue_)),
      response_queue_(std::move(other.response_queue_)),
      chunked_streaming_(other.chunked_streaming_),
      stream_chunk_idx_(other.stream_chunk_idx_),
      last_stream_time_(other.last_stream_time_)
{
    other.fd_ = -1;  // Prevent double close
}

Connection& Connection::operator=(Connection&& other) noexcept
{
    if(this != &other)
    {
        close_connection();

        fd_                = other.fd_;
        buffer_            = std::move(other.buffer_);
        send_buffer_       = std::move(other.send_buffer_);
        last_activity_     = other.last_activity_;
        keep_alive_        = other.keep_alive_;
        request_queue_     = std::move(other.request_queue_);
        response_queue_    = std::move(other.response_queue_);
        chunked_streaming_ = other.chunked_streaming_;
        stream_chunk_idx_  = other.stream_chunk_idx_;
        last_stream_time_  = other.last_stream_time_;

        other.fd_ = -1;  // Prevent double close
    }
    return *this;
}

int Connection::fd() const noexcept
{
    return fd_;
}

bool Connection::is_valid() const noexcept
{
    return is_valid_socket(fd_);
}

std::string& Connection::buffer() noexcept
{
    return buffer_;
}

std::string& Connection::send_buffer() noexcept
{
    return send_buffer_;
}

time_t& Connection::last_activity() noexcept
{
    return last_activity_;
}

bool& Connection::keep_alive() noexcept
{
    return keep_alive_;
}

std::queue<HttpParser>& Connection::request_queue() noexcept
{
    return request_queue_;
}

std::queue<std::string>& Connection::response_queue() noexcept
{
    return response_queue_;
}

bool& Connection::chunked_streaming() noexcept
{
    return chunked_streaming_;
}

int& Connection::stream_chunk_idx() noexcept
{
    return stream_chunk_idx_;
}

time_t& Connection::last_stream_time() noexcept
{
    return last_stream_time_;
}

void Connection::reset() noexcept
{
    buffer_.clear();
    send_buffer_.clear();
    last_activity_ = time(nullptr);
    keep_alive_    = false;

    // Clear queues efficiently
    std::queue<HttpParser>().swap(request_queue_);
    std::queue<std::string>().swap(response_queue_);

    chunked_streaming_ = false;
    stream_chunk_idx_  = 0;
    last_stream_time_  = 0;
}

bool Connection::close_connection() noexcept
{
    if(fd_ != -1)
    {
        const int result = close(fd_);
        fd_              = -1;
        return result == 0;
    }
    return true;  // Already closed
}

bool Connection::has_timed_out(int timeout_seconds) const noexcept
{
    if(!is_valid()) { return true; }

    const time_t now = time(nullptr);
    return (now - last_activity_) > timeout_seconds;
}

bool Connection::is_valid_socket(int socket_fd) noexcept
{
    return socket_fd >= 0;
}