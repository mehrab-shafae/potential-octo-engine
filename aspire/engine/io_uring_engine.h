#pragma once

#include <liburing.h>
#include <cstddef>

namespace aspire::engine {

class IoUringEngine {
public:
    explicit IoUringEngine(unsigned int queueDepth = 256);
    ~IoUringEngine();

    // Non-copyable
    IoUringEngine(const IoUringEngine&) = delete;
    IoUringEngine& operator=(const IoUringEngine&) = delete;

    // Movable
    IoUringEngine(IoUringEngine&&) noexcept;
    IoUringEngine& operator=(IoUringEngine&&) noexcept;

    // Submits queued requests and processes completions (to be implemented)
    void run();

    // Access underlying io_uring handle (temporary helper)
    struct io_uring* handle();

private:
    struct io_uring ring_{}; // zero-init
    unsigned int queueDepth_{};
};

} // namespace aspire::engine 