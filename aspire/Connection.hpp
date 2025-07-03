#pragma once
#include <string>
#include <queue>
#include <map>
#include <ctime>
#include "HttpParser.hpp"

class Connection {
public:
    explicit Connection(int socket_fd);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;

    int fd() const;
    std::string& buffer();
    std::string& send_buffer();
    time_t& last_activity();
    bool& keep_alive();
    std::queue<HttpParser>& request_queue();
    std::queue<std::string>& response_queue();
    bool& chunked_streaming();
    int& stream_chunk_idx();
    time_t& last_stream_time();

    void reset();
private:
    int m_fd;
    std::string m_buffer;
    std::string m_send_buffer;
    time_t m_last_activity;
    bool m_keep_alive;
    std::queue<HttpParser> m_request_queue;
    std::queue<std::string> m_response_queue;
    bool m_chunked_streaming = false;
    int m_stream_chunk_idx = 0;
    time_t m_last_stream_time = 0;
}; 