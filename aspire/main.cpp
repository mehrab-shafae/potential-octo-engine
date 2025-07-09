#include "engine/io_uring_engine.h"
#include <iostream>

int main() {
    try {
        aspire::engine::IoUringEngine engine{};
        std::cout << "IoUringEngine initialized successfully.\n";
        // engine.run(); // TODO: implement event loop
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
} 