#!/usr/bin/env python3
"""
Скрипт построения графиков по результатам бенчмарков аллокаторов памяти
Требуется: matplotlib, pandas
Установка: pip3 install matplotlib pandas
"""

import sys
import os
import argparse
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib
matplotlib.use('Agg')


def _ordered_benchmarks(series: pd.Series) -> list[str]:
    """Вернуть список сценариев (Benchmark) в стабильном порядке"""

    preferred = ['Sequential', 'Random', 'Mixed', 'Stress']
    unique = list(pd.unique(series.dropna()))
    if all(b in unique for b in preferred):
        return preferred
    return sorted(unique)


def _sanity_check_dataframe(df: pd.DataFrame) -> None:
    """Проверки на типичные проблемы с данными

    Идея: если значения перепутаны при парсинге/выводе CSV, это часто проявляется как
    несоответствие между (AllocOps, AllocTime_us) и AllocOpsPerSec (и аналогично для free)
    Также дополнительно подсвечиваем ситуацию, когда число успешных операций AllocOps
    заметно отличается между аллокаторами в одном сценарии
    """

    # 1) Пересчитываем ops/sec из ops и времени и сверяемся с колонкой
    def _rel_diff(a: float, b: float) -> float:
        denom = max(abs(a), abs(b), 1e-12)
        return abs(a - b) / denom

    for idx, row in df.iterrows():
        try:
            alloc_time_s = float(row['AllocTime_us']) * 1e-6
            free_time_s = float(row['FreeTime_us']) * 1e-6
            alloc_ops = float(row['AllocOps'])
            free_ops = float(row['FreeOps'])

            calc_alloc = (alloc_ops / alloc_time_s) if alloc_time_s > 0 else float('inf')
            calc_free = (free_ops / free_time_s) if free_time_s > 0 else float('inf')
            file_alloc = float(row['AllocOpsPerSec'])
            file_free = float(row['FreeOpsPerSec'])

            if _rel_diff(calc_alloc, file_alloc) > 0.01:
                print(
                    "Warning: AllocOpsPerSec mismatch for "
                    f"Allocator={row['Allocator']} Benchmark={row['Benchmark']}: "
                    f"csv={file_alloc:.2f}, recomputed={calc_alloc:.2f}"
                )
            if _rel_diff(calc_free, file_free) > 0.01:
                print(
                    "Warning: FreeOpsPerSec mismatch for "
                    f"Allocator={row['Allocator']} Benchmark={row['Benchmark']}: "
                    f"csv={file_free:.2f}, recomputed={calc_free:.2f}"
                )

            # 2) Слишком короткие измерения почти всегда дают шумные ops/sec
            if row['Benchmark'] in ['Sequential', 'Random', 'Mixed', 'Stress']:
                if alloc_time_s < 1e-4:  # < 0.1 ms
                    print(
                        "Warning: Very short allocation timing (<0.1ms) for "
                        f"Allocator={row['Allocator']} Benchmark={row['Benchmark']}: "
                        f"AllocTime_us={row['AllocTime_us']}"
                    )
                if free_time_s < 1e-4:
                    print(
                        "Warning: Very short free timing (<0.1ms) for "
                        f"Allocator={row['Allocator']} Benchmark={row['Benchmark']}: "
                        f"FreeTime_us={row['FreeTime_us']}"
                    )
        except Exception:
            # Не мешаем построению графиков из-за проверки
            continue

    # 3) Подсветить различия в количестве успешных операций между аллокаторами в одном сценарии
    if 'Allocator' in df.columns and 'Benchmark' in df.columns and 'AllocOps' in df.columns:
        by_bench = df.groupby('Benchmark')['AllocOps'].nunique(dropna=True)
        diffs = by_bench[by_bench > 1]
        if not diffs.empty:
            for bench in diffs.index.tolist():
                rows = df[df['Benchmark'] == bench][['Allocator', 'AllocOps']]
                details = ", ".join([f"{r['Allocator']}={int(r['AllocOps'])}" for _, r in rows.iterrows()])
                print(
                    "Warning: Different successful AllocOps between allocators for "
                    f"Benchmark={bench}: {details}"
                )

