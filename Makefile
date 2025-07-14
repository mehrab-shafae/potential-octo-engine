CXX = g++
CXXFLAGS = -std=c++20 -O2
DPDK_FLAGS = $(shell pkg-config --cflags libdpdk)
DPDK_LIBS = $(shell pkg-config --libs libdpdk)

aspire: aspire.cpp
	$(CXX) $(CXXFLAGS) $(DPDK_FLAGS) aspire.cpp -o aspire $(DPDK_LIBS)

clean:
	rm -f aspire 