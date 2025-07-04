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
#include <ctime>
#include <map>
#include <queue>
#include <string>

#include "HttpParser.hpp"

/**
 * @brief Represents a client connection and manages its state and buffers.
 */
class Connection
{
   public:
    /**
     * @brief Construct a new Connection object.
     * @param socket_fd The file descriptor for the socket.
     */
    explicit Connection(int socket_fd);
    /**
     * @brief Destroy the Connection object and close the socket.
     */
    ~Connection();
    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;

    /**
     * @brief Get the socket file descriptor.
     * @return The socket file descriptor.
     */
    int fd() const;
    /**
     * @brief Get the input buffer for this connection.
     * @return Reference to the buffer string.
     */
    std::string& buffer();
    /**
     * @brief Get the send buffer for this connection.
     * @return Reference to the send buffer string.
     */
    std::string& send_buffer();
    /**
     * @brief Get the last activity time for this connection.
     * @return Reference to the last activity time.
     */
    time_t& last_activity();
    /**
     * @brief Get the keep-alive flag for this connection.
     * @return Reference to the keep-alive flag.
     */
    bool& keep_alive();
    /**
     * @brief Get the request queue for this connection.
     * @return Reference to the request queue.
     */
    std::queue<HttpParser>& request_queue();
    /**
     * @brief Get the response queue for this connection.
     * @return Reference to the response queue.
     */
    std::queue<std::string>& response_queue();
    /**
     * @brief Get the chunked streaming flag for this connection.
     * @return Reference to the chunked streaming flag.
     */
    bool& chunked_streaming();
    /**
     * @brief Get the current chunk index for streaming.
     * @return Reference to the chunk index.
     */
    int& stream_chunk_idx();
    /**
     * @brief Get the last stream time for chunked streaming.
     * @return Reference to the last stream time.
     */
    time_t& last_stream_time();

    /**
     * @brief Reset the connection state and buffers.
     */
    void reset();

   private:
    int                     fd_;
    std::string             buffer_;
    std::string             send_buffer_;
    time_t                  last_activity_;
    bool                    keep_alive_;
    std::queue<HttpParser>  request_queue_;
    std::queue<std::string> response_queue_;
    bool                    chunked_streaming_ = false;
    int                     stream_chunk_idx_  = 0;
    time_t                  last_stream_time_  = 0;
};