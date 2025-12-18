# kp_os_20

## Проект: Аллокаторы памяти

Курсовой проект по операционным системам, реализующий два алгоритма управления памятью на языке C99.

### Обзор

Этот репозиторий содержит полноценную реализацию двух алгоритмов управления динамической памятью:

1. **Segregated Free-List** - аллокатор с сегрегированными списками свободных блоков
2. **Buddy (2^n)** - аллокатор со степенями двойки (блоки размера $2^n$)

Проект включает:
- ✅ Полную реализацию обоих аллокаторов на C99
- ✅ Единый API для работы с аллокаторами
- ✅ Модульные тесты для проверки корректности
- ✅ Набор бенчмарков для оценки производительности
- ✅ Скрипты для автоматизации запуска тестов и бенчмарков
- ✅ Скрипт визуализации результатов (Python)
- ✅ Примеры результатов в формате CSV
- ✅ Makefile для сборки на Linux/Unix
- ✅ Подробная документация на русском языке

### Быстрый старт

```bash
# Клонирование репозитория
git clone https://github.com/andro-kes/kp_os_20.git
cd kp_os_20/mem-allocators

# Сборка проекта
make all

# Запуск тестов
make test

# Запуск бенчмарков
make bench
```

### Структура репозитория

```
kp_os_20/
├── README.md                 # Этот файл
└── mem-allocators/           # Основной проект
    ├── include/              # Заголовочные файлы
    │   ├── allocator.h       # Общий интерфейс
    │   ├── segregated_freelist.h
    │   └── buddy_allocator.h
    ├── src/                  # Реализация аллокаторов
    │   ├── allocator.c
    │   ├── segregated_freelist.c
    │   └── buddy_allocator.c
    ├── tests/                # Модульные тесты
    │   └── test_allocators.c
    ├── bench/                # Бенчмарки
    │   └── benchmark.c
    ├── scripts/              # Вспомогательные скрипты
    │   ├── run_benchmarks.sh
    │   └── plot_results.py
    ├── results/              # Результаты бенчмарков
    │   └── sample_results.csv
    ├── Makefile              # Makefile для сборки
    └── README.md             # Подробная документация
```

### Требования

**Обязательные:**
- GCC с поддержкой C99
- GNU Make
- Linux/Unix операционная система

**Опциональные (для визуализации):**
- Python 3
- matplotlib
- pandas

### Установка зависимостей

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install build-essential gcc make

# Для визуализации (опционально)
sudo apt-get install python3 python3-pip
pip3 install matplotlib pandas
```

#### Fedora/RHEL
```bash
sudo dnf install gcc make

# Для визуализации (опционально)
sudo dnf install python3 python3-pip
pip3 install matplotlib pandas
```

### Использование

#### Сборка проекта

```bash
cd mem-allocators
make all
```

Создаются исполняемые файлы:
- `build/test_allocators` - тесты
- `build/benchmark` - бенчмарки

#### Запуск тестов

```bash
# Через Make
make test

# Или напрямую
./build/test_allocators
```

#### Запуск бенчмарков

```bash
# Автоматический запуск (рекомендуется)
cd scripts
./run_benchmarks.sh

# Или через Make
cd ..
make bench

# Только один аллокатор
make bench-segregated
make bench-buddy

# Ручной запуск с параметрами
./build/benchmark -a segregated -n 50000 -o results/my_results.csv
```

#### Визуализация результатов

```bash
# Построить график для одного файла
python3 scripts/plot_results.py results/benchmark_results.csv

