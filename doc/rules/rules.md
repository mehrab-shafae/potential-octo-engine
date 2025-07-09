# Aspire C++ Coding Rules and Principles

## Relationship to NASA JPL C Coding Standard

This document is a supplement and customization of the official [NASA JPL C Coding Standard] for the Aspire project and C++20.

- All rules and principles of the JPL standard must be followed, except where specifically modified or extended in this document for C++20 or Aspire project requirements.
- In any case of ambiguity or silence in this document, the JPL standard is the primary reference.
- Sections that are specific to C++20 or Aspire customization are explicitly marked as such.

**MANDATORY COMPLIANCE:**  
All contributors, reviewers, and automated systems are required to adhere strictly to every rule, guideline, and example provided in this document. No deviation, omission, or alternative interpretation is permitted. The standards, conventions, and requirements set forth herein are exhaustive and binding for all aspects of the Aspire project.

## General Language and Professionalism

All contributions to the Aspire project—including code, comments, documentation, commit messages, log messages, and user-facing text—must conform precisely to the standards, conventions, and tone established in this document. Any content that does not fully align with these requirements is unacceptable and will be rejected.

All contributions to the Aspire project must adhere to the following language and professionalism standards:

- **Language:** All code, comments, documentation, commit messages, log messages, and user-facing text must be written in clear, simple, and formal English. The use of other languages is strictly prohibited.
- **Professionalism:** All identifiers (class names, function names, variables, etc.), comments, and documentation must maintain a professional and organizational tone. The following are strictly forbidden:
    - Jokes, humor, or informal remarks in any part of the codebase or documentation.
    - Slang, colloquial expressions, or abbreviations that are not universally recognized in the software engineering field.
    - Non-English words, phrases, or sentences.
    - Emojis, emoticons, or other non-standard symbols.
    - Unprofessional, unclear, or playful naming for any code element.
- **Clarity:** All written content must be unambiguous and easy to understand for an international, professional audience. If a term or abbreviation is not universally recognized, it must be clearly defined.

Any violation of these standards will be considered a breach of project policy and must be corrected immediately. These rules are intended to ensure the Aspire project maintains the highest standards of clarity, professionalism, and organizational integrity.

---

## Project Scope and Platform Constraints

All code in the Aspire project shall strictly adhere to the following constraints:

- **Programming Language:** C++20 only. No features or syntax beyond the C++20 standard are permitted. All code must compile cleanly with a C++20-compliant compiler.
- **Operating System:** Linux only. The codebase is intended exclusively for Linux environments. It is permitted to use Linux-specific APIs, system calls, and facilities where appropriate.
- **Permitted Features:**
    - All standard C++20 library features are allowed.
    - All standard Linux system calls and POSIX APIs are allowed.
    - Third-party libraries may only be used if they are compatible with both C++20 and Linux, and their use is explicitly approved by project maintainers.
- **Prohibited Features:**
    - Any language features, libraries, or APIs not available in C++20 or not supported on Linux are strictly forbidden.
    - No Windows-specific, macOS-specific, or non-portable extensions may be used.

All contributors are responsible for ensuring that their code is fully compliant with these constraints. Any deviation must be justified and approved in advance.

---

## Introduction

This document establishes the official coding rules and principles for the Aspire project, inspired by the NASA JPL C Coding Standard and adapted for modern C++. These rules are to be followed rigorously by all contributors. The aim is to maximize code safety, clarity, maintainability, and predictability, while leveraging the strengths of C++ in a disciplined manner.

---

## 1. Class Structure and Usage

### Rule 1.1: Classes Shall Be Simple and Focused
*Aspired Customization*
A class shall encapsulate a single, well-defined responsibility. Classes with multiple, unrelated responsibilities are forbidden.

**Example (Compliant):**
```cpp
// Timer class: Only responsible for timing
class Timer {
public:
    void start();
    void stop();
    double elapsed_seconds() const;
private:
    std::chrono::time_point<std::chrono::steady_clock> start_;
    std::chrono::time_point<std::chrono::steady_clock> end_;
};
```

