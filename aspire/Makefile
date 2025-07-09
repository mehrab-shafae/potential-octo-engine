CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -O2 -pipe
LDFLAGS := -luring

# Directory configuration
BUILD_DIR := build
SRC_DIRS := engine
INC_DIRS := .

# Source and object files
SRCS := $(wildcard $(addsuffix /*.cpp, $(SRC_DIRS))) main.cpp
OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRCS))

# Pattern rule for compiling object files
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(addprefix -I, $(INC_DIRS)) -c $< -o $@

# Link final executable
aspire: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) aspire 