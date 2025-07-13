// by MRB - DPDK Version

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ring.h>
#include <rte_tcp.h>
#include <rte_ip.h>
#include <rte_ether.h>
#include <rte_byteorder.h>
#include <rte_timer.h>
#include <rte_flow.h>

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
#include <signal.h>
#include <atomic>

namespace aspire {

namespace config {

struct DpdkConfig {
    // DPDK specific settings
    uint16_t portId{0};
    uint16_t nbRxQueues{1};
    uint16_t nbTxQueues{1};
    uint16_t nbRxDesc{1024};
    uint16_t nbTxDesc{1024};
    
    // Memory pool settings
    uint32_t mbufPoolSize{8192};
    uint32_t mbufCacheSize{250};
    
    // TCP settings
    uint32_t tcpMaxConnections{10000};
    uint32_t tcpTimeout{30};
    
    // Server settings
    uint16_t serverPort{4556};
    uint32_t maxConnections{10000};
    uint32_t bufferPoolSize{1000};
};

class ConfigManager {
public:
    static ConfigManager& instance() {
        static ConfigManager inst;
        return inst;
    }

    DpdkConfig& getDpdkConfig() { return dpdkConfig_; }
    const DpdkConfig& getDpdkConfig() const { return dpdkConfig_; }

    void setDpdkConfig(const DpdkConfig& config) { dpdkConfig_ = config; }

    void setHighPerformance() {
        dpdkConfig_.nbRxDesc = 2048;
        dpdkConfig_.nbTxDesc = 2048;
        dpdkConfig_.mbufPoolSize = 16384;
        dpdkConfig_.tcpMaxConnections = 50000;
    }

    void setLowLatency() {
        dpdkConfig_.nbRxDesc = 512;
        dpdkConfig_.nbTxDesc = 512;
        dpdkConfig_.mbufCacheSize = 32;
        dpdkConfig_.tcpTimeout = 15;
    }

    void setHighThroughput() {
        dpdkConfig_.nbRxDesc = 4096;
        dpdkConfig_.nbTxDesc = 4096;
        dpdkConfig_.mbufPoolSize = 32768;
        dpdkConfig_.tcpMaxConnections = 100000;
    }

private:
    DpdkConfig dpdkConfig_;
};

} // namespace config

namespace util {

// Expected class for error handling
template <typename T> class Expected {
public:
    // Success
    Expected(T&& val) : data_(std::in_place_index<0>, std::move(val)) {}

    Expected(const T&) = delete;

    // Failure
    Expected(std::string err) : data_(std::in_place_index<1>, std::move(err)) {}

    Expected(Expected&&) noexcept = default;
    Expected& operator=(Expected&&) noexcept = default;

    bool ok() const { return std::holds_alternative<T>(data_); }
    T& value() { return std::get<T>(data_); }
    const T& value() const { return std::get<T>(data_); }

    // Move-out helper
    T take() { 
        return std::move(std::get<T>(data_));
    }

    const std::string& error() const { return std::get<std::string>(data_); }

private:
    std::variant<T, std::string> data_;
};

// DPDK Buffer Pool
class DpdkBufferPool {
public:
    static DpdkBufferPool& instance() {
        static DpdkBufferPool inst;
        return inst;
    }

    struct rte_mempool* getMbufPool() const { return mbufPool_; }
    
    struct rte_mbuf* acquireMbuf() {
        return rte_pktmbuf_alloc(mbufPool_);
    }

    void releaseMbuf(struct rte_mbuf* mbuf) {
        if (mbuf) rte_pktmbuf_free(mbuf);
    }

    ~DpdkBufferPool() {
        if (mbufPool_) {
            rte_mempool_free(mbufPool_);
        }
    }

private:
    DpdkBufferPool() {
        const auto& config = config::ConfigManager::instance().getDpdkConfig();
        mbufPool_ = rte_pktmbuf_pool_create("mbuf_pool", 
                                           config.mbufPoolSize,
                                           config.mbufCacheSize, 
                                           0, 
                                           RTE_MBUF_DEFAULT_BUF_SIZE, 
                                           rte_socket_id());
        if (!mbufPool_) {
            throw std::runtime_error("Failed to create mbuf pool");
        }
    }

    struct rte_mempool* mbufPool_{nullptr};
};

// DPDK Connection State
struct DpdkConnection {
    uint32_t srcIp;
    uint16_t srcPort;
    uint32_t dstIp;
    uint16_t dstPort;
    