**Example (Non-Compliant):**
```cpp
// BAD: Timer class also logs messages
class Timer {
public:
    void start();
    void stop();
    double elapsed_seconds() const;
    void log(const std::string& msg); // Not allowed
};
```

### Rule 1.2: Singleton Pattern for Unique Utilities
*Aspired Customization*
Where only one instance of a utility class is required, the singleton pattern shall be used. The constructor must be private, and copy/move operations deleted.

**Example:**
```cpp
class Config {
public:
    static Config& instance();
    bool load(const std::string& path);
private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
};
```

---

## 2. Global State and Variables

### Rule 2.1: Global Variables Are Prohibited
*Aspired Customization*
No global variables shall be used. Shared state must be encapsulated within classes as private static members if necessary.

**Example (Compliant):**
```cpp
class Counter {
public:
    static int get();
    static void increment();
private:
    static int value_;
};
```

**Example (Non-Compliant):**
```cpp
// BAD: Global variable
int counter = 0;
```

---

## 3. Function Simplicity and Clarity

### Rule 3.1: Functions Shall Be Short and Do One Thing
*Aspired Customization*
Each function shall perform a single, well-defined task. Functions exceeding 30 lines are discouraged and must be justified.

**Example (Compliant):**
```cpp
bool FileReader::open(const std::string& path);
bool FileReader::read_line(std::string& out);
```

**Example (Non-Compliant):**
```cpp
// BAD: Function does file open, read, and parse in one
bool FileReader::process(const std::string& path, Data& out);
```

---

## 4. Error Handling

### Rule 4.1: All Public Methods Shall Return Status
*Aspired Customization*
All public methods must return a status indicator (e.g., bool, enum, or error code). Silent failure is forbidden.

**Example:**
```cpp
bool Logger::access_log(const std::string& msg);
```

### Rule 4.2: Error Codes Must Be Checked
*Aspired Customization*
All error codes and return values must be checked by the caller. Ignoring return values is forbidden.

**Example (Compliant):**
```cpp
if (!logger.access_log("entry")) {
    // Handle error
}
```

**Example (Non-Compliant):**
```cpp
logger.access_log("entry"); // BAD: Return value ignored
```

---

## 5. Naming and Documentation

### Rule 5.1: Names Shall Be Descriptive
*Aspired Customization*
All identifiers (classes, functions, variables) must have clear, descriptive names. Abbreviations are discouraged unless universally understood.

### Rule 5.2: Doxygen-Style Comments Required
*Aspired Customization*
Every public class and method must be documented with a Doxygen-style comment, describing its purpose, parameters, and return value.

**Example:**
```cpp
/**
 * @brief Reads a line from the file.
 * @param out The line read.
 * @return true if successful, false otherwise.
 */
bool FileReader::read_line(std::string& out);
```

---

## 6. C++-Only Features

### Rule 6.1: Use Only Standard C++ Features
*C++20-specific*
C-style constructs such as FILE*, printf, and char* are forbidden in utility classes. Use std::string, std::ifstream, std::ofstream, and other standard library types.

**Example (Compliant):**
```cpp
std::ofstream ofs("log.txt");
```

**Example (Non-Compliant):**
```cpp
FILE* fp = fopen("log.txt", "a"); // BAD
```

### Rule 6.2: Avoid Advanced C++ Features
*C++20-specific*
Templates, macros (except include guards), and metaprogramming are forbidden in utility classes.

---

## 7. Thread Safety

### Rule 7.1: Public Methods Must Be Thread-Safe
*Aspired Customization*
If a class is used from multiple threads, all public methods must be thread-safe. Use std::mutex and lock with std::lock_guard.

**Example:**
```cpp
class SafeCounter {
public:
    void increment() {
        std::lock_guard<std::mutex> lock(mtx_);
        ++value_;
    }
    int get() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return value_;
    }
private:
    mutable std::mutex mtx_;
    int value_ = 0;
};
```

