#include "engine/io_uring_engine.h"
#include <stdexcept>

namespace aspire::engine {

IoUringEngine::IoUringEngine(unsigned int queueDepth) : queueDepth_(queueDepth) {
    if (io_uring_queue_init(queueDepth_, &ring_, 0) < 0) {
        throw std::runtime_error("io_uring_queue_init failed");
    }
}

IoUringEngine::~IoUringEngine() {
    if (ring_.ring_fd >= 0) {
        io_uring_queue_exit(&ring_);
    }
}

IoUringEngine::IoUringEngine(IoUringEngine&& other) noexcept : ring_(other.ring_), queueDepth_(other.queueDepth_) {
    other.ring_.ring_fd = -1;
}

IoUringEngine& IoUringEngine::operator=(IoUringEngine&& other) noexcept {
    if (this != &other) {
        if (ring_.ring_fd >= 0) {
            io_uring_queue_exit(&ring_);
        }
        ring_ = other.ring_;
        queueDepth_ = other.queueDepth_;
        other.ring_.ring_fd = -1;
    }
    return *this;
}

void IoUringEngine::run() {
    // TODO: implement event loop logic
}

struct io_uring* IoUringEngine::handle() {
    return &ring_;
}

} // namespace aspire::engine 