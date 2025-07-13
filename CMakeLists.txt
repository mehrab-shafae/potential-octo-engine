cmake_minimum_required(VERSION 3.16)
project(aspire_dpdk VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -mtune=native -Wall -Wextra -D_GNU_SOURCE")

# Find DPDK
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

# Include directories
include_directories(${DPDK_INCLUDE_DIRS})

# Link directories
link_directories(${DPDK_LIBRARY_DIRS})

# Add definitions
add_definitions(${DPDK_CFLAGS_OTHER})

# Source files
set(SOURCES
    aspire_dpdk.cpp
)

# Create executable
add_executable(aspire_dpdk ${SOURCES})

# Link libraries
target_link_libraries(aspire_dpdk ${DPDK_LIBRARIES})

# Set RPATH for DPDK libraries
set_target_properties(aspire_dpdk PROPERTIES
    INSTALL_RPATH "${DPDK_LIBRARY_DIRS}"
    BUILD_WITH_INSTALL_RPATH TRUE
)

# Install target
install(TARGETS aspire_dpdk
    RUNTIME DESTINATION bin
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
)

# Custom targets for different build types
add_custom_target(debug
    COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Debug .
    COMMAND ${CMAKE_COMMAND} --build . --config Debug
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building debug version"
)

add_custom_target(release
    COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Release .
    COMMAND ${CMAKE_COMMAND} --build . --config Release
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building release version"
)

# Print configuration info
message(STATUS "DPDK found: ${DPDK_VERSION}")
message(STATUS "DPDK include dirs: ${DPDK_INCLUDE_DIRS}")
message(STATUS "DPDK libraries: ${DPDK_LIBRARIES}")
message(STATUS "DPDK cflags: ${DPDK_CFLAGS_OTHER}")

# Add test target
enable_testing()
add_test(NAME DPDKTest COMMAND aspire_dpdk --help)

# Custom target for running with DPDK
add_custom_target(run-dpdk
    COMMAND sudo ./aspire_dpdk --lcores=0-3 --socket-mem=1024,1024 --huge-dir=/mnt/huge
    DEPENDS aspire_dpdk
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running DPDK server"
)

# Custom target for testing
add_custom_target(test-dpdk
    COMMAND sudo ./aspire_dpdk --lcores=0 --socket-mem=512 --huge-dir=/mnt/huge
    DEPENDS aspire_dpdk
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running DPDK server in test mode"
) 