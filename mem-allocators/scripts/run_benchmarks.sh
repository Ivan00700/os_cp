#!/bin/bash
# Скрипт запуска бенчмарков для аллокаторов памяти

set -e

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # без цвета

echo -e "${GREEN}=== Memory Allocator Benchmark Runner ===${NC}"
echo ""

# Проверяем, собран ли проект
if [ ! -f "build/benchmark" ]; then
    echo -e "${YELLOW}Building project...${NC}"
    make all
    echo ""
fi

# Создаём каталог результатов, если его нет
mkdir -p results

# Число операций по умолчанию
NUM_OPS=10000

# Разбор аргументов командной строки
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

# Сначала запускаем модульные тесты
echo -e "${GREEN}Running unit tests...${NC}"
./build/test_allocators
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ All tests passed!${NC}"
    echo ""
else
    echo -e "${RED}✗ Tests failed!${NC}"
    exit 1
fi

# Запускаем бенчмарки
echo -e "${GREEN}Running benchmarks with ${NUM_OPS} operations...${NC}"
echo ""

# Запуск для обоих аллокаторов
echo -e "${YELLOW}Benchmarking both allocators...${NC}"
./build/benchmark -n ${NUM_OPS} -o results/benchmark_results.csv

# Запуск по отдельности (для сравнения)
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
