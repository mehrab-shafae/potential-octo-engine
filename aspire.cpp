// by MRB

#include <arpa/inet.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <any>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace aspire {

namespace config {

struct SocketConfig {
        bool tcpNodelay{true};
        bool tcpQuickAck{true};
        bool tcpCork{false};
        bool tcpKeepAlive{true};
        bool tcpWindowClamp{true};

        int keepAliveTime{30};
        int keepAliveInterval{5};
        int keepAliveProbes{3};

        int sendBufferSize{256 * 1024};
        int recvBufferSize{256 * 1024};

        size_t maxConnectionPoolSize{100};
        size_t maxBufferPoolSize{1000};

        int          port{8080};
        int          backlog{128};
        unsigned int queueDepth{1024};
        unsigned int threads{0};
};

class ConfigManager {
    public:
        static ConfigManager& instance() {
            static ConfigManager inst;
            return inst;
        }

        SocketConfig&       getSocketConfig() { return socketConfig_; }
        const SocketConfig& getSocketConfig() const { return socketConfig_; }

        void setSocketConfig(const SocketConfig& config) { socketConfig_ = config; }

        void setHighPerformance() {
            socketConfig_.tcpNodelay     = true;
            socketConfig_.tcpQuickAck    = true;
            socketConfig_.tcpCork        = false;
            socketConfig_.tcpKeepAlive   = true;
            socketConfig_.tcpWindowClamp = true;
            socketConfig_.sendBufferSize = 256 * 1024;
            socketConfig_.recvBufferSize = 256 * 1024;
        }

        void setLowLatency() {
            socketConfig_.tcpNodelay        = true;
            socketConfig_.tcpQuickAck       = true;
            socketConfig_.tcpCork           = false;
            socketConfig_.keepAliveTime     = 15;
            socketConfig_.keepAliveInterval = 3;
            socketConfig_.keepAliveProbes   = 2;
        }

        void setHighThroughput() {
            socketConfig_.tcpNodelay     = false;
            socketConfig_.tcpQuickAck    = false;
            socketConfig_.tcpCork        = true;
            socketConfig_.sendBufferSize = 512 * 1024;
            socketConfig_.recvBufferSize = 512 * 1024;
        }

        void setConservative() {
            socketConfig_.tcpNodelay     = false;
            socketConfig_.tcpQuickAck    = false;
            socketConfig_.tcpCork        = false;
            socketConfig_.tcpKeepAlive   = false;
            socketConfig_.tcpWindowClamp = false;
            socketConfig_.sendBufferSize = 64 * 1024;
            socketConfig_.recvBufferSize = 64 * 1024;
        }

    private:
        SocketConfig socketConfig_;
};

} // namespace config

namespace util {
class BufferPool {
    public:
        static BufferPool& instance() {
            static BufferPool inst;
            return inst;
        }

        char* acquire(std::size_t size = kBufSize) {
            std::lock_guard<std::mutex> lock(m_);

            auto& pool = getPoolForSize(size);
            if(!pool.empty()) {
                char* buf = pool.top();
                pool.pop();
                return buf;
            }

            return static_cast<char*>(std::aligned_alloc(64, size));
        }

        void release(char* buf, std::size_t size = kBufSize) {
            if(!buf) return;
            std::lock_guard<std::mutex> lock(m_);
            auto&                       pool = getPoolForSize(size);
            if(pool.size() < maxPoolSize_) {
                pool.push(buf);
            } else {
                std::free(buf);
            }
        }

        static constexpr std::size_t kBufSize      = 4096;
        static constexpr std::size_t kSmallBufSize = 1024;
        static constexpr std::size_t kLargeBufSize = 8192;
        std::size_t                  maxPoolSize_{1000}; // Will be updated in constructor

    private:
        BufferPool() { maxPoolSize_ = config::ConfigManager::instance().getSocketConfig().maxBufferPoolSize; }
        ~BufferPool() {
            for(auto& pool : pools_) {
                while(!pool.empty()) {
                    std::free(pool.top());
                    pool.pop();
                }
            }
        }

        std::stack<char*>& getPoolForSize(std::size_t size) {
            if(size <= kSmallBufSize) return pools_[ 0 ];
            if(size <= kBufSize) return pools_[ 1 ];
            return pools_[ 2 ];
        }

        std::array<std::stack<char*>, 3> pools_; // small, medium, large
        std::mutex                       m_;
};
} // namespace util

namespace util {
class Fd {
    public:
        Fd() = default;
        explicit Fd(int fd) : fd_(fd) {}
        ~Fd() { reset(); }

        Fd(const Fd&)            = delete;
        Fd& operator=(const Fd&) = delete;

        Fd(Fd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
        Fd& operator=(Fd&& other) noexcept {
            if(this != &other) {
                reset();
                fd_       = other.fd_;
                other.fd_ = -1;
            }
            return *this;
        }

        int      get() const { return fd_; }
        explicit operator int() const { return fd_; }
        bool     valid() const { return fd_ >= 0; }

        int release() {
            int tmp = fd_;
            fd_     = -1;
            return tmp;
        }

        void reset(int newFd = -1) {
            if(fd_ >= 0) { ::close(fd_); }
            fd_ = newFd;
        }

    private:
        int fd_{-1};
};

template <typename T> class Expected {
    public:
        // Success
        Expected(T&& val) : data_(std::make_unique<T>(std::move(val))) {}

        Expected(const T&) = delete;

        // Failure
        Expected(std::string err) : data_(std::move(err)) {}

        Expected(Expected&&) noexcept            = default;
        Expected& operator=(Expected&&) noexcept = default;

        bool     ok() const { return std::holds_alternative<std::unique_ptr<T>>(data_); }
        T&       value() { return *std::get<std::unique_ptr<T>>(data_); }
        const T& value() const { return *std::get<std::unique_ptr<T>>(data_); }

        // Move-out helper
        T take() { return std::move(*std::get<std::unique_ptr<T>>(data_)); }

        const std::string& error() const { return std::get<std::string>(data_); }

    private:
        std::variant<std::unique_ptr<T>, std::string> data_;
};
} // namespace util

// --------------------------- http namespace -----------------------------
namespace http {

struct Request {
        std::string                                  method;
        std::string                                  path;
        std::string                                  version;
        std::unordered_map<std::string, std::string> headers;
        std::string                                  body;