    // TCP state
    uint32_t seqNum;
    uint32_t ackNum;
    uint8_t tcpState; // SYN_SENT, ESTABLISHED, etc.
    
    // HTTP state
    std::string incomingData;
    std::string outgoingData;
    bool keepAlive{false};
    
    // Timestamp for timeout
    uint64_t lastActivity;
    
    DpdkConnection() : seqNum(0), ackNum(0), tcpState(0), lastActivity(0) {}
};

// DPDK Connection Pool
class DpdkConnectionPool {
public:
    static DpdkConnectionPool& instance() {
        static DpdkConnectionPool inst;
        return inst;
    }

    DpdkConnection* acquire(uint32_t srcIp, uint16_t srcPort, uint32_t dstIp, uint16_t dstPort) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check if connection already exists
        uint64_t key = makeConnectionKey(srcIp, srcPort, dstIp, dstPort);
        auto it = connections_.find(key);
        if (it != connections_.end()) {
            it->second->lastActivity = rte_get_tsc_cycles();
            return it->second.get();
        }
        
        // Create new connection
        auto conn = std::make_unique<DpdkConnection>();
        conn->srcIp = srcIp;
        conn->srcPort = srcPort;
        conn->dstIp = dstIp;
        conn->dstPort = dstPort;
        conn->lastActivity = rte_get_tsc_cycles();
        
        DpdkConnection* ptr = conn.get();
        connections_[key] = std::move(conn);
        
        return ptr;
    }

    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t now = rte_get_tsc_cycles();
        uint64_t timeout = config::ConfigManager::instance().getDpdkConfig().tcpTimeout * rte_get_tsc_hz();
        
        auto it = connections_.begin();
        while (it != connections_.end()) {
            if (now - it->second->lastActivity > timeout) {
                it = connections_.erase(it);
            } else {
                ++it;
            }
        }
    }

    size_t getConnectionCount() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
        return connections_.size();
    }

private:
    uint64_t makeConnectionKey(uint32_t srcIp, uint16_t srcPort, uint32_t dstIp, uint16_t dstPort) {
        return (static_cast<uint64_t>(srcIp) << 32) | srcPort |
               (static_cast<uint64_t>(dstIp) << 48) | (static_cast<uint64_t>(dstPort) << 32);
    }

    std::unordered_map<uint64_t, std::unique_ptr<DpdkConnection>> connections_;
    std::mutex mutex_;
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
        std::string result;
        result.reserve(1024); // Pre-allocate
        
        result += "HTTP/1.1 ";
        result += std::to_string(statusCode);
        result += ' ';
        result += statusMessage;
        result += "\r\n";
        
        for (const auto& [k, v] : headers) {
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
    Request& req;
    Response& res;
    std::unordered_map<std::string, std::string> params;
    std::unordered_map<std::string, std::any> data;

    Context(Request& request, Response& response) : req(request), res(response) {}

    void setStatus(int code, const std::string& message = "") {
        res.statusCode = code;
        if (!message.empty()) res.statusMessage = message;
    }

    void setHeader(const std::string& key, const std::string& value) {
        res.headers[key] = value;
    }

    void setBody(const std::string& body) {
        res.body = body;
    }

    std::string getParam(const std::string& key) const {
        auto it = params.find(key);
        return it != params.end() ? it->second : "";
    }

    std::string getHeader(const std::string& key) const {
        auto it = req.headers.find(key);
        return it != req.headers.end() ? it->second : "";
    }

    template<typename T>
    void setData(const std::string& key, T&& value) {
        data[key] = std::forward<T>(value);
    }

    template<typename T>
    T* getData(const std::string& key) {
        auto it = data.find(key);
        if (it != data.end()) {
            try {
                return std::any_cast<T>(&it->second);
            } catch(...) {
                return nullptr;
            }
        }
        return nullptr;
    }
};

// Middleware function type
using Middleware = std::function<void(Context&, std::function<void()>)>;

// Middleware types
enum class MiddlewareType {
    Global,
    Route,
    Error
};

// Middleware entry
struct MiddlewareEntry {
    Middleware middleware;
    MiddlewareType type;
    std::string path;

    MiddlewareEntry(Middleware m, MiddlewareType t, std::string p = "") 
        : middleware(std::move(m)), type(t), path(std::move(p)) {}
};

