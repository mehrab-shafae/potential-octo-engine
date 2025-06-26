CXX = g++
CXXFLAGS = -Wall -Wextra -Werror -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion -std=c++20
SRC_DIR = aspire
BIN_DIR = build
TARGET = $(BIN_DIR)/aspire

all: merge

merge: $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -static $(SRC_DIR)/main.cpp -o $(TARGET)
	./pretty.sh

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

.PHONY: all merge
