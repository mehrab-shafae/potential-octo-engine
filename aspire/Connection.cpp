#include "Connection.hpp"

#include <unistd.h>

Connection::Connection(int socket_fd)
    : m_fd(socket_fd), m_last_activity(time(nullptr)), m_keep_alive(false)
{
}

Connection::~Connection()
{
    if(m_fd != -1)
    {
        close(m_fd);
        m_fd = -1;
    }
}

Connection::Connection(Connection&& other) noexcept
{
    m_fd                = other.m_fd;
    m_buffer            = std::move(other.m_buffer);
    m_send_buffer       = std::move(other.m_send_buffer);
    m_last_activity     = other.m_last_activity;
    m_keep_alive        = other.m_keep_alive;
    m_request_queue     = std::move(other.m_request_queue);
    m_response_queue    = std::move(other.m_response_queue);
    m_chunked_streaming = other.m_chunked_streaming;
    m_stream_chunk_idx  = other.m_stream_chunk_idx;
    m_last_stream_time  = other.m_last_stream_time;
    other.m_fd          = -1;
}

Connection& Connection::operator=(Connection&& other) noexcept
{
    if(this != &other)
    {
        if(m_fd != -1) close(m_fd);
        m_fd                = other.m_fd;
        m_buffer            = std::move(other.m_buffer);
        m_send_buffer       = std::move(other.m_send_buffer);
        m_last_activity     = other.m_last_activity;
        m_keep_alive        = other.m_keep_alive;
        m_request_queue     = std::move(other.m_request_queue);
        m_response_queue    = std::move(other.m_response_queue);
        m_chunked_streaming = other.m_chunked_streaming;
        m_stream_chunk_idx  = other.m_stream_chunk_idx;
        m_last_stream_time  = other.m_last_stream_time;
        other.m_fd          = -1;
    }
    return *this;
}

int Connection::fd() const
{
    return m_fd;
}
std::string& Connection::buffer()
{
    return m_buffer;
}
std::string& Connection::send_buffer()
{
    return m_send_buffer;
}
time_t& Connection::last_activity()
{
    return m_last_activity;
}
bool& Connection::keep_alive()
{
    return m_keep_alive;
}
std::queue<HttpParser>& Connection::request_queue()
{
    return m_request_queue;
}
std::queue<std::string>& Connection::response_queue()
{
    return m_response_queue;
}
bool& Connection::chunked_streaming()
{
    return m_chunked_streaming;
}
int& Connection::stream_chunk_idx()
{
    return m_stream_chunk_idx;
}
time_t& Connection::last_stream_time()
{
    return m_last_stream_time;
}

void Connection::reset()
{
    m_buffer.clear();
    m_send_buffer.clear();
    m_last_activity = time(nullptr);
    m_keep_alive    = false;
    while(!m_request_queue.empty()) m_request_queue.pop();
    while(!m_response_queue.empty()) m_response_queue.pop();
    m_chunked_streaming = false;
    m_stream_chunk_idx  = 0;
    m_last_stream_time  = 0;
}