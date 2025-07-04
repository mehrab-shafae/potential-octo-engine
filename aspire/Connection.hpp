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
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <string_view>

#include "HttpParser.hpp"

/**
 * @brief Represents a client connection and manages its state and buffers.
 *
 * Uses modern C++20 features for better error handling and RAII.
 */
class Connection
{
   public:
    /**
     * @brief Construct a new Connection object.
     * @param socket_fd The file descriptor for the socket.
     * @throws std::runtime_error if socket_fd is invalid.
     */
    explicit Connection(int socket_fd);

    /**
     * @brief Destroy the Connection object and close the socket.
     */
    ~Connection();

    // Delete copy operations to prevent resource sharing
    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;

    // Move operations for efficient resource transfer
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    /**
     * @brief Get the socket file descriptor.
     * @return The socket file descriptor or -1 if invalid.
     */
    [[nodiscard]] int fd() const noexcept;

    /**
     * @brief Check if the connection is valid.
     * @return true if connection is valid, false otherwise.
     */
    [[nodiscard]] bool is_valid() const noexcept;

    /**
     * @brief Get the input buffer for this connection.
     * @return Reference to the buffer string.
     */
    std::string& buffer() noexcept;

    /**
     * @brief Get the send buffer for this connection.
     * @return Reference to the send buffer string.
     */
    std::string& send_buffer() noexcept;

    /**
     * @brief Get the last activity time for this connection.
     * @return Reference to the last activity time.
     */
    time_t& last_activity() noexcept;

    /**
     * @brief Get the keep-alive flag for this connection.
     * @return Reference to the keep-alive flag.
     */
    bool& keep_alive() noexcept;

    /**
     * @brief Get the request queue for this connection.
     * @return Reference to the request queue.
     */
    std::queue<HttpParser>& request_queue() noexcept;

    /**
     * @brief Get the response queue for this connection.
     * @return Reference to the response queue.
     */
    std::queue<std::string>& response_queue() noexcept;

    /**
     * @brief Get the chunked streaming flag for this connection.
     * @return Reference to the chunked streaming flag.
     */
    bool& chunked_streaming() noexcept;

    /**
     * @brief Get the current chunk index for streaming.
     * @return Reference to the chunk index.
     */
    int& stream_chunk_idx() noexcept;

    /**
     * @brief Get the last stream time for chunked streaming.
     * @return Reference to the last stream time.
     */
    time_t& last_stream_time() noexcept;

    /**
     * @brief Reset the connection state and buffers.
     */
    void reset() noexcept;

    /**
     * @brief Safely close the connection.
     * @return true if closed successfully, false otherwise.
     */
    bool close_connection() noexcept;

    /**
     * @brief Check if connection has timed out.
     * @param timeout_seconds Timeout in seconds.
     * @return true if connection has timed out, false otherwise.
     */
    [[nodiscard]] bool has_timed_out(int timeout_seconds) const noexcept;

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

    /**
     * @brief Validate socket file descriptor.
     * @param socket_fd The socket file descriptor to validate.
     * @return true if valid, false otherwise.
     */
    [[nodiscard]] static bool is_valid_socket(int socket_fd) noexcept;
};