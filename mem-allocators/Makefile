# Makefile for Memory Allocators Project

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -g -I./include
LDFLAGS = -lm

# Directories
SRC_DIR = src
INCLUDE_DIR = include
TEST_DIR = tests
BENCH_DIR = bench
BUILD_DIR = build
RESULTS_DIR = results

# Source files
SOURCES = $(SRC_DIR)/allocator.c \
          $(SRC_DIR)/segregated_freelist.c \
		  $(SRC_DIR)/buddy_allocator.c

# Object files
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

# Executables
TEST_BIN = $(BUILD_DIR)/test_allocators
BENCH_BIN = $(BUILD_DIR)/benchmark

# Default target
all: dirs $(TEST_BIN) $(BENCH_BIN)

# Create build directories
dirs:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(RESULTS_DIR)

# Build object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build test executable
$(TEST_BIN): $(OBJECTS) $(TEST_DIR)/test_allocators.c
	$(CC) $(CFLAGS) $(OBJECTS) $(TEST_DIR)/test_allocators.c -o $@ $(LDFLAGS)

# Build benchmark executable
$(BENCH_BIN): $(OBJECTS) $(BENCH_DIR)/benchmark.c
	$(CC) $(CFLAGS) $(OBJECTS) $(BENCH_DIR)/benchmark.c -o $@ $(LDFLAGS)

# Run tests
test: $(TEST_BIN)
	@echo "Running unit tests..."
	@./$(TEST_BIN)

# Run benchmarks
bench: $(BENCH_BIN)
	@echo "Running benchmarks..."
	@./$(BENCH_BIN) -o $(RESULTS_DIR)/benchmark_results.csv

# Run benchmarks for specific allocator
bench-segregated: $(BENCH_BIN)
	@echo "Running benchmarks for Segregated Free-List allocator..."
	@./$(BENCH_BIN) -a segregated -o $(RESULTS_DIR)/segregated_results.csv

bench-buddy: $(BENCH_BIN)
	@echo "Running benchmarks for Buddy allocator..."
	@./$(BENCH_BIN) -a buddy -o $(RESULTS_DIR)/buddy_results.csv

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(RESULTS_DIR)/*.csv

# Clean everything including results
distclean: clean
	rm -rf $(RESULTS_DIR)

# Show help
help:
	@echo "Memory Allocators Project - Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  all              - Build all executables (default)"
	@echo "  test             - Build and run unit tests"
	@echo "  bench            - Build and run benchmarks for both allocators"
	@echo "  bench-segregated - Run benchmarks for Segregated Free-List only"
	@echo "  bench-buddy      - Run benchmarks for Buddy (power-of-two) only"
	@echo "  clean            - Remove build artifacts"
	@echo "  distclean        - Remove all build artifacts and results"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make             # Build everything"
	@echo "  make test        # Run unit tests"
	@echo "  make bench       # Run all benchmarks"

.PHONY: all dirs test bench bench-segregated bench-buddy clean distclean help