# Сравнение нескольких файлов
python3 scripts/plot_results.py results/*.csv -c -o comparison.png
```

### Описание аллокаторов

#### Segregated Free-List

Аллокатор организует память в виде нескольких списков свободных блоков, где каждый список содержит блоки определенного размера (16, 32, 64, 128, 256, 512, 1024, 2048 байт).

**Преимущества:**
- Быстрое выделение O(1) для стандартных размеров
- Минимальная фрагментация для популярных размеров
- Простая реализация

**Лучше подходит для:**
- Частых выделений/освобождений
- Малых и средних блоков памяти
- Предсказуемых паттернов использования

#### Buddy (2^n)

Аллокатор управляет памятью блоками размером $2^n$. При выделении блок при необходимости делится пополам, при освобождении — сливается с «buddy»-соседом такого же размера.

**Преимущества:**
- Эффективное использование памяти
- Хорошая локальность данных
- Быстрое освобождение через битовую карту

**Лучше подходит для:**
- Множества объектов одного размера
- Операций с хорошей локальностью
- Системного программирования

### API для программистов

```c
#include "allocator.h"

// Создание аллокатора (1 МБ кучи)
allocator_t* alloc = allocator_create(
    ALLOCATOR_SEGREGATED_FREELIST,  // или ALLOCATOR_BUDDY
    backing,
    1024 * 1024
  );

// Выделение памяти
void* ptr = allocator_alloc(alloc, 256);

// Работа с памятью
if (ptr) {
    memset(ptr, 0, 256);
    // ... использование ...
}

// Освобождение памяти
allocator_free(alloc, ptr);

// Уничтожение аллокатора
allocator_destroy(alloc);
```

### Опции командной строки

#### Программа benchmark

```
./build/benchmark [OPTIONS]

Options:
  -a, --allocator <type>   Тип аллокатора: segregated, buddy, all
  -n, --num-ops <number>   Количество операций (по умолчанию: 10000)
  -o, --output <file>      Выходной CSV файл (по умолчанию: stdout)
  -h, --help               Справка
```

#### Скрипт run_benchmarks.sh

```
./scripts/run_benchmarks.sh [OPTIONS]

Options:
  -n, --num-ops <number>   Количество операций (по умолчанию: 10000)
  -h, --help               Справка
```

#### Скрипт plot_results.py

```
python3 scripts/plot_results.py <input_files> [OPTIONS]

Options:
  -o, --output <file>      Выходной файл изображения
  -c, --comparison         Создать сравнительный график из нескольких файлов
```

### Примеры использования

#### Пример 1: Базовое тестирование

```bash
cd mem-allocators
make test
```

#### Пример 2: Сравнение производительности

```bash
# Запуск бенчмарков для обоих аллокаторов
make bench

# Визуализация
python3 scripts/plot_results.py results/benchmark_results.csv
```

#### Пример 3: Детальное тестирование одного аллокатора

```bash
# Тест Segregated Free-List с 100,000 операций
./build/benchmark -a segregated -n 100000 -o results/segregated_100k.csv

# Построение графика
python3 scripts/plot_results.py results/segregated_100k.csv -o segregated_100k.png
```

#### Пример 4: Автоматизированный запуск

```bash
cd scripts
./run_benchmarks.sh -n 50000
# Результаты будут сохранены в results/
```

### Интерпретация результатов

Результаты бенчмарков содержат следующие метрики:

- **Time_us**: Время выполнения в микросекундах
- **Operations**: Количество выполненных операций
- **Ops_per_sec**: Операций в секунду (выше = лучше)

**Типы бенчмарков:**
1. **Sequential** - последовательные alloc/free
2. **Random** - случайные операции разных размеров
3. **Mixed** - смешанные паттерны использования
4. **Stress** - стресс-тест с максимальной нагрузкой

### Результаты (образцы)

| Аллокатор             | Sequential | Random    | Mixed     | Stress    |
|-----------------------|------------|-----------|-----------|-----------|
| SegregatedFreeList    | 398,634    | 218,944   | 223,964   | 294,568   |
| Buddy                 | 328,157    | 191,056   | 178,034   | 253,413   |

*Операций в секунду (больше = лучше)*

### Очистка

```bash
# Удалить скомпилированные файлы
make clean

# Удалить всё включая результаты
make distclean
```

### Устранение проблем

**Проблема:** Ошибки компиляции
```bash
# Проверьте версию GCC
gcc --version

# Должна быть поддержка C99
gcc -std=c99 -v
```

**Проблема:** Segmentation fault при тестах
- Решение: Увеличьте размер кучи в `TEST_HEAP_SIZE` в файле `tests/test_allocators.c`

**Проблема:** Failed allocations в бенчмарках
- Решение: Уменьшите количество операций через `-n` или увеличьте `DEFAULT_HEAP_SIZE` в `bench/benchmark.c`

**Проблема:** Скрипт plot_results.py не работает
```bash
# Установите зависимости
pip3 install --user matplotlib pandas

# Проверьте установку
python3 -c "import matplotlib, pandas; print('OK')"
```

### Разработка и расширение

Проект организован модульно для легкого расширения:

1. **Добавление нового аллокатора:**
   - Создайте файлы `include/my_allocator.h` и `src/my_allocator.c`
   - Реализуйте функции create/destroy/alloc/free
   - Добавьте новый тип в `allocator_type_t` в `allocator.h`
   - Обновите `allocator.c` для поддержки нового типа

2. **Добавление новых тестов:**
   - Отредактируйте `tests/test_allocators.c`
   - Добавьте новую тестовую функцию
   - Вызовите её в `main()`

3. **Добавление новых бенчмарков:**
   - Отредактируйте `bench/benchmark.c`
   - Добавьте новую benchmark функцию
   - Вызовите её в `run_benchmarks()`

### Дополнительные ресурсы

- [Подробная документация проекта](mem-allocators/README.md)
- [Описание Segregated Free Lists](https://en.wikipedia.org/wiki/Free_list)
- [Buddy memory allocation](https://en.wikipedia.org/wiki/Buddy_memory_allocation)

### Лицензия

Проект создан в образовательных целях.

### Авторы

Курсовая работа по операционным системам, 2025.

### Поддержка

При обнаружении ошибок или предложениях по улучшению создавайте issues в репозитории проекта.
