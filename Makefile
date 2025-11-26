CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS :=

# Directories
BUILD_DIR := build/obj
BIN_DIR := bin

SRCS := \
	src/main.cpp \
	src/parser.cpp \
	src/executor.cpp \
	src/builtins.cpp \
	src/prompt.cpp

# Object files go to build/obj/
OBJS := $(SRCS:src/%.cpp=$(BUILD_DIR)/%.o)

TARGET := $(BIN_DIR)/chirshell

.PHONY: all clean directories

all: directories $(TARGET)

directories:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

$(TARGET): $(OBJS) | directories
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.cpp | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)



