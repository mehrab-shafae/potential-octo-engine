#include <liburing.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <unordered_map>
#include <stack>
#include <mutex>
#include <string>
#include <sstream>
#include <functional>
#include <cctype>

namespace aspire {

// --------------------------- util::BufferPool ---------------------------
namespace util {
class BufferPool {
public:
    static BufferPool& instance() {
        static BufferPool inst;
        return inst;
    }

    char* acquire() {
        std::lock_guard<std::mutex> lock(m_);
        if (!pool_.empty()) {
            char* buf = pool_.top();
            pool_.pop();
            return buf;
        }
        return static_cast<char*>(std::malloc(kBufSize));
    }

    void release(char* buf) {
        if (!buf) return;
        std::lock_guard<std::mutex> lock(m_);
        pool_.push(buf);
    }

    static constexpr std::size_t kBufSize = 4096;

private:
    BufferPool() = default;
    ~BufferPool() {
        while (!pool_.empty()) {
            std::free(pool_.top());
            pool_.pop();
        }
    }

    std::stack<char*> pool_;
    std::mutex m_;
};
} // namespace util

// --------------------------- http namespace -----------------------------
namespace http {

struct Request {
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    std::size_t contentLength{0};
    bool connectionClose{false};
};

class Response {
public:
    int statusCode{200};
    std::string statusMessage{"OK"};
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    std::string serialize() const {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << statusCode << ' ' << statusMessage << "\r\n";
        for (const auto& [k, v] : headers) {
            oss << k << ": " << v << "\r\n";
        }
        oss << "Content-Length: " << body.size() << "\r\n\r\n";
        oss << body;
        return oss.str();
    }
};

enum class ParseResult {
    Incomplete,
    Complete,
    Error
};

inline ParseResult parseRequest(const std::string& buffer, Request& req, std::size_t& consumed) {
    const std::string delimiter = "\r\n\r\n";
    auto pos = buffer.find(delimiter);
    if (pos == std::string::npos) {
        return ParseResult::Incomplete;
    }

    // Headers end position
    consumed = pos + delimiter.size();

    std::istringstream stream(buffer.substr(0, pos));
    std::string line;

    // Start line
    if (!std::getline(stream, line)) {
        return ParseResult::Error;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream startLine(line);
    if (!(startLine >> req.method >> req.path >> req.version)) {
        return ParseResult::Error;
    }

    // Headers
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        // Trim leading space
        if (!value.empty() && value.front() == ' ') value.erase(0, 1);
        req.headers[key] = value;
    }

    // Content-Length
    req.contentLength = 0;
    auto itCL = req.headers.find("Content-Length");
    if (itCL != req.headers.end()) {
        req.contentLength = std::stoul(itCL->second);
    }

    // Connection header
    req.connectionClose = false;
    auto itConn = req.headers.find("Connection");
    if (itConn != req.headers.end()) {
        std::string v = itConn->second;
        for (auto& ch : v) ch = std::tolower(ch);
        if (v == "close") req.connectionClose = true;
    }

    std::size_t totalNeeded = consumed + req.contentLength;
    if (buffer.size() < totalNeeded) {
        return ParseResult::Incomplete;
    }

    if (req.contentLength > 0) {
        req.body = buffer.substr(consumed, req.contentLength);
    }

    consumed = totalNeeded;
    return ParseResult::Complete;
}

using Handler = std::function<void(const Request&, Response&)>;

class Router {
public:
    static Router& instance() {
        static Router r;
        return r;
    }

    void registerHandler(const std::string& path, Handler h) {
        routes_[path] = std::move(h);
    }

    Handler find(const std::string& path) const {
        auto it = routes_.find(path);
        if (it != routes_.end()) return it->second;
        return nullptr;
    }
private:
    std::unordered_map<std::string, Handler> routes_;
};

} // namespace http

// --------------------------- engine::IoUringEngine ----------------------
namespace engine {

class IoUringEngine {
public:
    explicit IoUringEngine(unsigned int queueDepth = 1024) : queueDepth_(queueDepth) {
        if (io_uring_queue_init(queueDepth_, &ring_, 0) < 0) {
            throw std::runtime_error("io_uring_queue_init failed");
        }
    }

    ~IoUringEngine() {
        if (ring_.ring_fd >= 0) {
            io_uring_queue_exit(&ring_);
        }
    }

    IoUringEngine(const IoUringEngine&) = delete;
    IoUringEngine& operator=(const IoUringEngine&) = delete;