---

## 8. Macros

### Rule 8.1: Macros Are Forbidden Except for Include Guards
*Aspired Customization*
Macros shall not be used for logic, configuration, or logging. Only include guards are permitted.

**Example (Compliant):**
```cpp
#ifndef CONFIG_HPP
#define CONFIG_HPP
// ...
#endif // CONFIG_HPP
```

**Example (Non-Compliant):**
```cpp
#define LOG(x) // BAD
```

---

## 9. File Handling

### Rule 9.1: Always Check File Open Status
*Aspired Customization*
Files must be checked for successful opening before use. Use RAII for file streams.

**Example:**
```cpp
std::ofstream ofs("data.txt");
if (!ofs.is_open()) {
    // Handle error
}
```

---

## 10. Simplicity and Readability

### Rule 10.1: Code Shall Be Readable and Maintainable
*Aspired Customization*
Code must be easy to read, maintain, and review. Clever tricks, obscure syntax, and non-standard extensions are forbidden.

**Example (Compliant):**
```cpp
for (const auto& item : items) {
    process(item);
}
```

**Example (Non-Compliant):**
```cpp
// BAD: Obscure pointer arithmetic
for (auto* p = arr; *p; ++p) { do_something(*p); }
```

---

## 11. Example: Utility Class Skeletons

### Logger
```cpp
/**
 * @brief Thread-safe logger class (singleton).
 */
class Logger {
public:
    static Logger& instance();
    bool info(const std::string& msg);
    bool error(const std::string& msg);
    bool debug(const std::string& msg);
    bool access_log(const std::string& msg);
private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    std::mutex mtx_;
    std::mutex access_log_mtx_;
};
```

### Timer
```cpp
/**
 * @brief Simple timer utility for measuring elapsed time.
 */
class Timer {
public:
    void start();
    void stop();
    double elapsed_seconds() const;
private:
    std::chrono::time_point<std::chrono::steady_clock> start_;
    std::chrono::time_point<std::chrono::steady_clock> end_;
};
```

### Config
```cpp
/**
 * @brief Singleton configuration loader.
 */
class Config {
public:
    static Config& instance();
    bool load(const std::string& path);
    std::string get(const std::string& key) const;
private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    std::map<std::string, std::string> data_;
};
```

---

## Naming Conventions

*Aspired Customization*
All code in the Aspire project must follow these naming conventions:

- **Variables and Functions:** Use `lower_case_with_underscores` for all variable and function names (e.g., `sensor_value`, `read_data()`).
- **Classes and Structs:** Use `UpperCamelCase` for all class and struct names (e.g., `RequestHandler`, `SensorData`).

These conventions are mandatory and help ensure consistency, clarity, and professionalism throughout the codebase.

## Line Length Limit

*Aspired Customization*
- No line of code, comment, or documentation shall exceed 80 characters in length. This ensures readability and consistency across all environments and tools.

---

## Review and Tooling

*Aspired Customization*
- Each small change (commit or pull request) should have a maximum diff of 20 lines. This encourages incremental, reviewable changes and helps maintain code quality.

---

## 12. Project Structure and File Organization

*Aspired Customization - Enterprise-Scale Architecture*

The Aspire project must follow a hierarchical, modular structure inspired by large-scale projects like Linux and Git. This structure ensures scalability, maintainability, and clear separation of concerns as the project grows.

### Rule 12.1: Hierarchical Directory Structure

The project shall follow this mandatory directory structure:

