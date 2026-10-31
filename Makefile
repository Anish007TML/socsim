# socsim build
#   make            build the simulator and the tests
#   make test       run the tests
#   make asan       tests with AddressSanitizer + UBSan
#   make demo       run the textbook examples and the mixed workload
CXX      ?= g++
SANITIZE ?=
CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra -Wshadow -Werror
ifneq ($(SANITIZE),)
CXXFLAGS += -fsanitize=$(SANITIZE) -fno-omit-frame-pointer
LDFLAGS  += -fsanitize=$(SANITIZE)
endif

BUILD := build
LIB_SRCS := src/cache.cpp src/tlb.cpp src/memory.cpp src/process.cpp src/scheduler.cpp src/simulator.cpp
LIB_OBJS := $(LIB_SRCS:src/%.cpp=$(BUILD)/%.o)
HEADERS  := $(wildcard src/*.h)

all: $(BUILD)/socsim $(BUILD)/tests

$(BUILD)/%.o: src/%.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/socsim: $(LIB_OBJS) $(BUILD)/main.o
	$(CXX) $^ $(LDFLAGS) -o $@

$(BUILD)/tests: tests/tests.cpp $(LIB_OBJS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -Isrc tests/tests.cpp $(LIB_OBJS) $(LDFLAGS) -o $@

$(BUILD):
	mkdir -p $(BUILD)

test: $(BUILD)/tests
	./$(BUILD)/tests

asan:
	$(MAKE) clean
	$(MAKE) SANITIZE=address,undefined test

demo: $(BUILD)/socsim
	./$(BUILD)/socsim workloads/textbook_fcfs.txt --ideal --cores 1 --cs 0 --sched fcfs --gantt
	@echo
	./$(BUILD)/socsim workloads/textbook_fcfs.txt --ideal --cores 1 --cs 0 --sched rr --quantum 4 --gantt
	@echo
	./$(BUILD)/socsim workloads/mixed.txt

clean:
	rm -rf $(BUILD)

.PHONY: all test asan demo clean
