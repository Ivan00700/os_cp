#!/bin/bash
# Runner script for memory allocator benchmarks

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Memory Allocator Benchmark Runner ===${NC}"
echo ""

# Check if project is built
if [ ! -f "build/benchmark" ]; then
    echo -e "${YELLOW}Building project...${NC}"
    make all
    echo ""
fi

# Create results directory if it doesn't exist
mkdir -p results

# Default number of operations
NUM_OPS=10000

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -n|--num-ops)
            NUM_OPS="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  -n, --num-ops <number>   Number of operations per benchmark (default: 10000)"
            echo "  -h, --help               Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# Run unit tests first
echo -e "${GREEN}Running unit tests...${NC}"
./build/test_allocators
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ All tests passed!${NC}"
    echo ""
else
    echo -e "${RED}✗ Tests failed!${NC}"
    exit 1
fi

# Run benchmarks
echo -e "${GREEN}Running benchmarks with ${NUM_OPS} operations...${NC}"
echo ""

# Run both allocators
echo -e "${YELLOW}Benchmarking both allocators...${NC}"
./build/benchmark -n ${NUM_OPS} -o results/benchmark_results.csv

# Run individual allocators for comparison
echo ""
echo -e "${YELLOW}Benchmarking Segregated Free-List allocator...${NC}"
./build/benchmark -a segregated -n ${NUM_OPS} -o results/segregated_results.csv

echo ""
echo -e "${YELLOW}Benchmarking Buddy allocator...${NC}"
./build/benchmark -a buddy -n ${NUM_OPS} -o results/buddy_results.csv

echo ""
echo -e "${GREEN}=== Benchmark Complete ===${NC}"
echo ""
echo "Results saved to:"
echo "  - results/benchmark_results.csv"
echo "  - results/segregated_results.csv"
echo "  - results/buddy_results.csv"
echo ""
echo -e "${YELLOW}Tip: Use scripts/plot_results.py to visualize the results${NC}"
