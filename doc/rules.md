# Aspire C++ Coding Rules and Principles

## General Language and Professionalism

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
All public methods must return a status indicator (e.g., bool, enum, or error code). Silent failure is forbidden.

**Example:**
```cpp
bool Logger::access_log(const std::string& msg);
```

### Rule 4.2: Error Codes Must Be Checked
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
All identifiers (classes, functions, variables) must have clear, descriptive names. Abbreviations are discouraged unless universally understood.

### Rule 5.2: Doxygen-Style Comments Required
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
Templates, macros (except include guards), and metaprogramming are forbidden in utility classes.

---

## 7. Thread Safety

### Rule 7.1: Public Methods Must Be Thread-Safe
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

All code in the Aspire project must follow these naming conventions:

- **Variables and Functions:** Use `lower_case_with_underscores` for all variable and function names (e.g., `sensor_value`, `read_data()`).
- **Classes and Structs:** Use `UpperCamelCase` for all class and struct names (e.g., `RequestHandler`, `SensorData`).

These conventions are mandatory and help ensure consistency, clarity, and professionalism throughout the codebase.

## Line Length Limit

- No line of code, comment, or documentation shall exceed 80 characters in length. This ensures readability and consistency across all environments and tools.

---

## Review and Tooling

- Each small change (commit or pull request) should have a maximum diff of 20 lines. This encourages incremental, reviewable changes and helps maintain code quality.

## Conclusion

These rules are mandatory for all code in the Aspire project. They are designed to ensure the highest standards of safety, clarity, and maintainability, in the spirit of the NASA JPL C Coding Standard, but adapted for disciplined, modern C++ development. 