```
aspiredb/
├── aspire/                    # Core application directory
│   ├── core/                 # Core system components
│   │   ├── config/          # Configuration management
│   │   │   ├── Config.hpp
│   │   │   ├── Config.cpp
│   │   │   └── Config_test.cpp
│   │   ├── logging/         # Logging system
│   │   │   ├── Logger.hpp
│   │   │   ├── Logger.cpp
│   │   │   └── Logger_test.cpp
│   │   ├── networking/      # Network handling
│   │   │   ├── Connection.hpp
│   │   │   ├── Connection.cpp
│   │   │   └── Connection_test.cpp
│   │   └── utils/           # Core utilities
│   │       ├── Timer.hpp
│   │       ├── Timer.cpp
│   │       └── Timer_test.cpp
│   ├── modules/             # Feature modules
│   │   ├── auth/            # Authentication module
│   │   │   ├── AuthModule.hpp
│   │   │   ├── AuthModule.cpp
│   │   │   ├── UserManager.hpp
│   │   │   ├── UserManager.cpp
│   │   │   ├── SessionManager.hpp
│   │   │   ├── SessionManager.cpp
│   │   │   └── auth_test.cpp
│   │   ├── database/        # Database operations
│   │   │   ├── DatabaseModule.hpp
│   │   │   ├── DatabaseModule.cpp
│   │   │   ├── QueryEngine.hpp
│   │   │   ├── QueryEngine.cpp
│   │   │   └── database_test.cpp
│   │   ├── http/            # HTTP handling
│   │   │   ├── HttpModule.hpp
│   │   │   ├── HttpModule.cpp
│   │   │   ├── RequestHandler.hpp
│   │   │   ├── RequestHandler.cpp
│   │   │   └── http_test.cpp
│   │   └── metrics/         # Metrics collection
│   │       ├── MetricsModule.hpp
│   │       ├── MetricsModule.cpp
│   │       ├── MetricsCollector.hpp
│   │       ├── MetricsCollector.cpp
│   │       └── metrics_test.cpp
│   ├── interfaces/          # Public APIs and interfaces
│   │   ├── api/
│   │   │   ├── AuthAPI.hpp
│   │   │   ├── DatabaseAPI.hpp
│   │   │   └── MetricsAPI.hpp
│   │   └── protocols/
│   │       ├── HttpProtocol.hpp
│   │       └── TcpProtocol.hpp
│   ├── tests/               # C++ Unit and integration tests
│   │   ├── unit/
│   │   │   ├── core_unit_test.cpp
│   │   │   └── modules_unit_test.cpp
│   │   ├── integration/
│   │   │   ├── auth_integration_test.cpp
│   │   │   ├── database_integration_test.cpp
│   │   │   └── full_system_test.cpp
│   │   └── performance/
│   │       ├── load_test.cpp
│   │       └── memory_test.cpp
│   └── main.cpp             # Application entry point
├── test/                     # Shell test scripts
│   ├── unit/
│   │   ├── config_test.sh
│   │   ├── logging_test.sh
│   │   ├── network_test.sh
│   │   └── utils_test.sh
│   ├── integration/
│   │   ├── auth_integration_test.sh
│   │   ├── database_integration_test.sh
│   │   ├── http_integration_test.sh
│   │   └── full_system_test.sh
│   ├── performance/
│   │   ├── load_test.sh
│   │   ├── stress_test.sh
│   │   ├── memory_test.sh
│   │   └── benchmark_test.sh
│   ├── regression/
│   │   ├── regression_test.sh
│   │   ├── compatibility_test.sh
│   │   └── stability_test.sh
│   ├── pipeline/
│   │   ├── build_test.sh
│   │   ├── deploy_test.sh
│   │   ├── release_test.sh
│   │   └── ci_test.sh
│   ├── logs/                 # Test execution logs
│   │   ├── unit/
│   │   ├── integration/
│   │   ├── performance/
│   │   └── regression/
│   └── tmp/                  # Temporary test files
├── lib/                      # Third-party libraries
│   ├── json.hpp
│   └── README.md
├── doc/                      # Documentation
│   ├── api/                 # API documentation
│   │   ├── core/
│   │   │   ├── config.md
│   │   │   ├── logging.md
│   │   │   └── networking.md
│   │   └── modules/
│   │       ├── auth.md
│   │       ├── database.md
│   │       ├── http.md
│   │       └── metrics.md
│   ├── design/              # Design documents
│   │   ├── architecture.md
│   │   ├── data_flow.md
│   │   ├── security.md
│   │   └── performance.md
│   ├── plan/                # Project planning
│   │   ├── roadmap.md
│   │   ├── milestones.md
│   │   └── features/
│   ├── rules/               # Coding rules
│   │   ├── rules.md
│   │   └── conventions.md
│   └── README.md
├── scripts/                  # Build and utility scripts
│   ├── build/
│   │   ├── build.sh
│   │   ├── clean.sh
│   │   └── install.sh
│   ├── test/
│   │   ├── run_tests.sh
│   │   ├── coverage.sh
│   │   └── benchmark.sh
│   ├── deploy/
│   │   ├── deploy.sh
│   │   ├── rollback.sh
│   │   └── health_check.sh
│   └── utils/
│       ├── lint.sh
│       ├── format.sh
│       └── docs.sh
├── tools/                    # Development tools
│   ├── lint/
│   ├── format/
│   └── docs/
├── logs/                     # Application logs
│   ├── access.log
│   ├── error.log
│   ├── debug.log
│   └── metrics.log
├── config/                   # Configuration files
│   ├── aspire.conf
│   ├── logging.conf
│   └── database.conf
├── build/                    # Build artifacts (gitignored)
├── dist/                     # Distribution files (gitignored)
├── .gitignore
├── Makefile
├── README.md
└── LICENSE
```

