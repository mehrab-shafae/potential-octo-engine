#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>

#include "json.hpp"

// const double PRICE_BUTTER = 1.00;
// const double PRICE_MILK   = 3.00;
// const double PRICE_EGGS   = 6.95;

constexpr int     PORT      = 7100;
constexpr size_t  DATA_SIZE = 1024ull * 1024 * 1024;  // 1 GiB
constexpr size_t  MAX_INPUT_SIZE = 10 * 1024 * 1024; // 10 MB
const std::string SAVE_DIR  = "data";

std::mutex dir_mutex;
std::mutex log_mutex;
std::atomic<size_t> total_files{0};
std::atomic<size_t> total_bytes{0};

void print_args (const int argc, const char* const argv[])
{
    std::cout << "[Aspire] Command-line arguments (argc = " << argc << "):\n";
    for (int i = 0; i < argc; ++i)
    {
        std::cout << "  argv[" << i << "]: '" << argv[ i ] << "'\n";
    }
}

void debug_log (const std::string& msg)
{
    std::cerr << "[Aspire][DEBUG] " << msg << std::endl;
}

std::string random_filename (size_t length = 16)
{
    static const char                      charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static std::mt19937                    rng{std::random_device{}()};
    static std::uniform_int_distribution<> dist (0, sizeof (charset) - 2);
    std::string                            str (length, 0);
    for (size_t i = 0; i < length; ++i)
        str[ i ] = charset[ dist (rng) ];
    return str;
}

void ensure_data_dir() {
    std::lock_guard<std::mutex> lock(dir_mutex);
    std::error_code ec;
    std::filesystem::create_directories(SAVE_DIR, ec);
    if (ec) {
        std::lock_guard<std::mutex> l2(log_mutex);
        std::cerr << "[ERROR] Failed to create data directory: " << ec.message() << std::endl;
        std::ofstream log("server.log", std::ios::app);
        log << "[ERROR] Failed to create data directory: " << ec.message() << std::endl;
    }
}

void log_message(const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cerr << msg << std::endl;
    std::ofstream log("server.log", std::ios::app);
    log << msg << std::endl;
}

void handle_client (int client_sock)
{
    // Get client IP (for logging)
    sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(client_sock, (sockaddr*)&addr, &addr_len);
    char client_ip[INET_ADDRSTRLEN] = "unknown";
    inet_ntop(AF_INET, &addr.sin_addr, client_ip, INET_ADDRSTRLEN);

    std::string data_str;
    char   buffer[ 64 * 1024 ];  // 64 KiB buffer
    size_t total_received = 0;
    bool size_limit_exceeded = false;
    while (true)
    {
        ssize_t n = recv (client_sock, buffer, sizeof(buffer), 0);
        if (n <= 0)
            break;
        if (total_received + n > MAX_INPUT_SIZE) {
            size_limit_exceeded = true;
            break;
        }
        data_str.append(buffer, n);
        total_received += n;
    }
    if (size_limit_exceeded) {
        std::string msg = "[WARN] Input size exceeded from " + std::string(client_ip);
        log_message(msg);
        send(client_sock, "ERROR: input too large\n", 23, 0);
        close(client_sock);
        return;
    }
    nlohmann::json jdata;
    std::string format = "json";
    bool valid_json = true;
    try {
        jdata = nlohmann::json::parse(data_str);
        if (jdata.contains("format")) {
            std::string f = jdata["format"].get<std::string>();
            if (f == "bson" || f == "json")
                format = f;
        }
    } catch (...) {
        valid_json = false;
        jdata["raw_data"] = data_str;
    }
    if (jdata.contains("format")) jdata.erase("format");

    ensure_data_dir();
    std::string filename = SAVE_DIR + "/" + random_filename() + (format == "bson" ? ".bson" : ".json");
    std::ofstream outfile(filename, std::ios::binary);
    if (!outfile)
    {
        std::string msg = "[ERROR] Cannot open file for writing: " + filename;
        log_message(msg);
        send(client_sock, "ERROR: cannot write file\n", 25, 0);
        close(client_sock);
        return;
    }
    size_t written_bytes = 0;
    if (format == "bson") {
        std::vector<uint8_t> bson_data = nlohmann::json::to_bson(jdata);
        outfile.write(reinterpret_cast<const char*>(bson_data.data()), bson_data.size());
        written_bytes = bson_data.size();
    } else {
        std::string outstr = jdata.dump(4);
        outfile << outstr;
        written_bytes = outstr.size();
    }
    outfile.close();
    total_files++;
    total_bytes += written_bytes;
    send(client_sock, "OK\n", 3, 0);
    close(client_sock);
    std::string msg = std::string("[INFO] Saved ") + (format == "bson" ? "BSON" : "JSON") + " to " + filename +
        " (client: " + client_ip + ", valid_json: " + (valid_json ? "yes" : "no") + ", size: " + std::to_string(written_bytes) +
        ", total_files: " + std::to_string(total_files.load()) + ", total_bytes: " + std::to_string(total_bytes.load()) + ")";
    log_message(msg);
}

int main (int argc, char* argv[])
{
    debug_log ("Program started.");
    print_args (argc, argv);
    std::cout << "Hello, World!" << std::endl;

    ensure_data_dir();

    int server_fd = socket (AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "[ERROR] Cannot create socket." << std::endl;
        return 1;
    }
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);  // 127.0.0.1
    addr.sin_port        = htons (PORT);
    if (bind (server_fd, reinterpret_cast<sockaddr*> (&addr), sizeof (addr)) < 0)
    {
        std::cerr << "[ERROR] Bind failed." << std::endl;
        close (server_fd);
        return 1;
    }
    if (listen (server_fd, 8) < 0)
    {
        std::cerr << "[ERROR] Listen failed." << std::endl;
        close (server_fd);
        return 1;
    }
    std::cerr << "[Aspire] Server listening on 127.0.0.1:" << PORT << std::endl;
    while (true)
    {
        int client_sock = accept (server_fd, nullptr, nullptr);
        if (client_sock < 0)
            continue;
        std::thread (handle_client, client_sock).detach ();
    }
    close (server_fd);

    debug_log ("Program finished.");
    return EXIT_SUCCESS;
}