    IoUringEngine(IoUringEngine&& other) noexcept : ring_(other.ring_), queueDepth_(other.queueDepth_) {
        other.ring_.ring_fd = -1;
    }
    IoUringEngine& operator=(IoUringEngine&& other) noexcept {
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

    struct CompletionContext {
        void (*handler)(IoUringEngine&, struct io_uring_cqe*, void*);
        void* data;
    };

    void run() {
        running_ = true;
        while (running_) {
            struct io_uring_cqe* cqe = nullptr;
            int ret = io_uring_wait_cqe(&ring_, &cqe);
            if (ret < 0) {
                if (ret == -EINTR) continue;
                throw std::runtime_error("io_uring_wait_cqe failed");
            }
            auto* ctx = reinterpret_cast<CompletionContext*>(cqe->user_data);
            if (ctx && ctx->handler) {
                ctx->handler(*this, cqe, ctx->data);
                delete ctx;
            }
            io_uring_cqe_seen(&ring_, cqe);
        }
    }

    void stop() { running_ = false; }

    struct io_uring* handle() { return &ring_; }

private:
    struct io_uring ring_{};
    unsigned int queueDepth_{};
    bool running_{false};
};

} // namespace engine

// --------------------------- transport namespace ------------------------
namespace transport {
using aspire::util::BufferPool;
using aspire::engine::IoUringEngine;
using aspire::http::parseRequest;
using aspire::http::ParseResult;
using aspire::http::Request;
using aspire::http::Response;
using aspire::http::Router;

class Connection; // forward

class TcpAcceptor {
public:
    TcpAcceptor(IoUringEngine& eng, uint16_t port, int backlog = 128) : engine_(eng) {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) throw std::runtime_error("socket() failed");

        int opt = 1;
        if (::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            ::close(listenFd_);
            throw std::runtime_error("setsockopt() failed");
        }
#ifdef SO_REUSEPORT
        if (::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
            ::close(listenFd_);
            throw std::runtime_error("setsockopt(SO_REUSEPORT) failed");
        }
#endif
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(listenFd_);
            throw std::runtime_error("bind() failed");
        }

        setNonBlocking(listenFd_);
        if (::listen(listenFd_, backlog) < 0) {
            ::close(listenFd_);
            throw std::runtime_error("listen() failed");
        }
        std::cout << "Listening on 0.0.0.0:" << port << "\n";
        submitAccept();
    }

    ~TcpAcceptor() {
        if (listenFd_ >= 0) ::close(listenFd_);
    }

private:
    IoUringEngine& engine_;
    int listenFd_{};

    static void setNonBlocking(int fd) {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0) flags = 0;
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    void submitAccept() {
        auto* sqe = io_uring_get_sqe(engine_.handle());
        if (!sqe) throw std::runtime_error("io_uring_get_sqe returned null");
        static thread_local sockaddr_in clientAddr;
        static thread_local socklen_t clientLen = sizeof(clientAddr);

        io_uring_prep_accept(sqe, listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen, 0);
        auto* ctx = new IoUringEngine::CompletionContext{&TcpAcceptor::onAccept, this};
        sqe->user_data = reinterpret_cast<uint64_t>(ctx);
        io_uring_submit(engine_.handle());
    }

    static void onAccept(IoUringEngine& eng, struct io_uring_cqe* cqe, void* data);
};

class Connection {
public:
    Connection(IoUringEngine& eng, int fd) : engine_(eng), fd_(fd) {
        buffer_ = BufferPool::instance().acquire();
        std::cout << "New connection fd=" << fd_ << "\n";
        submitRead();
    }