### Rule 12.2: File Naming and Organization

**File Naming Conventions:**
- **Header files:** Use `.hpp` extension for C++ headers
- **Source files:** Use `.cpp` extension for C++ implementation
- **Test files:** Use `_test.cpp` suffix for test files
- **Module files:** Use descriptive names that reflect functionality

**Example (Compliant):**
```
aspire/core/config/
├── Config.hpp
├── Config.cpp
├── Config_test.cpp
└── ConfigManager.hpp
```

**Example (Non-Compliant):**
```
aspire/
├── config.hpp        # BAD: No directory structure
├── config.cpp
└── test_config.cpp   # BAD: Inconsistent naming
```

### Rule 12.3: Module Separation and Boundaries

**Module Definition:**
A module is a self-contained unit of functionality with clear boundaries. Each module must have:
- Its own directory under `aspire/modules/`
- A clear, single responsibility
- Minimal dependencies on other modules
- Its own test suite

**Example Module Structure:**
```
aspire/modules/auth/
├── AuthModule.hpp          # Module interface
├── AuthModule.cpp          # Module implementation
├── UserManager.hpp         # User management
├── UserManager.cpp
├── SessionManager.hpp      # Session handling
├── SessionManager.cpp
├── auth_test.cpp           # Module tests
└── README.md              # Module documentation
```

### Rule 12.4: Interface and Implementation Separation

**Header-Only Rule:**
- All public interfaces must be declared in header files
- Implementation details must be in separate `.cpp` files
- Headers must be self-contained and include necessary dependencies

**Example (Compliant):**
```cpp
// Config.hpp
#ifndef ASPIRE_CORE_CONFIG_CONFIG_HPP
#define ASPIRE_CORE_CONFIG_CONFIG_HPP

#include <string>
#include <map>

namespace aspire {
namespace core {
namespace config {

class Config {
public:
    static Config& instance();
    bool load(const std::string& path);
    std::string get(const std::string& key) const;
private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    std::map<std::string, std::string> data_;
};

} // namespace config
} // namespace core
} // namespace aspire

#endif // ASPIRE_CORE_CONFIG_CONFIG_HPP
```

### Rule 12.5: Namespace Organization

**Namespace Hierarchy:**
All code must use the `aspire` namespace with appropriate sub-namespaces reflecting the directory structure.

**Example:**
```cpp
namespace aspire {
namespace core {
namespace config {
    // Configuration-related classes
}

namespace logging {
    // Logging-related classes
}
} // namespace core

namespace modules {
namespace auth {
    // Authentication-related classes
}
} // namespace modules
} // namespace aspire
```