        std::size_t contentLength{0};
        bool        connectionClose{false};
};

class Response {
    public:
        int                                          statusCode{200};
        std::string                                  statusMessage{"OK"};
        std::unordered_map<std::string, std::string> headers;
        std::string                                  body;

        struct IoVecResponse {
                std::vector<struct iovec> iovecs;
                std::vector<std::string>  strings;

                void add(const std::string& str) {
                    strings.push_back(str);
                    struct iovec vec;
                    vec.iov_base = const_cast<char*>(strings.back().data());
                    vec.iov_len  = strings.back().length();
                    iovecs.push_back(vec);
                }

                void clear() {
                    iovecs.clear();
                    strings.clear();
                }
        };

        IoVecResponse toIoVec() const {
            IoVecResponse result;

            // HTTP status line
            std::string statusLine = "HTTP/1.1 " + std::to_string(statusCode) + " " + statusMessage + "\r\n";
            result.add(statusLine);

            // Headers
            for(const auto& [ k, v ] : headers) {
                std::string header = k + ": " + v + "\r\n";
                result.add(header);
            }

            // Content-Length header
            std::string contentLength = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
            result.add(contentLength);

            // Body
            if(!body.empty()) { result.add(body); }

            return result;
        }

        std::string serialize() const {
            // Pre-calculate size to avoid reallocations
            size_t totalSize = 0;
            totalSize += 16; // "HTTP/1.1 XXX "
            totalSize += statusMessage.length();
            totalSize += 2; // "\r\n"

            for(const auto& [ k, v ] : headers) {
                totalSize += k.length() + 2; // ": "
                totalSize += v.length() + 2; // "\r\n"
            }

            totalSize += 21; // "Content-Length: "
            totalSize += std::to_string(body.size()).length();
            totalSize += 4; // "\r\n\r\n"
            totalSize += body.size();

            std::string result;
            result.reserve(totalSize);

            result += "HTTP/1.1 ";
            result += std::to_string(statusCode);
            result += ' ';
            result += statusMessage;
            result += "\r\n";

            for(const auto& [ k, v ] : headers) {
                result += k;
                result += ": ";
                result += v;
                result += "\r\n";
            }

            result += "Content-Length: ";
            result += std::to_string(body.size());
            result += "\r\n\r\n";
            result += body;

            return result;
        }
};

class Context {
    public:
        Request&  req;
        Response& res;

        // Shared data between middleware
        std::unordered_map<std::string, std::string> params;
        std::unordered_map<std::string, std::any>    data;

        Context(Request& request, Response& response) : req(request), res(response) {}

        // Helper methods for common operations
        void setStatus(int code, const std::string& message = "") {
            res.statusCode = code;
            if(!message.empty()) { res.statusMessage = message; }
        }

        void setHeader(const std::string& key, const std::string& value) { res.headers[ key ] = value; }

        void setBody(const std::string& body) { res.body = body; }

        std::string getParam(const std::string& key) const {
            auto it = params.find(key);
            return it != params.end() ? it->second : "";
        }

        std::string getHeader(const std::string& key) const {
            auto it = req.headers.find(key);
            return it != req.headers.end() ? it->second : "";
        }

        template <typename T> void setData(const std::string& key, T&& value) { data[ key ] = std::forward<T>(value); }

        template <typename T> T* getData(const std::string& key) {
            auto it = data.find(key);
            if(it != data.end()) {
                try {
                    return std::any_cast<T>(&it->second);
                } catch(...) { return nullptr; }
            }
            return nullptr;
        }
};

// Middleware function type
using Middleware = std::function<void(Context&, std::function<void()>)>;

// Middleware types
enum class MiddlewareType {
    Global, // Applied to all routes
    Route,  // Applied to specific routes
    Error   // Error handling middleware
};

// Middleware entry
struct MiddlewareEntry {
        Middleware     middleware;
        MiddlewareType type;
        std::string    path; // For route-specific middleware

        MiddlewareEntry(Middleware m, MiddlewareType t, std::string p = "") : middleware(std::move(m)), type(t), path(std::move(p)) {}
};

// Next function type for middleware chain
using NextFunction = std::function<void()>;

// Middleware chain executor
class MiddlewareChain {
    public:
        static void execute(const std::vector<MiddlewareEntry>& middleware, Context& ctx, std::function<void()> finalHandler) {
            if(middleware.empty()) {
                finalHandler();
                return;
            }

            executeNext(middleware.begin(), middleware.end(), ctx, finalHandler);
        }

    private:
        static void executeNext(std::vector<MiddlewareEntry>::const_iterator current, std::vector<MiddlewareEntry>::const_iterator end, Context& ctx, std::function<void()> finalHandler) {
            if(current == end) {
                finalHandler();
                return;
            }

            auto next = [ current, end, &ctx, finalHandler ]() mutable { executeNext(++current, end, ctx, finalHandler); };

            current->middleware(ctx, next);
        }
};

enum class ParseResult { Incomplete, Complete, Error };

inline ParseResult parseRequest(const std::string& buffer, Request& req, std::size_t& consumed) {
    const std::string_view delimiter = "\r\n\r\n";
    auto                   pos       = buffer.find(delimiter);
    if(pos == std::string::npos) { return ParseResult::Incomplete; }

    // Headers end position
    consumed = pos + delimiter.size();

    std::string_view headerSection(buffer.data(), pos);

    // Parse start line
    auto lineEnd = headerSection.find('\n');
    if(lineEnd == std::string_view::npos) { return ParseResult::Error; }

    std::string_view startLine = headerSection.substr(0, lineEnd);
    if(startLine.back() == '\r') { startLine = startLine.substr(0, startLine.length() - 1); }

    // Parse method, path, version using string_view for zero-copy
    auto space1 = startLine.find(' ');
    if(space1 == std::string_view::npos) return ParseResult::Error;

    auto space2 = startLine.find(' ', space1 + 1);
    if(space2 == std::string_view::npos) return ParseResult::Error;

    req.method  = std::string(startLine.substr(0, space1));
    req.path    = std::string(startLine.substr(space1 + 1, space2 - space1 - 1));
    req.version = std::string(startLine.substr(space2 + 1));

    // Parse headers with optimized string handling
    std::string_view remaining = headerSection.substr(lineEnd + 1);
    req.headers.clear(); // Reuse existing map

    while(!remaining.empty()) {
        auto lineEnd = remaining.find('\n');
        if(lineEnd == std::string_view::npos) break;

        std::string_view line = remaining.substr(0, lineEnd);
        if(line.back() == '\r') { line = line.substr(0, line.length() - 1); }

        if(line.empty()) break;

        auto colon = line.find(':');
        if(colon != std::string_view::npos) {
            std::string_view key   = line.substr(0, colon);
            std::string_view value = line.substr(colon + 1);

            // Trim leading space efficiently
            while(!value.empty() && value.front() == ' ') { value = value.substr(1); }

            // Convert to lowercase for case-insensitive comparison
            std::string lowerKey;
            lowerKey.reserve(key.size());
            for(char c : key) { lowerKey += std::tolower(c); }

            req.headers[ std::move(lowerKey) ] = std::string(value);
        }

        remaining = remaining.substr(lineEnd + 1);
    }

    // Content-Length with optimized lookup
    req.contentLength = 0;
    auto itCL         = req.headers.find("content-length");
    if(itCL != req.headers.end()) {
        try {
            req.contentLength = std::stoul(itCL->second);
        } catch(...) { return ParseResult::Error; }
    }

    // Connection header with optimized comparison
    req.connectionClose = false;
    auto itConn         = req.headers.find("connection");
    if(itConn != req.headers.end()) {
        const std::string& v = itConn->second;
        if(v == "close") req.connectionClose = true;
    }

    std::size_t totalNeeded = consumed + req.contentLength;
    if(buffer.size() < totalNeeded) { return ParseResult::Incomplete; }

    if(req.contentLength > 0) { req.body = buffer.substr(consumed, req.contentLength); }

    consumed = totalNeeded;
    return ParseResult::Complete;
}

using Handler = std::function<void(const Request&, Response&)>;

// Radix Tree Node for efficient path matching
struct RadixNode {
        std::string                             path;
        Handler                                 handler;
        std::vector<std::unique_ptr<RadixNode>> children;

