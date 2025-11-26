CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -pthread -Iinclude
LDFLAGS := -pthread

SRCS := \
	src/main.cpp \
	src/parser.cpp \
	src/executor.cpp \
	src/builtins.cpp \
	src/jobs.cpp \
	src/signals.cpp \
	src/prompt.cpp

OBJS := $(SRCS:.cpp=.o)

TARGET := chirshell

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJS) $(TARGET)