### Rule 12.6: Dependency Management

**Dependency Rules:**
- Core modules may not depend on feature modules
- Feature modules may depend on core modules
- Circular dependencies are strictly forbidden
- External dependencies must be documented and approved

**Dependency Graph (Compliant):**
```
core/ → modules/auth/
core/ → modules/database/
modules/auth/ → modules/database/
```

**Dependency Graph (Non-Compliant):**
```
core/ → modules/auth/ → core/  # BAD: Circular dependency
```

### Rule 12.7: Build System Organization

**Makefile Structure:**
- Each module must have its own build rules
- Dependencies must be explicitly declared
- Build system must support incremental compilation
- Test targets must be separate from production targets

**Example Makefile Structure:**
```makefile
# Core modules
CORE_SOURCES = $(wildcard aspire/core/*/*.cpp)
CORE_OBJECTS = $(CORE_SOURCES:.cpp=.o)

# Feature modules
MODULE_SOURCES = $(wildcard aspire/modules/*/*.cpp)
MODULE_OBJECTS = $(MODULE_SOURCES:.cpp=.o)

# Test targets
TEST_SOURCES = $(wildcard aspire/tests/*.cpp)
TEST_OBJECTS = $(TEST_SOURCES:.cpp=.o)
```

### Rule 12.8: Documentation Structure

**Documentation Requirements:**
- Each module must have a `README.md` file
- API documentation must be in `doc/api/`
- Design documents must be in `doc/design/`
- All documentation must be in English

**Example Documentation Structure:**
```
doc/
├── api/
│   ├── core/
│   │   ├── config.md
│   │   └── logging.md
│   └── modules/
│       ├── auth.md
│       └── database.md
├── design/
│   ├── architecture.md
│   └── data_flow.md
└── README.md
```

### Rule 12.9: Test Organization

**Test Structure:**
- Unit tests must be co-located with source files
- Integration tests must be in `aspire/tests/`
- Test files must follow naming convention `*_test.cpp`
- Each module must have comprehensive test coverage
- Shell test scripts must be in `test/` directory at project root
- All test scripts must be executable and follow naming convention `*_test.sh`

**Example Test Structure:**
```
aspire/core/config/
├── Config.hpp
├── Config.cpp
└── Config_test.cpp

aspire/tests/
├── integration/
│   ├── auth_integration_test.cpp
│   └── database_integration_test.cpp
└── performance/
    └── load_test.cpp

test/                           # Shell test scripts
├── unit/
│   ├── config_test.sh
│   ├── logging_test.sh
│   └── network_test.sh
├── integration/
│   ├── auth_integration_test.sh
│   ├── database_integration_test.sh
│   └── full_system_test.sh
├── performance/
│   ├── load_test.sh
│   ├── stress_test.sh
│   └── memory_test.sh
├── regression/
│   ├── regression_test.sh
│   └── compatibility_test.sh
└── pipeline/
    ├── build_test.sh
    ├── deploy_test.sh
    └── release_test.sh
```

**Shell Test Script Requirements:**
- All shell scripts must have executable permissions (`chmod +x`)
- Scripts must use `#!/bin/bash` shebang
- Scripts must return exit code 0 for success, non-zero for failure
- Scripts must include proper error handling and logging
- Scripts must be idempotent (safe to run multiple times)
- Scripts must clean up after themselves