        // Path parameter support
        bool        isParam{false};
        std::string paramName;

        // Fast lookup optimization
        std::unordered_map<char, RadixNode*> charMap;

        RadixNode() = default;
        explicit RadixNode(std::string p) : path(std::move(p)) {}

        // Pre-compute character map for faster lookups
        void buildCharMap() {
            charMap.clear();
            for(auto& child : children) {
                if(!child->path.empty()) { charMap[ child->path[ 0 ] ] = child.get(); }
            }
        }
};

class Router {
    public:
        static Router& instance() {
            static Router r;
            return r;
        }

        // Register middleware
        void use(Middleware middleware) { middleware_.emplace_back(std::move(middleware), MiddlewareType::Global); }

        void use(const std::string& path, Middleware middleware) { middleware_.emplace_back(std::move(middleware), MiddlewareType::Route, path); }

        void useError(Middleware middleware) { errorMiddleware_.emplace_back(std::move(middleware)); }

        void registerHandler(const std::string& path, Handler h) {
            insertRoute(&root_, path, 0, std::move(h));
            rebuildCharMaps(&root_);
        }

        // Register handler with multiple middleware (Express.js style)
        void registerHandler(const std::string& path, const std::vector<Middleware>& middleware, Handler h) {
            // Create a composite handler that executes middleware chain + final
            // handler
            auto compositeHandler = [ this, middleware, h = std::move(h) ](const Request& req, Response& res) {
                Context ctx(const_cast<Request&>(req), res);

                // Find route parameters
                auto routeMatch = this->findWithParams(req.path);
                ctx.params      = routeMatch.params;

                auto finalHandler = [ &ctx, &h ]() { h(ctx.req, ctx.res); };

                // Convert middleware vector to MiddlewareEntry vector
                std::vector<MiddlewareEntry> middlewareEntries;
                for(const auto& m : middleware) { middlewareEntries.emplace_back(m, MiddlewareType::Global); }

                // Execute middleware chain
                MiddlewareChain::execute(middlewareEntries, ctx, finalHandler);
            };

            insertRoute(&root_, path, 0, std::move(compositeHandler));
            rebuildCharMaps(&root_);
        }

        // Register handler with single middleware
        void registerHandler(const std::string& path, Middleware middleware, Handler h) { registerHandler(path, std::vector<Middleware>{std::move(middleware)}, std::move(h)); }

        Handler find(const std::string& path) const { return findRoute(&root_, path, 0); }

        // Enhanced find with path parameters
        struct RouteMatch {
                Handler                                      handler;
                std::unordered_map<std::string, std::string> params;
        };

        RouteMatch findWithParams(const std::string& path) const {
            RouteMatch match;
            match.handler = findRouteWithParams(&root_, path, 0, match.params);
            return match;
        }

        // Execute middleware chain with route handler
        void executeRequest(Request& req, Response& res) {
            Context ctx(req, res);

            // Collect applicable middleware
            std::vector<MiddlewareEntry> applicableMiddleware;

            // Add global middleware
            for(const auto& entry : middleware_) {
                if(entry.type == MiddlewareType::Global) { applicableMiddleware.push_back(entry); }
            }

            // Add route-specific middleware
            for(const auto& entry : middleware_) {
                if(entry.type == MiddlewareType::Route && (entry.path.empty() || req.path.find(entry.path) == 0)) { applicableMiddleware.push_back(entry); }
            }

            // Find route handler
            auto routeMatch = findWithParams(req.path);
            ctx.params      = routeMatch.params;

            auto finalHandler = [ &ctx, &routeMatch ]() {
                if(routeMatch.handler) {
                    routeMatch.handler(ctx.req, ctx.res);
                } else {
                    ctx.setStatus(404, "Not Found");
                    ctx.setBody("404 Not Found\n");
                    ctx.setHeader("Content-Type", "text/plain");
                }
            };

            // Execute middleware chain
            MiddlewareChain::execute(applicableMiddleware, ctx, finalHandler);
        }

    private:
        RadixNode                    root_;
        std::vector<MiddlewareEntry> middleware_;
        std::vector<Middleware>      errorMiddleware_;

        void rebuildCharMaps(RadixNode* node) {
            node->buildCharMap();
            for(auto& child : node->children) { rebuildCharMaps(child.get()); }
        }