    ~Connection() {
        if (!closed_) ::close(fd_);
        BufferPool::instance().release(buffer_);
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

private:
    IoUringEngine& engine_;
    int fd_{};
    bool closed_{false};
    bool keepAlive_{false};

    std::string incoming_;
    std::string outgoing_;

    static constexpr std::size_t kBufferSize = BufferPool::kBufSize;
    char* buffer_{nullptr};

    void submitRead() {
        auto* sqe = io_uring_get_sqe(engine_.handle());
        if (!sqe) {
            std::cerr << "Failed to get SQE for read" << std::endl;
            closeConnection();
            return;
        }
        io_uring_prep_recv(sqe, fd_, buffer_, kBufferSize, 0);
        auto* ctx = new IoUringEngine::CompletionContext{&Connection::onRead, this};
        sqe->user_data = reinterpret_cast<uint64_t>(ctx);
        io_uring_submit(engine_.handle());
    }

    void submitWrite(std::size_t len) {
        auto* sqe = io_uring_get_sqe(engine_.handle());
        if (!sqe) {
            std::cerr << "Failed to get SQE for write" << std::endl;
            closeConnection();
            return;
        }
        io_uring_prep_send(sqe, fd_, buffer_, static_cast<unsigned int>(len), 0);
        auto* ctx = new IoUringEngine::CompletionContext{&Connection::onWrite, this};
        sqe->user_data = reinterpret_cast<uint64_t>(ctx);
        io_uring_submit(engine_.handle());
    }

    void closeConnection() {
        if (!closed_) {
            closed_ = true;
            ::close(fd_);
            std::cout << "Closed connection fd=" << fd_ << "\n";
            delete this; // Self-destroy
        }
    }

    static void onRead(IoUringEngine& eng, struct io_uring_cqe* cqe, void* data) {
        (void)eng;
        auto* conn = static_cast<Connection*>(data);
        if (cqe->res <= 0) {
            conn->closeConnection();
            return;
        }
        std::size_t bytes = cqe->res;
        conn->incoming_.append(conn->buffer_, bytes);

        std::size_t consumed = 0;
        Request req;
        auto result = parseRequest(conn->incoming_, req, consumed);

        if (result == ParseResult::Incomplete) {
            conn->submitRead();
            return;
        }
        if (result == ParseResult::Error) {
            conn->closeConnection();
            return;
        }

        Response resp;
        auto handler = Router::instance().find(req.path);
        if (handler) {
            handler(req, resp);
        } else {
            resp.statusCode = 404;
            resp.statusMessage = "Not Found";
            resp.body = "404 Not Found\n";
            resp.headers["Content-Type"] = "text/plain";
        }

        // Connection persistence
        conn->keepAlive_ = !req.connectionClose && req.version == "HTTP/1.1";
        if (!conn->keepAlive_) {
            resp.headers["Connection"] = "close";
        } else {
            resp.headers["Connection"] = "keep-alive";
        }

        conn->outgoing_ = resp.serialize();
        std::size_t len = conn->outgoing_.size();
        if (len > kBufferSize) {
            std::memcpy(conn->buffer_, conn->outgoing_.data(), kBufferSize);
            conn->submitWrite(kBufferSize);
        } else {
            std::memcpy(conn->buffer_, conn->outgoing_.data(), len);
            conn->submitWrite(len);
        }
        conn->incoming_.erase(0, consumed);
    }

    static void onWrite(IoUringEngine& eng, struct io_uring_cqe* cqe, void* data) {
        (void)eng;
        auto* conn = static_cast<Connection*>(data);
        if (cqe->res < 0) {
            conn->closeConnection();
            return;
        }
        if (conn->keepAlive_) {
            conn->submitRead();
        } else {
            conn->closeConnection();
        }
    }
};

inline void TcpAcceptor::onAccept(IoUringEngine& eng, struct io_uring_cqe* cqe, void* data) {
    (void)eng;
    auto* self = static_cast<TcpAcceptor*>(data);
    int clientFd = cqe->res;
    if (clientFd < 0) {
        self->submitAccept();
        return;
    }
    TcpAcceptor::setNonBlocking(clientFd);
    std::cout << "Accepted connection fd=" << clientFd << "\n";
    new Connection(self->engine_, clientFd);
    self->submitAccept();
}

} // namespace transport

} // namespace aspire

// --------------------------- main ---------------------------------------
int main() {
    using namespace aspire;
    try {
        auto& router = http::Router::instance();
        router.registerHandler("/", [](const http::Request&, http::Response& resp) {
            resp.body = "Hello from aspire\n";
            resp.headers["Content-Type"] = "text/plain";
        });
        router.registerHandler("/echo", [](const http::Request& req, http::Response& resp) {
            resp.body = req.body;
            resp.headers["Content-Type"] = "text/plain";
        });

        int port = 8080;
        unsigned int threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 4;

        std::vector<std::thread> workers;
        workers.reserve(threads);

        for (unsigned int i = 0; i < threads; ++i) {
            workers.emplace_back([port]() {
                aspire::engine::IoUringEngine engine(1024);
                aspire::transport::TcpAcceptor acceptor(engine, port);
                engine.run();
            });
        }

        std::cout << "Server started on port " << port << " with " << threads << " threads.\n";

        for (auto& t : workers) t.join();
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
} 