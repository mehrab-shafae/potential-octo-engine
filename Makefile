CXX = c++
CXXFLAGS = -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion
SRC_DIR = aspire
BIN_DIR = build
TARGET = $(BIN_DIR)/aspire

SOURCES = $(wildcard $(SRC_DIR)/*.cpp)

all: merge

merge: $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -flto=auto -std=c++20 -march=native -mtune=generic -pthread -static -s $(SOURCES) -o $(TARGET)
	./pretty.sh

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

.PHONY: all merge