def plot_results(csv_file, output_file=None):
    """Построить графики по результатам бенчмарка из CSV-файла"""
    
    # Проверяем, существует ли файл
    if not os.path.exists(csv_file):
        print(f"Error: File not found: {csv_file}")
        return False
    
    # Читаем CSV
    try:
        df = pd.read_csv(csv_file)
    except Exception as e:
        print(f"Error reading CSV file: {e}")
        return False

    _sanity_check_dataframe(df)
    
    # Проверяем наличие обязательных столбцов
    required_cols = ['Allocator', 'Benchmark', 'AllocOpsPerSec', 'FreeOpsPerSec', 'PeakUtilization']
    if not all(col in df.columns for col in required_cols):
        print(f"Error: CSV file must contain columns: {required_cols}")
        return False
    
    # Создаём фигуру с подграфиками
    fig, axs = plt.subplots(1, 3, figsize=(18, 6))
    ax1, ax2, ax3 = axs

    benchmark_order = _ordered_benchmarks(df['Benchmark'])
    df = df.copy()
    df['Benchmark'] = pd.Categorical(df['Benchmark'], categories=benchmark_order, ordered=True)
    
    # График 1: пропускная способность выделения (операций/сек) по сценариям
    pivot_data = df.pivot(index='Benchmark', columns='Allocator', values='AllocOpsPerSec')
    pivot_data.plot(kind='bar', ax=ax1, rot=45)
    ax1.set_title('Allocation Throughput (ops/sec)', fontsize=14, fontweight='bold')
    ax1.set_xlabel('Benchmark Type', fontsize=12)
    ax1.set_ylabel('ops/sec', fontsize=12)
    ax1.legend(title='Allocator', fontsize=10)
    ax1.grid(True, alpha=0.3)
    
    # График 2: пропускная способность освобождения (операций/сек) по сценариям
    pivot_free = df.pivot(index='Benchmark', columns='Allocator', values='FreeOpsPerSec')
    pivot_free.plot(kind='bar', ax=ax2, rot=45)
    ax2.set_title('Deallocation Throughput (ops/sec)', fontsize=14, fontweight='bold')
    ax2.set_xlabel('Benchmark Type', fontsize=12)
    ax2.set_ylabel('ops/sec', fontsize=12)
    ax2.legend(title='Allocator', fontsize=10)
    ax2.grid(True, alpha=0.3)

    # График 3: PeakUtilization отдельно по каждому сценарию
    # Требуемый вид: 4 столбца на аллокатор (по сценариям)
    pivot_util = df.pivot_table(index='Allocator', columns='Benchmark', values='PeakUtilization', aggfunc='mean', observed=False)
    pivot_util = pivot_util.reindex(columns=benchmark_order)
    pivot_util.plot(kind='bar', ax=ax3, rot=0)
    ax3.set_title('Peak Memory Utilization (PeakRequested/HeapSize)', fontsize=14, fontweight='bold')
    ax3.set_xlabel('Allocator', fontsize=12)
    ax3.set_ylabel('PeakRequested/HeapSize', fontsize=12)
    ax3.legend(title='Benchmark', fontsize=10)
    ax3.grid(True, alpha=0.3)
    
    # Подгоняем разметку
    plt.tight_layout()
    
    # Сохраняем график
    if output_file:
        plt.savefig(output_file, dpi=300, bbox_inches='tight')
        print(f"Plot saved to: {output_file}")
    else:
        # Генерируем имя по умолчанию
        output_file = csv_file.replace('.csv', '.png')
        plt.savefig(output_file, dpi=300, bbox_inches='tight')
        print(f"Plot saved to: {output_file}")
    
    plt.close()
    return True

