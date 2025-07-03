/*
 * IMPORTANT: All development of this file and ANY file in this project
 * (including headers and sources) MUST strictly comply with the rules and
 * standards defined in doc/rules.md. No exceptions are allowed. This notice
 * MUST appear at the top of EVERY file, without exception, to remind all
 * contributors.
 *
 * Aspire Project Signature: 2025-07-03T16:39:16+03:30
 */
#include "Connection.hpp"

#include <unistd.h>

Connection::Connection(int socket_fd)
    : fd_(socket_fd), last_activity_(time(nullptr)), keep_alive_(false)
{
}

Connection::~Connection()
{
    if(fd_ != -1)
    {
        close(fd_);
        fd_ = -1;
    }
}

Connection::Connection(Connection&& other) noexcept
{
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
    other.fd_          = -1;
}

Connection& Connection::operator=(Connection&& other) noexcept
{
    if(this != &other)
    {
        if(fd_ != -1) close(fd_);
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
        other.fd_          = -1;
    }
    return *this;
}

int Connection::fd() const
{
    return fd_;
}
std::string& Connection::buffer()
{
    return buffer_;
}
std::string& Connection::send_buffer()
{
    return send_buffer_;
}
time_t& Connection::last_activity()
{
    return last_activity_;
}
bool& Connection::keep_alive()
{
    return keep_alive_;
}
std::queue<HttpParser>& Connection::request_queue()
{
    return request_queue_;
}
std::queue<std::string>& Connection::response_queue()
{
    return response_queue_;
}
bool& Connection::chunked_streaming()
{
    return chunked_streaming_;
}
int& Connection::stream_chunk_idx()
{
    return stream_chunk_idx_;
}
time_t& Connection::last_stream_time()
{
    return last_stream_time_;
}

void Connection::reset()
{
    buffer_.clear();
    send_buffer_.clear();
    last_activity_ = time(nullptr);
    keep_alive_    = false;
    while(!request_queue_.empty()) request_queue_.pop();
    while(!response_queue_.empty()) response_queue_.pop();
    chunked_streaming_ = false;
    stream_chunk_idx_  = 0;
    last_stream_time_  = 0;
}