// Next function type for middleware chain
using NextFunction = std::function<void()>;

// Middleware chain executor
class MiddlewareChain {
public:
    static void execute(const std::vector<MiddlewareEntry>& middleware, Context& ctx, std::function<void()> finalHandler) {
        if (middleware.empty()) {
            finalHandler();
            return;
        }
        executeNext(middleware.begin(), middleware.end(), ctx, finalHandler);
    }

private:
    static void executeNext(std::vector<MiddlewareEntry>::const_iterator current, 
                           std::vector<MiddlewareEntry>::const_iterator end, 
                           Context& ctx, std::function<void()> finalHandler) {
        if (current == end) {
            finalHandler();
            return;
        }

        auto next = [current, end, &ctx, finalHandler]() mutable {
            executeNext(++current, end, ctx, finalHandler);
        };

        current->middleware(ctx, next);
    }
};

enum class ParseResult { Incomplete, Complete, Error };

inline ParseResult parseRequest(const std::string& buffer, Request& req, std::size_t& consumed) {
    const std::string_view delimiter = "\r\n\r\n";
    auto pos = buffer.find(delimiter);
    if (pos == std::string_view::npos) {
        return ParseResult::Incomplete;
    }

    consumed = pos + delimiter.size();
    std::string_view headerSection(buffer.data(), pos);

    // Parse start line
    auto lineEnd = headerSection.find('\n');
    if (lineEnd == std::string_view::npos) {
        return ParseResult::Error;
    }

    std::string_view startLine = headerSection.substr(0, lineEnd);
    if (startLine.back() == '\r') {
        startLine = startLine.substr(0, startLine.length() - 1);
    }

    auto space1 = startLine.find(' ');
    if (space1 == std::string_view::npos) return ParseResult::Error;

    auto space2 = startLine.find(' ', space1 + 1);
    if (space2 == std::string_view::npos) return ParseResult::Error;

    req.method = std::string(startLine.substr(0, space1));
    req.path = std::string(startLine.substr(space1 + 1, space2 - space1 - 1));
    req.version = std::string(startLine.substr(space2 + 1));

    // Parse headers
    std::string_view remaining = headerSection.substr(lineEnd + 1);
    req.headers.clear();

    while (!remaining.empty()) {
        auto lineEnd = remaining.find('\n');
        if (lineEnd == std::string_view::npos) break;

        std::string_view line = remaining.substr(0, lineEnd);
        if (line.back() == '\r') {
            line = line.substr(0, line.length() - 1);
        }

        if (line.empty()) break;

        auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            std::string_view key = line.substr(0, colon);
            std::string_view value = line.substr(colon + 1);

            while (!value.empty() && value.front() == ' ') {
                value = value.substr(1);
            }

            std::string lowerKey;
            lowerKey.reserve(key.size());
            for (char c : key) {
                lowerKey += std::tolower(c);
            }

            req.headers[std::move(lowerKey)] = std::string(value);
        }

        remaining = remaining.substr(lineEnd + 1);
    }

    // Content-Length
    req.contentLength = 0;
    auto itCL = req.headers.find("content-length");
    if (itCL != req.headers.end()) {
        try {
            req.contentLength = std::stoul(itCL->second);
        } catch(...) {
            return ParseResult::Error;
        }
    }

    // Connection header
    req.connectionClose = false;
    auto itConn = req.headers.find("connection");
    if (itConn != req.headers.end()) {
        const std::string& v = itConn->second;
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

// Radix Tree Node for efficient path matching
struct RadixNode {
    std::string path;
    Handler handler;
    std::vector<std::unique_ptr<RadixNode>> children;
    bool isParam{false};
    std::string paramName;
    std::unordered_map<char, RadixNode*> charMap;

    RadixNode() = default;
    explicit RadixNode(std::string p) : path(std::move(p)) {}

    void buildCharMap() {
        charMap.clear();
        for (auto& child : children) {
            if (!child->path.empty()) {
                charMap[child->path[0]] = child.get();
            }
        }
    }
};

class Router {
public:
    static Router& instance() {
        static Router r;
        return r;
    }

    void use(Middleware middleware) {
        middleware_.emplace_back(std::move(middleware), MiddlewareType::Global);
    }

    void use(const std::string& path, Middleware middleware) {
        middleware_.emplace_back(std::move(middleware), MiddlewareType::Route, path);
    }

    void useError(Middleware middleware) {
        errorMiddleware_.emplace_back(std::move(middleware));
    }

    void registerHandler(const std::string& path, Handler h) {
        insertRoute(&root_, path, 0, std::move(h));
        rebuildCharMaps(&root_);
    }

    void registerHandler(const std::string& path, const std::vector<Middleware>& middleware, Handler h) {
        auto compositeHandler = [this, middleware, h = std::move(h)](const Request& req, Response& res) {
            Context ctx(const_cast<Request&>(req), res);
            auto routeMatch = this->findWithParams(req.path);
            ctx.params = routeMatch.params;

            auto finalHandler = [&ctx, &h]() {
                h(ctx.req, ctx.res);
            };

            std::vector<MiddlewareEntry> middlewareEntries;
            for (const auto& m : middleware) {
                middlewareEntries.emplace_back(m, MiddlewareType::Global);
            }

            MiddlewareChain::execute(middlewareEntries, ctx, finalHandler);
        };

        insertRoute(&root_, path, 0, std::move(compositeHandler));
        rebuildCharMaps(&root_);
    }

    void registerHandler(const std::string& path, Middleware middleware, Handler h) {
        registerHandler(path, std::vector<Middleware>{std::move(middleware)}, std::move(h));
    }

    Handler find(const std::string& path) const {
        return findRoute(&root_, path, 0);
    }

    struct RouteMatch {
        Handler handler;
        std::unordered_map<std::string, std::string> params;
    };

    RouteMatch findWithParams(const std::string& path) const {
        RouteMatch match;
        match.handler = findRouteWithParams(&root_, path, 0, match.params);
        return match;
    }

    void executeRequest(Request& req, Response& res) {
        Context ctx(req, res);

        std::vector<MiddlewareEntry> applicableMiddleware;

        for (const auto& entry : middleware_) {
            if (entry.type == MiddlewareType::Global) {
                applicableMiddleware.push_back(entry);
            }
        }

        for (const auto& entry : middleware_) {
            if (entry.type == MiddlewareType::Route && 
                (entry.path.empty() || req.path.find(entry.path) == 0)) {
                applicableMiddleware.push_back(entry);
            }
        }

        auto routeMatch = findWithParams(req.path);
        ctx.params = routeMatch.params;

        auto finalHandler = [&ctx, &routeMatch]() {
            if (routeMatch.handler) {
                routeMatch.handler(ctx.req, ctx.res);
            } else {
                ctx.setStatus(404, "Not Found");
                ctx.setBody("404 Not Found\n");
                ctx.setHeader("Content-Type", "text/plain");
            }
        };

        MiddlewareChain::execute(applicableMiddleware, ctx, finalHandler);
    }

private:
    RadixNode root_;
    std::vector<MiddlewareEntry> middleware_;
    std::vector<Middleware> errorMiddleware_;

    void rebuildCharMaps(RadixNode* node) {
        node->buildCharMap();
        for (auto& child : node->children) {
            rebuildCharMaps(child.get());
        }
    }

    void insertRoute(RadixNode* node, const std::string& path, size_t pos, Handler h) {
        if (pos >= path.length()) {
            node->handler = std::move(h);
            return;
        }

        if (path[pos] == ':') {
            auto slashPos = path.find('/', pos);
            if (slashPos == std::string::npos) slashPos = path.length();

            std::string paramName = path.substr(pos + 1, slashPos - pos - 1);
            std::string remainingPath = path.substr(slashPos);

            auto paramNode = std::make_unique<RadixNode>("");
            paramNode->isParam = true;
            paramNode->paramName = paramName;

            if (slashPos < path.length()) {
                insertRoute(paramNode.get(), path, slashPos, std::move(h));
            } else {
                paramNode->handler = std::move(h);
            }

            node->children.push_back(std::move(paramNode));
            return;
        }

        char firstChar = pos < path.length() ? path[pos] : '\0';
        auto it = node->charMap.find(firstChar);
        if (it != node->charMap.end()) {
            RadixNode* child = it->second;
            size_t common = 0;
            while (common < child->path.length() && pos + common < path.length() && 
                   child->path[common] == path[pos + common]) {
                common++;
            }

            if (common > 0) {
                if (common == child->path.length()) {
                    insertRoute(child, path, pos + common, std::move(h));
                    return;
                } else {
                    auto newChild = std::make_unique<RadixNode>(child->path.substr(common));
                    newChild->handler = std::move(child->handler);
                    newChild->children = std::move(child->children);
                    newChild->isParam = child->isParam;
                    newChild->paramName = child->paramName;

                    child->path = child->path.substr(0, common);
                    child->children.clear();
                    child->children.push_back(std::move(newChild));

                    insertRoute(child, path, pos + common, std::move(h));
                    return;
                }
            }
        }

        auto newChild = std::make_unique<RadixNode>(path.substr(pos));
        newChild->handler = std::move(h);
        node->children.push_back(std::move(newChild));
    }

    Handler findRoute(const RadixNode* node, const std::string& path, size_t pos) const {
        if (pos >= path.length()) {
            return node->handler;
        }

        char firstChar = path[pos];
        auto it = node->charMap.find(firstChar);
        if (it != node->charMap.end()) {
            const RadixNode* child = it->second;
            if (pos + child->path.length() <= path.length() && 
                path.substr(pos, child->path.length()) == child->path) {
                return findRoute(child, path, pos + child->path.length());
            }
        }

        return nullptr;
    }

    Handler findRouteWithParams(const RadixNode* node, const std::string& path, size_t pos, 
                               std::unordered_map<std::string, std::string>& params) const {
        if (pos >= path.length()) {
            return node->handler;
        }

        char firstChar = path[pos];
        auto it = node->charMap.find(firstChar);
        if (it != node->charMap.end()) {
            const RadixNode* child = it->second;
            if (pos + child->path.length() <= path.length() && 
                path.substr(pos, child->path.length()) == child->path) {
                return findRouteWithParams(child, path, pos + child->path.length(), params);
            }
        }

        for (const auto& child : node->children) {
            if (child->isParam) {
                auto slashPos = path.find('/', pos);
                if (slashPos == std::string::npos) slashPos = path.length();

                std::string paramValue = path.substr(pos, slashPos - pos);
                params[child->paramName] = paramValue;

                if (slashPos < path.length()) {
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

// --------------------------- dpdk namespace ----------------------------
namespace dpdk {

// Global signal handler
static std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nReceived signal " << signal << ", shutting down gracefully..." << std::endl;
        g_running = false;
    }
}

class DpdkEngine {
public:
    static util::Expected<DpdkEngine> create() {
        DpdkEngine engine;
        if (!engine.initialize()) {
            return std::string("Failed to initialize DPDK engine");
        }
        return engine;
    }

    ~DpdkEngine() {
        cleanup();
    }

    DpdkEngine(const DpdkEngine&) = delete;
    DpdkEngine& operator=(const DpdkEngine&) = delete;
    
    // Move constructor and assignment
    DpdkEngine(DpdkEngine&& other) noexcept 
        : running_(other.running_) {
        other.running_ = false;
    }
    
    DpdkEngine& operator=(DpdkEngine&& other) noexcept {
        if (this != &other) {
            cleanup();
            running_ = other.running_;
            other.running_ = false;
        }
        return *this;
    }

    void run() {
        running_ = true;
        
        // Set up signal handlers
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        
        std::cout << "DPDK engine started. Press Ctrl+C to stop." << std::endl;
        
        while (running_ && g_running) {
            processPackets();
            util::DpdkConnectionPool::instance().cleanup();
            
            // Small delay to prevent busy waiting
            rte_delay_us(100);
        }
        
        std::cout << "DPDK engine stopped." << std::endl;
    }

    void stop() { running_ = false; }

public:
    DpdkEngine() = default;

private:

    bool initialize() {
        const auto& config = config::ConfigManager::instance().getDpdkConfig();
        
        // Check port availability
        if (rte_eth_dev_count_avail() == 0) {
            std::cerr << "No Ethernet ports available" << std::endl;
            return false;
        }

        // Configure port
        if (!configurePort(config.portId)) {
            return false;
        }

        // Start port
        if (rte_eth_dev_start(config.portId) < 0) {
            std::cerr << "Failed to start port " << config.portId << std::endl;
            return false;
        }

        // Enable promiscuous mode for testing
        rte_eth_promiscuous_enable(config.portId);

        std::cout << "DPDK engine initialized on port " << config.portId << std::endl;
        return true;
    }

    bool configurePort(uint16_t portId) {
        const auto& config = config::ConfigManager::instance().getDpdkConfig();
        
        struct rte_eth_conf portConf = {};
        portConf.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
        portConf.rx_adv_conf.rss_conf.rss_key = nullptr;
        portConf.rx_adv_conf.rss_conf.rss_hf = RTE_ETH_RSS_IP | RTE_ETH_RSS_TCP | RTE_ETH_RSS_UDP;

        if (rte_eth_dev_configure(portId, config.nbRxQueues, config.nbTxQueues, &portConf) < 0) {
            std::cerr << "Failed to configure port " << portId << std::endl;
            return false;
        }

        // Setup RX queues
        for (uint16_t q = 0; q < config.nbRxQueues; q++) {
            if (rte_eth_rx_queue_setup(portId, q, config.nbRxDesc, 
                                      rte_eth_dev_socket_id(portId), nullptr, 
                                      util::DpdkBufferPool::instance().getMbufPool()) < 0) {
                std::cerr << "Failed to setup RX queue " << q << std::endl;
                return false;
            }
        }

        // Setup TX queues
        for (uint16_t q = 0; q < config.nbTxQueues; q++) {
            if (rte_eth_tx_queue_setup(portId, q, config.nbTxDesc, 
                                      rte_eth_dev_socket_id(portId), nullptr) < 0) {
                std::cerr << "Failed to setup TX queue " << q << std::endl;
                return false;
            }
        }

        return true;
    }

    void processPackets() {
        const auto& config = config::ConfigManager::instance().getDpdkConfig();
        struct rte_mbuf* pkts[32];
        
        for (uint16_t q = 0; q < config.nbRxQueues; q++) {
            uint16_t nbRx = rte_eth_rx_burst(config.portId, q, pkts, 32);
            
            for (uint16_t i = 0; i < nbRx; i++) {
                processPacket(pkts[i]);
            }
        }
    }

    void processPacket(struct rte_mbuf* mbuf) {
        if (!mbuf) return;
        
        struct rte_ether_hdr* ethHdr = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr*);
        
        if (rte_be_to_cpu_16(ethHdr->ether_type) != RTE_ETHER_TYPE_IPV4) {
            rte_pktmbuf_free(mbuf);
            return;
        }

        struct rte_ipv4_hdr* ipHdr = (struct rte_ipv4_hdr*)(ethHdr + 1);
        if (ipHdr->next_proto_id != IPPROTO_TCP) {
            rte_pktmbuf_free(mbuf);
            return;
        }

        struct rte_tcp_hdr* tcpHdr = (struct rte_tcp_hdr*)(ipHdr + 1);
        uint16_t srcPort = rte_be_to_cpu_16(tcpHdr->src_port);
        uint16_t dstPort = rte_be_to_cpu_16(tcpHdr->dst_port);
        
        // Check if this is HTTP traffic on our port
        const auto& config = config::ConfigManager::instance().getDpdkConfig();
        if (dstPort != config.serverPort) {
            rte_pktmbuf_free(mbuf);
            return;
        }

        // Get connection
        uint32_t srcIp = rte_be_to_cpu_32(ipHdr->src_addr);
        uint32_t dstIp = rte_be_to_cpu_32(ipHdr->dst_addr);
        
        auto* conn = util::DpdkConnectionPool::instance().acquire(srcIp, srcPort, dstIp, dstPort);
        
        // Process TCP packet
        processTcpPacket(conn, mbuf, tcpHdr, ipHdr);
    }

    void processTcpPacket(util::DpdkConnection* conn, struct rte_mbuf* mbuf, 
                         struct rte_tcp_hdr* tcpHdr, struct rte_ipv4_hdr* ipHdr) {
        if (!conn || !mbuf) {
            rte_pktmbuf_free(mbuf);
            return;
        }
        
        // Extract payload
        uint8_t* payload = (uint8_t*)(tcpHdr + 1);
        uint32_t payloadLen = rte_pktmbuf_data_len(mbuf) - 
                             sizeof(struct rte_ether_hdr) - 
                             sizeof(struct rte_ipv4_hdr) - 
                             (tcpHdr->data_off >> 4) * 4;

        if (payloadLen > 0) {
            conn->incomingData.append((char*)payload, payloadLen);
            
            // Parse HTTP request
            std::size_t consumed = 0;
            http::Request req;
            auto result = http::parseRequest(conn->incomingData, req, consumed);
            
            if (result == http::ParseResult::Complete) {
                try {
                    http::Response resp;
                    http::Router::instance().executeRequest(req, resp);
                    
                    // Send response
                    std::string responseStr = resp.serialize();
                    conn->outgoingData = responseStr;
                    conn->keepAlive = !req.connectionClose && req.version == "HTTP/1.1";
                    
                    // Send response using DPDK TX
                    sendResponse(conn, responseStr, ipHdr, tcpHdr);
                } catch (const std::exception& e) {
                    std::cerr << "Error processing HTTP request: " << e.what() << std::endl;
                }
            }
            
            if (result == http::ParseResult::Complete) {
                conn->incomingData.erase(0, consumed);
            }
        }
        
        rte_pktmbuf_free(mbuf);
    }

    void sendResponse(util::DpdkConnection* conn [[maybe_unused]], const std::string& response, 
                     struct rte_ipv4_hdr* ipHdr, struct rte_tcp_hdr* tcpHdr) {
        const auto& config = config::ConfigManager::instance().getDpdkConfig();
        
        // Create response packet
        struct rte_mbuf* respMbuf = rte_pktmbuf_alloc(util::DpdkBufferPool::instance().getMbufPool());
        if (!respMbuf) {
            std::cerr << "Failed to allocate mbuf for response" << std::endl;
            return;
        }

        // Set up packet headers
        struct rte_ether_hdr* ethHdr = rte_pktmbuf_mtod(respMbuf, struct rte_ether_hdr*);
        struct rte_ipv4_hdr* respIpHdr = (struct rte_ipv4_hdr*)(ethHdr + 1);
        struct rte_tcp_hdr* respTcpHdr = (struct rte_tcp_hdr*)(respIpHdr + 1);
        uint8_t* payload = (uint8_t*)(respTcpHdr + 1);

        // Copy response data
        size_t responseLen = std::min(response.size(), (size_t)(RTE_MBUF_DEFAULT_BUF_SIZE - 
                                                              sizeof(struct rte_ether_hdr) - 
                                                              sizeof(struct rte_ipv4_hdr) - 
                                                              sizeof(struct rte_tcp_hdr)));
        memcpy(payload, response.data(), responseLen);

        // Set up Ethernet header (swap src/dst)
        memcpy(ethHdr->dst_addr.addr_bytes, ethHdr->src_addr.addr_bytes, RTE_ETHER_ADDR_LEN);
        memcpy(ethHdr->src_addr.addr_bytes, ethHdr->dst_addr.addr_bytes, RTE_ETHER_ADDR_LEN);
        ethHdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

        // Set up IP header (swap src/dst)
        respIpHdr->version_ihl = 0x45;
        respIpHdr->type_of_service = 0;
        respIpHdr->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) + 
                                                   sizeof(struct rte_tcp_hdr) + responseLen);
        respIpHdr->packet_id = 0;
        respIpHdr->fragment_offset = 0;
        respIpHdr->time_to_live = 64;
        respIpHdr->next_proto_id = IPPROTO_TCP;
        respIpHdr->hdr_checksum = 0;
        respIpHdr->src_addr = ipHdr->dst_addr;
        respIpHdr->dst_addr = ipHdr->src_addr;

        // Set up TCP header (swap src/dst ports)
        respTcpHdr->src_port = tcpHdr->dst_port;
        respTcpHdr->dst_port = tcpHdr->src_port;
        respTcpHdr->sent_seq = tcpHdr->recv_ack;
        respTcpHdr->recv_ack = rte_cpu_to_be_32(rte_be_to_cpu_32(tcpHdr->sent_seq) + 1);
        respTcpHdr->data_off = 0x50; // 5 words
        respTcpHdr->tcp_flags = RTE_TCP_ACK_FLAG | RTE_TCP_PSH_FLAG;
        respTcpHdr->rx_win = rte_cpu_to_be_16(65535);
        respTcpHdr->tcp_urp = 0;

        // Set packet length
        respMbuf->data_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + 
                             sizeof(struct rte_tcp_hdr) + responseLen;
        respMbuf->pkt_len = respMbuf->data_len;

        // Send packet
        uint16_t nbTx = rte_eth_tx_burst(config.portId, 0, &respMbuf, 1);
        if (nbTx == 0) {
            rte_pktmbuf_free(respMbuf);
        }
    }

    void cleanup() {
        const auto& config = config::ConfigManager::instance().getDpdkConfig();
        rte_eth_dev_stop(config.portId);
        rte_eth_dev_close(config.portId);
    }

    bool running_{false};
};

} // namespace dpdk

} // namespace aspire