        void insertRoute(RadixNode* node, const std::string& path, size_t pos, Handler h) {
            if(pos >= path.length()) {
                node->handler = std::move(h);
                return;
            }

            // Check for path parameters (e.g., /users/:id)
            if(path[ pos ] == ':') {
                auto slashPos = path.find('/', pos);
                if(slashPos == std::string::npos) slashPos = path.length();

                std::string paramName     = path.substr(pos + 1, slashPos - pos - 1);
                std::string remainingPath = path.substr(slashPos);

                // Create parameter node
                auto paramNode       = std::make_unique<RadixNode>("");
                paramNode->isParam   = true;
                paramNode->paramName = paramName;

                if(slashPos < path.length()) {
                    insertRoute(paramNode.get(), path, slashPos, std::move(h));
                } else {
                    paramNode->handler = std::move(h);
                }

                node->children.push_back(std::move(paramNode));
                return;
            }

            // Find matching child using character map for O(1) lookup
            char firstChar = pos < path.length() ? path[ pos ] : '\0';
            auto it        = node->charMap.find(firstChar);
            if(it != node->charMap.end()) {
                RadixNode* child  = it->second;
                size_t     common = 0;
                while(common < child->path.length() && pos + common < path.length() && child->path[ common ] == path[ pos + common ]) { common++; }

                if(common > 0) {
                    if(common == child->path.length()) {
                        // Full match, continue with child
                        insertRoute(child, path, pos + common, std::move(h));
                        return;
                    } else {
                        // Partial match, split node
                        auto newChild       = std::make_unique<RadixNode>(child->path.substr(common));
                        newChild->handler   = std::move(child->handler);
                        newChild->children  = std::move(child->children);
                        newChild->isParam   = child->isParam;
                        newChild->paramName = child->paramName;

                        child->path = child->path.substr(0, common);
                        child->children.clear();
                        child->children.push_back(std::move(newChild));

                        insertRoute(child, path, pos + common, std::move(h));
                        return;
                    }
                }
            }

            // No match found, create new child
            auto newChild     = std::make_unique<RadixNode>(path.substr(pos));
            newChild->handler = std::move(h);
            node->children.push_back(std::move(newChild));
        }

        Handler findRoute(const RadixNode* node, const std::string& path, size_t pos) const {
            if(pos >= path.length()) { return node->handler; }

            // Use character map for faster lookup
            char firstChar = path[ pos ];
            auto it        = node->charMap.find(firstChar);
            if(it != node->charMap.end()) {
                const RadixNode* child = it->second;
                if(pos + child->path.length() <= path.length() && path.substr(pos, child->path.length()) == child->path) { return findRoute(child, path, pos + child->path.length()); }
            }

            return nullptr;
        }

        Handler findRouteWithParams(const RadixNode* node, const std::string& path, size_t pos, std::unordered_map<std::string, std::string>& params) const {
            if(pos >= path.length()) { return node->handler; }

            // Try exact matches first
            char firstChar = path[ pos ];
            auto it        = node->charMap.find(firstChar);
            if(it != node->charMap.end()) {
                const RadixNode* child = it->second;
                if(pos + child->path.length() <= path.length() && path.substr(pos, child->path.length()) == child->path) {
                    return findRouteWithParams(child, path, pos + child->path.length(), params);
                }
            }

            // Try parameter nodes
            for(const auto& child : node->children) {
                if(child->isParam) {
                    auto slashPos = path.find('/', pos);
                    if(slashPos == std::string::npos) slashPos = path.length();

                    std::string paramValue     = path.substr(pos, slashPos - pos);
                    params[ child->paramName ] = paramValue;

                    if(slashPos < path.length()) {
                        return findRouteWithParams(child.get(), path, slashPos, params);
                    } else {
                        return child->handler;
                    }
                }
            }

            return nullptr;
        }
};

} // namespace http

// --------------------------- engine::IoUringEngine ----------------------
namespace engine {

class IoUringEngine {
    public:
        // Prefer using create() which returns util::Expected instead of throwing.
        explicit IoUringEngine(unsigned int queueDepth = 1024);

        static util::Expected<IoUringEngine> create(unsigned int queueDepth = 1024);

        ~IoUringEngine() {
            if(ring_.ring_fd >= 0) { io_uring_queue_exit(&ring_); }
        }

        IoUringEngine(const IoUringEngine&)            = delete;
        IoUringEngine& operator=(const IoUringEngine&) = delete;

