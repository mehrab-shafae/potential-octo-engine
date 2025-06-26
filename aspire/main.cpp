#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// const double PRICE_BUTTER = 1.00;
// const double PRICE_MILK   = 3.00;
// const double PRICE_EGGS   = 6.95;

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

int main (int argc, char* argv[])
{
    debug_log ("Program started.");
    print_args (argc, argv);
    std::cout << "Hello, World!" << std::endl;
    debug_log ("Program finished.");
    return EXIT_SUCCESS;
}