**Example Shell Test Script:**
```bash
#!/bin/bash

# config_test.sh - Unit test for configuration module
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Test configuration
TEST_NAME="config_test"
LOG_FILE="$PROJECT_ROOT/test/logs/${TEST_NAME}.log"

# Ensure log directory exists
mkdir -p "$(dirname "$LOG_FILE")"

# Logging function
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a "$LOG_FILE"
}

# Cleanup function
cleanup() {
    log "Cleaning up test artifacts..."
    rm -rf "$PROJECT_ROOT/test/tmp/config_test_*"
}

# Set up cleanup trap
trap cleanup EXIT

# Test execution
log "Starting $TEST_NAME"
log "Building test executable..."

cd "$PROJECT_ROOT"
make test_config

if [ $? -eq 0 ]; then
    log "Build successful, running tests..."
    ./test_config
    TEST_EXIT_CODE=$?
    
    if [ $TEST_EXIT_CODE -eq 0 ]; then
        log "All tests passed"
        exit 0
    else
        log "Tests failed with exit code $TEST_EXIT_CODE"
        exit 1
    fi
else
    log "Build failed"
    exit 1
fi
```

### Rule 12.10: Version Control and File Organization

**Git Repository Structure:**
- Each logical component must be in its own directory
- Related files must be grouped together
- Binary files and generated content must be in appropriate directories
- Build artifacts must be excluded from version control

**Example .gitignore Structure:**
```
# Build artifacts
build/
*.o
*.so
*.a

# Generated files
*.log
*.tmp

# IDE files
.vscode/
.idea/
```

### Rule 12.11: Scalability and Growth Management

**Growth Management Rules:**
- When a module exceeds 500 lines, it must be split into sub-modules
- When a directory exceeds 10 files, it must be reorganized into subdirectories
- New features must be implemented as separate modules
- Legacy code must be gradually migrated to the new structure

**Example Growth Management:**
```
# Before: Single large module
aspire/modules/database/
├── DatabaseManager.cpp    # 800 lines - TOO LARGE
└── DatabaseManager.hpp

# After: Split into sub-modules
aspire/modules/database/
├── connection/
│   ├── ConnectionManager.cpp
│   └── ConnectionManager.hpp
├── query/
│   ├── QueryEngine.cpp
│   └── QueryEngine.hpp
└── storage/
    ├── StorageManager.cpp
    └── StorageManager.hpp
```

### Rule 12.12: Code Review and Quality Gates

**Review Requirements:**
- All new modules must be reviewed by at least two senior developers
- Directory structure changes must be approved by project maintainers
- New dependencies must be reviewed and approved
- Documentation must be updated for all structural changes
- All shell test scripts must be reviewed by DevOps team
- Performance tests must be reviewed by performance team

**Quality Gates:**
- No module may exceed 1000 lines without special approval
- No directory may exceed 20 files without reorganization
- All modules must have at least 80% test coverage
- All public APIs must be documented
- All shell scripts must pass linting with shellcheck
- All tests must pass before any merge to main branch

### Rule 12.13: Strict File Organization Enforcement

**MANDATORY COMPLIANCE - NO EXCEPTIONS:**
- Every file must be in its designated directory according to the structure
- No files may be placed in incorrect directories
- All file names must follow the exact naming conventions specified
- No temporary files or build artifacts may be committed to version control
- All directories must contain a README.md file explaining their purpose

**Directory-Specific Requirements:**

**Core Directory (`aspire/core/`):**
- Only fundamental system components allowed
- No business logic or feature-specific code
- Must be completely independent of other modules
- All classes must be thread-safe by default

**Modules Directory (`aspire/modules/`):**
- Each module must be completely self-contained
- No cross-module dependencies without explicit approval
- Each module must have its own build configuration
- Each module must have comprehensive test suite

**Interfaces Directory (`aspire/interfaces/`):**
- Only public API declarations allowed
- No implementation code permitted
- Must be stable and backward-compatible
- Must include complete API documentation

**Tests Directory (`aspire/tests/`):**
- Only C++ test files allowed
- Must be organized by test type (unit, integration, performance)
- Each test file must test exactly one module or component
- All tests must be independent and runnable in isolation

**Shell Tests Directory (`test/`):**
- Only executable shell scripts allowed
- Must be organized by test category
- All scripts must be idempotent and safe to run multiple times
- Must include proper error handling and logging

### Rule 12.14: Strict Naming and Convention Enforcement