        IoUringEngine(IoUringEngine&& other) noexcept : ring_(other.ring_), queueDepth_(other.queueDepth_) { other.ring_.ring_fd = -1; }
        IoUringEngine& operator=(IoUringEngine&& other) noexcept {
            if(this != &other) {
                if(ring_.ring_fd >= 0) { io_uring_queue_exit(&ring_); }
                ring_               = other.ring_;
                queueDepth_         = other.queueDepth_;
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
            while(running_) {
                struct io_uring_cqe* cqe = nullptr;
                int                  ret = io_uring_wait_cqe(&ring_, &cqe);
                if(ret < 0) {
                    if(ret == -EINTR) continue;
                    throw std::runtime_error("io_uring_wait_cqe failed");
                }
                auto* ctx = reinterpret_cast<CompletionContext*>(cqe->user_data);
                if(ctx && ctx->handler) {
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
        unsigned int    queueDepth_{};
        bool            running_{false};
};

// ---------------- IoUringEngine definitions ----------------
inline engine::IoUringEngine::IoUringEngine(unsigned int queueDepth) : queueDepth_(queueDepth) {
    if(io_uring_queue_init(queueDepth_, &ring_, 0) < 0) {
        // Mark as invalid; caller should check via create().
        ring_.ring_fd = -1;
    }
}

inline util::Expected<engine::IoUringEngine> engine::IoUringEngine::create(unsigned int queueDepth) {
    IoUringEngine eng(queueDepth);
    if(eng.ring_.ring_fd < 0) { return std::string{"io_uring_queue_init failed"}; }
    return eng;
}

} // namespace engine

// --------------------------- transport namespace ------------------------
namespace transport {
using aspire::engine::IoUringEngine;
using aspire::http::parseRequest;
using aspire::http::ParseResult;
using aspire::http::Request;
using aspire::http::Response;
using aspire::http::Router;
using aspire::util::BufferPool;

class Connection; // forward

class TcpAcceptor {
    public:
        static util::Expected<TcpAcceptor> create(IoUringEngine& eng, uint16_t port, int backlog = 128);

        TcpAcceptor(IoUringEngine& eng, uint16_t port, int backlog = 128) : engine_(eng) {
            util::Fd sock(::socket(AF_INET, SOCK_STREAM, 0));
            if(!sock.valid()) throw std::runtime_error("socket() failed");
            listenFd_ = std::move(sock);

            int opt = 1;
            if(::setsockopt(static_cast<int>(listenFd_), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) { throw std::runtime_error("setsockopt() failed"); }
#ifdef SO_REUSEPORT
            if(::setsockopt(static_cast<int>(listenFd_), SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) { throw std::runtime_error("setsockopt(SO_REUSEPORT) failed"); }
#endif

            // Set socket buffer sizes from config
            const auto& config = config::ConfigManager::instance().getSocketConfig();
            if(::setsockopt(static_cast<int>(listenFd_), SOL_SOCKET, SO_SNDBUF, &config.sendBufferSize, sizeof(config.sendBufferSize)) < 0) {
                std::cerr << "Warning: setsockopt(SO_SNDBUF) failed" << std::endl;
            }
            if(::setsockopt(static_cast<int>(listenFd_), SOL_SOCKET, SO_RCVBUF, &config.recvBufferSize, sizeof(config.recvBufferSize)) < 0) {
                std::cerr << "Warning: setsockopt(SO_RCVBUF) failed" << std::endl;
            }

            sockaddr_in addr{};
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port        = htons(port);
            if(::bind(static_cast<int>(listenFd_), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { throw std::runtime_error("bind() failed"); }

            setNonBlocking(static_cast<int>(listenFd_));
            if(::listen(static_cast<int>(listenFd_), backlog) < 0) { throw std::runtime_error("listen() failed"); }
            std::cout << "Listening on 0.0.0.0:" << port << "\n";
            submitAccept();
        }

        ~TcpAcceptor() = default;

        TcpAcceptor(const TcpAcceptor&)                = delete;
        TcpAcceptor& operator=(const TcpAcceptor&)     = delete;
        TcpAcceptor(TcpAcceptor&&) noexcept            = default;
        TcpAcceptor& operator=(TcpAcceptor&&) noexcept = default;

    private:
        IoUringEngine& engine_;
        util::Fd       listenFd_{};

        static void setNonBlocking(int fd) {
            int flags = ::fcntl(fd, F_GETFL, 0);
            if(flags < 0) flags = 0;
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }

        void submitAccept() {
            auto* sqe = io_uring_get_sqe(engine_.handle());
            if(!sqe) throw std::runtime_error("io_uring_get_sqe returned null");
            static thread_local sockaddr_in clientAddr;
            static thread_local socklen_t   clientLen = sizeof(clientAddr);

            io_uring_prep_accept(sqe, static_cast<int>(listenFd_), reinterpret_cast<sockaddr*>(&clientAddr), &clientLen, 0);
            auto* ctx      = new IoUringEngine::CompletionContext{&TcpAcceptor::onAccept, this};
            sqe->user_data = reinterpret_cast<uint64_t>(ctx);
            io_uring_submit(engine_.handle());
        }

        static void onAccept(IoUringEngine& eng, struct io_uring_cqe* cqe, void* data);
};

inline util::Expected<transport::TcpAcceptor> transport::TcpAcceptor::create(IoUringEngine& eng, uint16_t port, int backlog) {
    try {
        return TcpAcceptor(eng, port, backlog);
    } catch(const std::exception& ex) { return std::string(ex.what()); }
}

// Connection Pool for reusing connection objects
class ConnectionPool {
    public:
        static ConnectionPool& instance() {
            static ConnectionPool inst;
            return inst;
        }

        Connection* acquire(IoUringEngine& eng, int fd);
        void        release(Connection* conn);

    private:
        ConnectionPool() { maxPoolSize_ = config::ConfigManager::instance().getSocketConfig().maxConnectionPoolSize; }

        size_t                  maxPoolSize_{100};
        std::stack<Connection*> pool_;
        std::mutex              m_;
};

class Connection {
    public:
        Connection(IoUringEngine& eng, int fd) : engine_(&eng), fd_(fd) {
            buffer_ = BufferPool::instance().acquire();
            std::cout << "New connection fd=" << fd_.get() << "\n";
            submitRead();
        }

        ~Connection() {
            // fd_ closes automatically via RAII
            BufferPool::instance().release(buffer_);
        }

        void reset(IoUringEngine& eng, int fd) {
            engine_    = &eng;
            fd_        = util::Fd(fd);
            closed_    = false;
            keepAlive_ = false;
            incoming_.clear();
            outgoing_.clear();
            buffer_ = BufferPool::instance().acquire();
            std::cout << "Reused connection fd=" << fd_.get() << "\n";
            submitRead();
        }

        Connection(const Connection&)            = delete;
        Connection& operator=(const Connection&) = delete;

    private:
        IoUringEngine* engine_;
        util::Fd       fd_{};
        bool           closed_{false};
        bool           keepAlive_{false};

        std::string incoming_;
        std::string outgoing_;

        static constexpr std::size_t kBufferSize = BufferPool::kBufSize;
        char*                        buffer_{nullptr};

        // Zero-copy response optimization
        Response::IoVecResponse currentResponse_;
        size_t                  responseOffset_{0};
        bool                    sendingResponse_{false};

        void submitRead() {
            auto* sqe = io_uring_get_sqe(engine_->handle());
            if(!sqe) {
                std::cerr << "Failed to get SQE for read" << std::endl;
                closeConnection();
                return;
            }
            io_uring_prep_recv(sqe, static_cast<int>(fd_), buffer_, kBufferSize, 0);
            auto* ctx      = new IoUringEngine::CompletionContext{&Connection::onRead, this};
            sqe->user_data = reinterpret_cast<uint64_t>(ctx);
            io_uring_submit(engine_->handle());
        }

        void submitWrite(const void* data, std::size_t len) {
            auto* sqe = io_uring_get_sqe(engine_->handle());
            if(!sqe) {
                std::cerr << "Failed to get SQE for write" << std::endl;
                closeConnection();
                return;
            }
            io_uring_prep_send(sqe, static_cast<int>(fd_), data, static_cast<unsigned int>(len), 0);
            auto* ctx      = new IoUringEngine::CompletionContext{&Connection::onWrite, this};
            sqe->user_data = reinterpret_cast<uint64_t>(ctx);
            io_uring_submit(engine_->handle());
        }

        void submitWritev(const std::vector<struct iovec>& iovecs) {
            auto* sqe = io_uring_get_sqe(engine_->handle());
            if(!sqe) {
                std::cerr << "Failed to get SQE for writev" << std::endl;
                closeConnection();
                return;
            }
            io_uring_prep_writev(sqe, static_cast<int>(fd_), iovecs.data(), static_cast<unsigned int>(iovecs.size()), 0);
            auto* ctx      = new IoUringEngine::CompletionContext{&Connection::onWrite, this};
            sqe->user_data = reinterpret_cast<uint64_t>(ctx);
            io_uring_submit(engine_->handle());
        }

        void closeConnection() {
            if(!closed_) {
                closed_ = true;
                fd_.reset();
                std::cout << "Closed connection fd=" << fd_.get() << "\n";
                ConnectionPool::instance().release(this);
            }
        }

        void sendResponse(const Response& resp) {
            currentResponse_ = resp.toIoVec();
            responseOffset_  = 0;
            sendingResponse_ = true;

            if(!currentResponse_.iovecs.empty()) { submitWritev(currentResponse_.iovecs); }
        }

        static void onRead(IoUringEngine& eng, struct io_uring_cqe* cqe, void* data) {
            (void)eng;
            auto* conn = static_cast<Connection*>(data);
            if(cqe->res <= 0) {
                conn->closeConnection();
                return;
            }
            std::size_t bytes = cqe->res;
            conn->incoming_.append(conn->buffer_, bytes);

            std::size_t consumed = 0;
            Request     req;
            auto        result = parseRequest(conn->incoming_, req, consumed);

            if(result == ParseResult::Incomplete) {
                conn->submitRead();
                return;
            }
            if(result == ParseResult::Error) {
                conn->closeConnection();
                return;
            }

            Response resp;

            // Use middleware system instead of direct handler lookup
            Router::instance().executeRequest(req, resp);

            // Connection persistence
            conn->keepAlive_ = !req.connectionClose && req.version == "HTTP/1.1";
            if(!conn->keepAlive_) {
                resp.headers[ "Connection" ] = "close";
            } else {
                resp.headers[ "Connection" ] = "keep-alive";
            }

            // Use zero-copy response
            conn->sendResponse(resp);
            conn->incoming_.erase(0, consumed);
        }

        static void onWrite(IoUringEngine& eng, struct io_uring_cqe* cqe, void* data) {
            (void)eng;
            auto* conn = static_cast<Connection*>(data);
            if(cqe->res < 0) {
                conn->closeConnection();
                return;
            }

            if(conn->sendingResponse_) {
                // Handle partial writes for zero-copy response
                size_t written = cqe->res;
                conn->responseOffset_ += written;

                if(conn->responseOffset_ < conn->currentResponse_.iovecs.size()) {
                    // Continue sending remaining iovecs
                    std::vector<struct iovec> remaining(conn->currentResponse_.iovecs.begin() + conn->responseOffset_, conn->currentResponse_.iovecs.end());
                    conn->submitWritev(remaining);
                } else {
                    conn->sendingResponse_ = false;
                    conn->currentResponse_.clear();

                    if(conn->keepAlive_) {
                        conn->submitRead();
                    } else {
                        conn->closeConnection();
                    }
                }
            } else {
                if(conn->keepAlive_) {
                    conn->submitRead();
                } else {
                    conn->closeConnection();
                }
            }
        }
};

// ConnectionPool implementation
inline Connection* ConnectionPool::acquire(IoUringEngine& eng, int fd) {
    std::lock_guard<std::mutex> lock(m_);
    if(!pool_.empty()) {
        Connection* conn = pool_.top();
        pool_.pop();
        conn->reset(eng, fd);
        return conn;
    }
    return new Connection(eng, fd);
}

inline void ConnectionPool::release(Connection* conn) {
    if(!conn) return;
    std::lock_guard<std::mutex> lock(m_);
    if(pool_.size() < maxPoolSize_) {
        pool_.push(conn);
    } else {
        delete conn;
    }
}

inline void TcpAcceptor::onAccept(IoUringEngine& eng, struct io_uring_cqe* cqe, void* data) {
    (void)eng;
    auto* self     = static_cast<TcpAcceptor*>(data);
    int   clientFd = cqe->res;
    if(clientFd < 0) {
        self->submitAccept();
        return;
    }
    TcpAcceptor::setNonBlocking(clientFd);

    // Apply socket optimizations from config
    const auto& config = config::ConfigManager::instance().getSocketConfig();
    int         opt    = 1;

    // TCP_NODELAY for low latency
    if(config.tcpNodelay) {
        if(::setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) { std::cerr << "Warning: setsockopt(TCP_NODELAY) failed" << std::endl; }
    }

    // TCP_QUICKACK for faster ACKs
    if(config.tcpQuickAck) {
        if(::setsockopt(clientFd, IPPROTO_TCP, TCP_QUICKACK, &opt, sizeof(opt)) < 0) { std::cerr << "Warning: setsockopt(TCP_QUICKACK) failed" << std::endl; }
    }

    // TCP_CORK for better throughput (disable for HTTP)
    if(config.tcpCork) {
        if(::setsockopt(clientFd, IPPROTO_TCP, TCP_CORK, &opt, sizeof(opt)) < 0) { std::cerr << "Warning: setsockopt(TCP_CORK) failed" << std::endl; }
    } else {
        opt = 0;
        if(::setsockopt(clientFd, IPPROTO_TCP, TCP_CORK, &opt, sizeof(opt)) < 0) { std::cerr << "Warning: setsockopt(TCP_CORK) failed" << std::endl; }
    }

    // Set keep-alive with configurable settings
    if(config.tcpKeepAlive) {
        opt = 1;
        if(::setsockopt(clientFd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) { std::cerr << "Warning: setsockopt(SO_KEEPALIVE) failed" << std::endl; }

        // Optimize keep-alive parameters
        if(::setsockopt(clientFd, IPPROTO_TCP, TCP_KEEPIDLE, &config.keepAliveTime, sizeof(config.keepAliveTime)) < 0) { std::cerr << "Warning: setsockopt(TCP_KEEPIDLE) failed" << std::endl; }
        if(::setsockopt(clientFd, IPPROTO_TCP, TCP_KEEPINTVL, &config.keepAliveInterval, sizeof(config.keepAliveInterval)) < 0) {
            std::cerr << "Warning: setsockopt(TCP_KEEPINTVL) failed" << std::endl;
        }
        if(::setsockopt(clientFd, IPPROTO_TCP, TCP_KEEPCNT, &config.keepAliveProbes, sizeof(config.keepAliveProbes)) < 0) { std::cerr << "Warning: setsockopt(TCP_KEEPCNT) failed" << std::endl; }
    }

    // Set buffer sizes from config
    if(::setsockopt(clientFd, SOL_SOCKET, SO_SNDBUF, &config.sendBufferSize, sizeof(config.sendBufferSize)) < 0) { std::cerr << "Warning: setsockopt(SO_SNDBUF) failed for client" << std::endl; }
    if(::setsockopt(clientFd, SOL_SOCKET, SO_RCVBUF, &config.recvBufferSize, sizeof(config.recvBufferSize)) < 0) { std::cerr << "Warning: setsockopt(SO_RCVBUF) failed for client" << std::endl; }

    // Set TCP window scaling for better performance
    if(config.tcpWindowClamp) {
        opt = 1;
        if(::setsockopt(clientFd, IPPROTO_TCP, TCP_WINDOW_CLAMP, &opt, sizeof(opt)) < 0) { std::cerr << "Warning: setsockopt(TCP_WINDOW_CLAMP) failed" << std::endl; }
    }

    std::cout << "Accepted connection fd=" << clientFd << "\n";
    ConnectionPool::instance().acquire(self->engine_, clientFd);
    self->submitAccept();
}

} // namespace transport

} // namespace aspire

// --------------------------- main ---------------------------------------
int main() {
    using namespace aspire;
    try {
        // Configure server settings
        auto& config = config::ConfigManager::instance();

        // ===== CONFIGURATION EXAMPLES =====
        // Choose one of these preset configurations:

        // 1. High Performance (default) - balanced for most use cases
        // config.setHighPerformance();

        // 2. Low Latency - optimized for real-time applications
        config.setLowLatency();

        // 3. High Throughput - optimized for bulk data transfer
        // config.setHighThroughput();

        // 4. Conservative - minimal resource usage
        // config.setConservative();

        // ===== CUSTOM CONFIGURATION =====
        // Or customize individual settings:
        auto& socketConfig = config.getSocketConfig();

        // TCP Optimizations
        // socketConfig.tcpNodelay = true;        // Enable TCP_NODELAY for low
        // latency socketConfig.tcpQuickAck = true;       // Enable TCP_QUICKACK
        // for faster ACKs socketConfig.tcpCork = false;          // Disable
        // TCP_CORK (recommended for HTTP) socketConfig.tcpKeepAlive = true; //
        // Enable keep-alive socketConfig.tcpWindowClamp = true;    // Enable
        // TCP window scaling

        // Keep-alive Parameters
        // socketConfig.keepAliveTime = 30;       // Idle time before first
        // probe (seconds) socketConfig.keepAliveInterval = 5;    // Interval
        // between probes (seconds) socketConfig.keepAliveProbes = 3;      //
        // Number of probes before giving up

        // Buffer Sizes
        // socketConfig.sendBufferSize = 256 * 1024;  // Send buffer size
        // (bytes) socketConfig.recvBufferSize = 256 * 1024;  // Receive buffer
        // size (bytes)

        // Pool Settings
        // socketConfig.maxConnectionPoolSize = 100;   // Max connections in
        // pool socketConfig.maxBufferPoolSize = 1000;     // Max buffers in
        // pool

        // Server Settings
        // socketConfig.port = 8080;                   // Server port
        // socketConfig.backlog = 128;                 // Connection backlog
        // socketConfig.queueDepth = 1024;             // io_uring queue depth
        // socketConfig.threads = 0;                   // Number of threads (0 =
        // auto-detect)

        // ===== END CONFIGURATION =====

        auto& router = http::Router::instance();

        // ===== MIDDLEWARE EXAMPLES =====

        // Global middleware - applied to all requests
        router.use([](http::Context& ctx, std::function<void()> next) {
            std::cout << "Global middleware: " << ctx.req.method << " " << ctx.req.path << std::endl;

            // Add request timestamp
            ctx.setData("timestamp", std::chrono::system_clock::now());

            // Continue to next middleware
            next();
        });

        // Logging middleware
        router.use([](http::Context& ctx [[maybe_unused]], std::function<void()> next) {
            auto start = std::chrono::high_resolution_clock::now();

            next();

            auto end      = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            std::cout << "Request completed in " << duration.count() << " microseconds" << std::endl;
        });

        // Authentication middleware for /api routes
        router.use("/api", [](http::Context& ctx, std::function<void()> next) {
            std::string authHeader = ctx.getHeader("authorization");

            if(authHeader.empty() || authHeader != "Bearer valid-token") {
                ctx.setStatus(401, "Unauthorized");
                ctx.setBody("Authentication required\n");
                ctx.setHeader("Content-Type", "text/plain");
                return; // Don't call next() - stop the chain
            }

            // Add user info to context
            ctx.setData("user", std::string("authenticated-user"));
            next();
        });

        // Rate limiting middleware for /api routes
        router.use("/api", [](http::Context& ctx, std::function<void()> next) {
            // Simple rate limiting - in production you'd use a proper rate
            // limiter
            static std::unordered_map<std::string, int> requestCounts;
            static std::mutex                           rateLimitMutex;

            std::string clientIP = ctx.getHeader("x-forwarded-for");
            if(clientIP.empty()) { clientIP = "unknown"; }

            {
                std::lock_guard<std::mutex> lock(rateLimitMutex);
                if(requestCounts[ clientIP ] > 100) {
                    ctx.setStatus(429, "Too Many Requests");
                    ctx.setBody("Rate limit exceeded\n");
                    ctx.setHeader("Content-Type", "text/plain");
                    return;
                }
                requestCounts[ clientIP ]++;
            }

            next();
        });

        // CORS middleware
        router.use([](http::Context& ctx, std::function<void()> next) {
            ctx.setHeader("Access-Control-Allow-Origin", "*");
            ctx.setHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            ctx.setHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

            if(ctx.req.method == "OPTIONS") {
                ctx.setStatus(200, "OK");
                ctx.setBody("");
                return;
            }

            next();
        });

        // Body parsing middleware for JSON
        router.use([](http::Context& ctx, std::function<void()> next) {
            std::string contentType = ctx.getHeader("content-type");
            if(contentType.find("application/json") != std::string::npos && !ctx.req.body.empty()) {
                // In a real implementation, you'd parse JSON here
                ctx.setData("parsedBody", ctx.req.body);
            }
            next();
        });

        // ===== ROUTE HANDLERS =====

        router.registerHandler("/", [](const http::Request&, http::Response& resp) {
            resp.body                      = "Hello from aspire with middleware!\n";
            resp.headers[ "Content-Type" ] = "text/plain";
        });

        router.registerHandler("/echo", [](const http::Request& req, http::Response& resp) {
            resp.body                      = req.body;
            resp.headers[ "Content-Type" ] = "text/plain";
        });

        // ===== EXPRESS.JS STYLE MIDDLEWARE CHAINS =====

        // Define reusable middleware functions
        auto loggingMiddleware = [](http::Context& ctx [[maybe_unused]], std::function<void()> next) {
            std::cout << "Route-specific logging: " << ctx.req.method << " " << ctx.req.path << std::endl;
            next();
        };

        auto validationMiddleware = [](http::Context& ctx, std::function<void()> next) {
            if(ctx.req.method == "POST" && ctx.req.body.empty()) {
                ctx.setStatus(400, "Bad Request");
                ctx.setBody("Request body is required\n");
                ctx.setHeader("Content-Type", "text/plain");
                return;
            }
            next();
        };

        auto cacheMiddleware = [](http::Context& ctx, std::function<void()> next) {
            // Simple cache check
            if(ctx.req.method == "GET") { ctx.setHeader("Cache-Control", "public, max-age=3600"); }
            next();
        };

        auto responseTimeMiddleware = [](http::Context& ctx [[maybe_unused]], std::function<void()> next) {
            auto start = std::chrono::high_resolution_clock::now();
            next();
            auto end      = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            ctx.setHeader("X-Response-Time", std::to_string(duration.count()) + "μs");
        };

        auto userAuthMiddleware = [](http::Context& ctx, std::function<void()> next) {
            std::string token = ctx.getHeader("x-user-token");
            if(token.empty()) {
                ctx.setStatus(401, "Unauthorized");
                ctx.setBody("User token required\n");
                ctx.setHeader("Content-Type", "text/plain");
                return;
            }
            ctx.setData("userToken", token);
            next();
        };

        auto adminAuthMiddleware = [](http::Context& ctx, std::function<void()> next) {
            std::string role = ctx.getHeader("x-user-role");
            if(role != "admin") {
                ctx.setStatus(403, "Forbidden");
                ctx.setBody("Admin access required\n");
                ctx.setHeader("Content-Type", "text/plain");
                return;
            }
            ctx.setData("userRole", "admin");
            next();
        };

        // Express.js style route with multiple middleware
        router.registerHandler("/api/users", std::vector<http::Middleware>{loggingMiddleware, cacheMiddleware, responseTimeMiddleware},
                               [](const http::Request& req [[maybe_unused]], http::Response& resp) {
                                   resp.body                      = "{\"users\": [\"user1\", \"user2\", \"user3\"]}\n";
                                   resp.headers[ "Content-Type" ] = "application/json";
                               });

        // Route with authentication middleware
        router.registerHandler("/api/admin/users", std::vector<http::Middleware>{loggingMiddleware, userAuthMiddleware, adminAuthMiddleware, responseTimeMiddleware},
                               [](const http::Request& req [[maybe_unused]], http::Response& resp) {
                                   resp.body                      = "{\"admin_users\": [\"admin1\", \"admin2\"]}\n";
                                   resp.headers[ "Content-Type" ] = "application/json";
                               });

        // Route with validation middleware
        router.registerHandler("/api/posts", std::vector<http::Middleware>{loggingMiddleware, validationMiddleware, responseTimeMiddleware},
                               [](const http::Request& req [[maybe_unused]], http::Response& resp) {
                                   resp.body                      = "{\"posts\": [\"post1\", \"post2\"]}\n";
                                   resp.headers[ "Content-Type" ] = "application/json";
                               });

        // Single middleware example
        router.registerHandler("/api/simple", cacheMiddleware, [](const http::Request& req [[maybe_unused]], http::Response& resp) {
            resp.body                      = "{\"message\": \"Simple cached response\"}\n";
            resp.headers[ "Content-Type" ] = "application/json";
        });

        // API routes with authentication (original style)
        router.registerHandler("/api/users/:id", [](const http::Request& req, http::Response& resp) {
            resp.body                      = "{\"user\": {\"id\": \"" + req.path.substr(req.path.find_last_of('/') + 1) + "\"}}\n";
            resp.headers[ "Content-Type" ] = "application/json";
        });

        // Example routes with path parameters
        router.registerHandler("/users/:id", [ &router ](const http::Request& req, http::Response& resp) {
            auto        match              = router.findWithParams(req.path);
            std::string userId             = match.params[ "id" ];
            resp.body                      = "User ID: " + userId + "\n";
            resp.headers[ "Content-Type" ] = "text/plain";
        });

        router.registerHandler("/posts/:id/comments/:commentId", [ &router ](const http::Request& req, http::Response& resp) {
            auto        match              = router.findWithParams(req.path);
            std::string postId             = match.params[ "id" ];
            std::string commentId          = match.params[ "commentId" ];
            resp.body                      = "Post ID: " + postId + ", Comment ID: " + commentId + "\n";
            resp.headers[ "Content-Type" ] = "text/plain";
        });

        // Get configuration values
        int          port    = socketConfig.port;
        unsigned int threads = socketConfig.threads;
        if(threads == 0) threads = std::thread::hardware_concurrency();
        if(threads == 0) threads = 4;

        std::vector<std::thread> workers;
        workers.reserve(threads);

        for(unsigned int i = 0; i < threads; ++i) {
            workers.emplace_back([ port ]() {
                auto engExp = aspire::engine::IoUringEngine::create(config::ConfigManager::instance().getSocketConfig().queueDepth);
                if(!engExp.ok()) {
                    std::cerr << "Engine init failed: " << engExp.error() << "\n";
                    return;
                }
                auto engine = engExp.take();

                auto accExp = aspire::transport::TcpAcceptor::create(engine, port, config::ConfigManager::instance().getSocketConfig().backlog);
                if(!accExp.ok()) {
                    std::cerr << "Acceptor init failed: " << accExp.error() << "\n";
                    return;
                }
                auto acceptor = accExp.take();

                engine.run();
            });
        }

        std::cout << "Server started on port " << port << " with " << threads << " threads.\n";
        std::cout << "Configuration: TCP_NODELAY=" << (socketConfig.tcpNodelay ? "enabled" : "disabled") << ", TCP_QUICKACK=" << (socketConfig.tcpQuickAck ? "enabled" : "disabled")
                  << ", Buffer sizes: " << socketConfig.sendBufferSize / 1024 << "KB\n";

        for(auto& t : workers) t.join();
    } catch(const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
