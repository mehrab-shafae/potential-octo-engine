# Makefile for Aspire DPDK HTTP Server

CXX = g++
CXXFLAGS = -std=c++20 -O3 -march=native -mtune=native -Wall -Wextra -D_GNU_SOURCE
DPDK_CFLAGS = $(shell pkg-config --cflags libdpdk)
DPDK_LIBS = $(shell pkg-config --libs libdpdk)

TARGET = aspire_dpdk
SOURCE = aspire_dpdk.cpp

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(DPDK_CFLAGS) -o $@ $< $(DPDK_LIBS)

# Clean build artifacts
clean:
	rm -f $(TARGET)

# Install (requires root for DPDK)
install: $(TARGET)
	sudo install -m 755 $(TARGET) /usr/local/bin/

# Run with DPDK EAL arguments
run: $(TARGET)
	sudo ./$(TARGET) --lcores=0-3 --socket-mem=1024,1024 --huge-dir=/mnt/huge

# Run with minimal configuration for testing
run-test: $(TARGET)
	sudo ./$(TARGET) --lcores=0 --socket-mem=512 --huge-dir=/mnt/huge

# Setup huge pages (requires root)
setup-hugepages:
	sudo sh -c 'echo 1024 > /proc/sys/vm/nr_hugepages'
	sudo mkdir -p /mnt/huge
	sudo mount -t hugetlbfs nodev /mnt/huge

# Check DPDK installation
check-dpdk:
	@echo "Checking DPDK installation..."
	@pkg-config --exists libdpdk || (echo "DPDK not found. Please install DPDK first." && exit 1)
	@echo "DPDK found: $(shell pkg-config --modversion libdpdk)"
	@echo "DPDK CFLAGS: $(DPDK_CFLAGS)"
	@echo "DPDK LIBS: $(DPDK_LIBS)"

# Performance test
perf-test: $(TARGET)
	@echo "Running performance test..."
	sudo ./$(TARGET) --lcores=0-3 --socket-mem=1024,1024 --huge-dir=/mnt/huge &
	@sleep 2
	@echo "Testing with wrk..."
	wrk -t4 -c100 -d30s http://localhost:4556/
	@sudo pkill -f $(TARGET)

# Debug build
debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

# Release build
release: CXXFLAGS += -DNDEBUG
release: $(TARGET)

# Check system requirements
check-system:
	@echo "Checking system requirements..."
	@echo "CPU cores: $(shell nproc)"
	@echo "Memory: $(shell free -h | grep Mem | awk '{print $$2}')"
	@echo "Huge pages: $(shell cat /proc/sys/vm/nr_hugepages)"
	@echo "DPDK ports: $(shell dpdk-devbind.py --status | grep -c "drv=vfio-pci")"

# Setup DPDK environment
setup-dpdk: setup-hugepages
	@echo "Setting up DPDK environment..."
	@echo "Please run the following commands manually:"
	@echo "1. Load DPDK drivers: sudo modprobe vfio-pci"
	@echo "2. Bind network interface: sudo dpdk-devbind.py --bind=vfio-pci <interface>"
	@echo "3. Check status: dpdk-devbind.py --status"

# Help target
help:
	@echo "Available targets:"
	@echo "  all          - Build the DPDK HTTP server"
	@echo "  clean        - Remove build artifacts"
	@echo "  install      - Install to /usr/local/bin (requires root)"
	@echo "  run          - Run with performance settings"
	@echo "  run-test     - Run with minimal settings for testing"
	@echo "  setup-hugepages - Setup huge pages (requires root)"
	@echo "  check-dpdk   - Check DPDK installation"
	@echo "  check-system - Check system requirements"
	@echo "  setup-dpdk   - Setup DPDK environment"
	@echo "  perf-test    - Run performance test with wrk"
	@echo "  debug        - Build with debug symbols"
	@echo "  release      - Build optimized release version"
	@echo "  help         - Show this help message"

.PHONY: all clean install run run-test setup-hugepages check-dpdk perf-test debug release check-system setup-dpdk help 