**File Naming Requirements:**
- All C++ header files must use `.hpp` extension
- All C++ source files must use `.cpp` extension
- All test files must use `_test.cpp` suffix
- All shell scripts must use `_test.sh` suffix
- All documentation files must use `.md` extension
- All configuration files must use `.conf` or `.json` extension

**Class and Function Naming:**
- All class names must use `UpperCamelCase`
- All function names must use `lower_case_with_underscores`
- All variable names must use `lower_case_with_underscores`
- All constant names must use `UPPER_CASE_WITH_UNDERSCORES`
- All namespace names must use `lower_case_with_underscores`

**Directory Naming:**
- All directory names must use `lower_case_with_underscores`
- No abbreviations or acronyms in directory names
- Directory names must be descriptive and self-explanatory
- No special characters or spaces in directory names

### Rule 12.15: Strict Dependency and Import Management

**Include Guard Requirements:**
- All header files must have unique include guards
- Include guard names must follow pattern: `ASPIRE_[PATH]_[FILENAME]_HPP`
- Include guards must be placed immediately after file header comment

**Include Order Requirements:**
1. System headers (standard library)
2. Third-party library headers
3. Project headers (in dependency order)
4. Local headers

**Dependency Documentation:**
- Each module must have a `dependencies.md` file
- All external dependencies must be documented with version requirements
- All internal dependencies must be documented with rationale
- Dependency changes require approval from architecture team

### Rule 12.16: Build System and Automation Requirements

**Makefile Requirements:**
- Must support incremental compilation
- Must have separate targets for debug and release builds
- Must have separate targets for unit tests and integration tests
- Must support parallel compilation
- Must include proper dependency tracking

**CI/CD Requirements:**
- All shell test scripts must be run in CI pipeline
- All C++ tests must be run in CI pipeline
- Build must fail if any test fails
- Code coverage reports must be generated
- Static analysis must be run on all code

**Automation Scripts:**
- All automation scripts must be in `scripts/` directory
- All scripts must be executable and properly documented
- All scripts must include error handling and logging
- All scripts must be idempotent

### Rule 12.17: Documentation and Knowledge Management

**Documentation Requirements:**
- Every directory must have a README.md file
- Every module must have API documentation
- Every public function must have Doxygen comments
- All design decisions must be documented
- All architectural changes must be documented

**Knowledge Management:**
- All documentation must be in English
- All documentation must be version-controlled
- All documentation must be reviewed and approved
- Documentation must be updated with every code change

### Rule 12.18: Security and Compliance Requirements

**Security Requirements:**
- All shell scripts must validate input parameters
- All file operations must use secure paths
- All network operations must use secure protocols
- All sensitive data must be properly handled

**Compliance Requirements:**
- All code must follow the established coding standards
- All tests must be run before any deployment
- All security vulnerabilities must be addressed immediately
- All compliance violations must be reported and fixed

### Rule 12.19: Performance and Scalability Requirements

**Performance Requirements:**
- All modules must have performance benchmarks
- All critical paths must be optimized
- All memory allocations must be tracked
- All performance regressions must be prevented

**Scalability Requirements:**
- All modules must be designed for horizontal scaling
- All state must be externalized where possible
- All bottlenecks must be identified and addressed
- All scaling limits must be documented

### Rule 12.20: Monitoring and Observability Requirements

**Monitoring Requirements:**
- All modules must include proper logging
- All critical operations must be instrumented
- All error conditions must be logged
- All performance metrics must be collected

**Observability Requirements:**
- All modules must expose health check endpoints
- All modules must provide diagnostic information
- All modules must support debugging modes
- All modules must include proper error reporting

---

## Conclusion

*Aspired Customization*
These rules are mandatory for all code in the Aspire project. They are designed to ensure the highest standards of safety, clarity, and maintainability, in the spirit of the NASA JPL C Coding Standard, but adapted for disciplined, modern C++ development.

## Reference

For the full set of original rules, see:
[NASA JPL C Coding Standard - Official Repository](https://github.com/nasa-jpl) 