// --------------------------- main ---------------------------------------
int main(int argc, char* argv[]) {
    using namespace aspire;
    
    try {
        // Initialize DPDK EAL
        int ret = rte_eal_init(argc, argv);
        if (ret < 0) {
            std::cerr << "Failed to initialize DPDK EAL" << std::endl;
            return 1;
        }

        // Configure server settings
        auto& config = config::ConfigManager::instance();
        
        // Set DPDK configuration
        config.setHighPerformance();
        
        auto& dpdkConfig = config.getDpdkConfig();
        
        // Customize DPDK settings
        dpdkConfig.portId = 0;  // Use first available port
        dpdkConfig.serverPort = 4556;
        dpdkConfig.nbRxQueues = 2;  // Reduced for single socket
        dpdkConfig.nbTxQueues = 2;  // Reduced for single socket
        dpdkConfig.nbRxDesc = 512;  // Reduced for memory efficiency
        dpdkConfig.nbTxDesc = 512;  // Reduced for memory efficiency
        dpdkConfig.mbufPoolSize = 4096;  // Reduced for memory efficiency
        dpdkConfig.mbufCacheSize = 128;  // Reduced for memory efficiency
        dpdkConfig.tcpMaxConnections = 5000;  // Reduced for memory efficiency
        dpdkConfig.tcpTimeout = 30;

        // Check if we have any ports available
        if (rte_eth_dev_count_avail() == 0) {
            std::cerr << "No DPDK ports available. Make sure you have:" << std::endl;
            std::cerr << "1. DPDK drivers loaded (e.g., vfio-pci)" << std::endl;
            std::cerr << "2. Huge pages configured" << std::endl;
            std::cerr << "3. Network interfaces bound to DPDK" << std::endl;
            std::cerr << "4. Running with proper EAL arguments" << std::endl;
            return 1;
        }

        std::cout << "Available DPDK ports: " << rte_eth_dev_count_avail() << std::endl;

        // Initialize HTTP router
        auto& router = http::Router::instance();

        // Add middleware
        router.use([](http::Context& ctx, std::function<void()> next) {
            std::cout << "DPDK Global middleware: " << ctx.req.method << " " << ctx.req.path << std::endl;
            next();
        });

        // Add routes
        router.registerHandler("/", [](const http::Request&, http::Response& resp) {
            resp.body = "Hello from DPDK-powered aspire!\n";
            resp.headers["Content-Type"] = "text/plain";
        });

        router.registerHandler("/echo", [](const http::Request& req, http::Response& resp) {
            resp.body = req.body;
            resp.headers["Content-Type"] = "text/plain";
        });

        router.registerHandler("/api/status", [](const http::Request&, http::Response& resp) {
            resp.body = "{\"status\": \"running\", \"engine\": \"dpdk\"}\n";
            resp.headers["Content-Type"] = "application/json";
        });

        router.registerHandler("/api/stats", [](const http::Request&, http::Response& resp) {
            resp.body = "{\"connections\": " + 
                       std::to_string(util::DpdkConnectionPool::instance().getConnectionCount()) + 
                       ", \"engine\": \"dpdk\"}\n";
            resp.headers["Content-Type"] = "application/json";
        });

        // Create and run DPDK engine
        auto engineExp = dpdk::DpdkEngine::create();
        if (!engineExp.ok()) {
            std::cerr << "Failed to create DPDK engine: " << engineExp.error() << std::endl;
            return 1;
        }

        auto engine = engineExp.take();
        
        std::cout << "DPDK HTTP server started on port " << dpdkConfig.serverPort << std::endl;
        std::cout << "Configuration: RX queues=" << dpdkConfig.nbRxQueues 
                  << ", TX queues=" << dpdkConfig.nbTxQueues 
                  << ", Buffer pool size=" << dpdkConfig.mbufPoolSize << std::endl;
        std::cout << "Server is ready to handle HTTP requests..." << std::endl;

        engine.run();

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
} 