def plot_comparison(files, output_file='comparison.png'):
    """Построить сравнительный график по нескольким файлам результатов"""
    
    all_data = []
    for file in files:
        if not os.path.exists(file):
            print(f"Warning: File not found: {file}")
            continue
        try:
            df = pd.read_csv(file)
            all_data.append(df)
        except Exception as e:
            print(f"Warning: Error reading {file}: {e}")
    
    if not all_data:
        print("Error: No valid data files")
        return False
    
    # Объединяем данные
    combined = pd.concat(all_data, ignore_index=True)

    benchmark_order = _ordered_benchmarks(combined['Benchmark'])
    combined = combined.copy()
    combined['Benchmark'] = pd.Categorical(combined['Benchmark'], categories=benchmark_order, ordered=True)
    
    # Создаём общий сравнительный график
    fig = plt.figure(figsize=(18, 10))
    
    # График 1: средняя скорость выделения
    ax1 = plt.subplot(2, 2, 1)
    pivot_ops = combined.pivot_table(
        index='Benchmark', 
        columns='Allocator', 
        values='AllocOpsPerSec', 
        aggfunc='mean',
        observed=False
    )
    pivot_ops.plot(kind='bar', ax=ax1, rot=45)
    ax1.set_title('Average Allocation Throughput (ops/sec)', fontsize=12, fontweight='bold')
    ax1.set_xlabel('Benchmark Type')
    ax1.set_ylabel('ops/sec')
    ax1.legend(title='Allocator', fontsize=9)
    ax1.grid(True, alpha=0.3)
    
    # График 2: средняя скорость освобождения
    ax2 = plt.subplot(2, 2, 2)
    pivot_time = combined.pivot_table(
        index='Benchmark', 
        columns='Allocator', 
        values='FreeOpsPerSec', 
        aggfunc='mean',
        observed=False
    )
    pivot_time.plot(kind='bar', ax=ax2, rot=45)
    ax2.set_title('Average Deallocation Throughput (ops/sec)', fontsize=12, fontweight='bold')
    ax2.set_xlabel('Benchmark Type')
    ax2.set_ylabel('ops/sec')
    ax2.legend(title='Allocator', fontsize=9)
    ax2.grid(True, alpha=0.3)
    
    # График 3: PeakUtilization по каждому сценарию (4 столбца на аллокатор)
    ax3 = plt.subplot(2, 2, 3)
    pivot_util = combined.pivot_table(
        index='Allocator',
        columns='Benchmark',
        values='PeakUtilization',
        aggfunc='mean',
        observed=False
    )
    pivot_util = pivot_util.reindex(columns=benchmark_order)
    pivot_util.plot(kind='bar', ax=ax3, rot=0)
    ax3.set_title('Peak Memory Utilization by Benchmark (PeakRequested/HeapSize)', fontsize=12, fontweight='bold')
    ax3.set_xlabel('Allocator')
    ax3.set_ylabel('PeakRequested/HeapSize')
    ax3.legend(title='Benchmark', fontsize=9)
    ax3.grid(True, alpha=0.3)
    
    # График 4: краткая сводка
    ax4 = plt.subplot(2, 2, 4)
    ax4.axis('off')
    
    # Считаем сводные показатели
    summary_text = "Summary Statistics\n\n"
    for allocator in combined['Allocator'].unique():
        alloc_data = combined[combined['Allocator'] == allocator]
        avg_alloc = alloc_data['AllocOpsPerSec'].mean()
        avg_free = alloc_data['FreeOpsPerSec'].mean()
        avg_util = alloc_data['PeakUtilization'].mean()
        summary_text += f"{allocator}:\n"
        summary_text += f"  Avg alloc (ops/sec): {avg_alloc:,.0f}\n"
        summary_text += f"  Avg free  (ops/sec): {avg_free:,.0f}\n"
        summary_text += f"  Avg peak util (PeakRequested/HeapSize): {avg_util:.4f}\n\n"
    
    ax4.text(0.1, 0.9, summary_text, transform=ax4.transAxes,
             fontsize=11, verticalalignment='top', family='monospace',
             bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
    
    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Comparison plot saved to: {output_file}")
    plt.close()
    
    return True

def main():
    parser = argparse.ArgumentParser(
        description='Построение графиков по результатам бенчмарков аллокаторов памяти'
    )
    parser.add_argument(
        'input_files', 
        nargs='+', 
        help='CSV-файл(ы) с результатами бенчмарка'
    )
    parser.add_argument(
        '-o', '--output',
        help='Файл изображения на выходе (по умолчанию: как входной, но с расширением .png)'
    )
    parser.add_argument(
        '-c', '--comparison',
        action='store_true',
        help='Построить сравнительный график по нескольким файлам'
    )
    
    args = parser.parse_args()
    
    if args.comparison and len(args.input_files) > 1:
        output = args.output if args.output else 'comparison.png'
        plot_comparison(args.input_files, output)
    elif len(args.input_files) == 1:
        plot_results(args.input_files[0], args.output)
    else:
        print("Error: For single file plotting, provide one input file")
        print("       For comparison, use -c/--comparison flag with multiple files")
        return 1
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
