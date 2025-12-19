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
    
    # Проверяем наличие обязательных столбцов
    required_cols = ['Allocator', 'Benchmark', 'AllocOpsPerSec', 'FreeOpsPerSec', 'PeakUtilization']
    if not all(col in df.columns for col in required_cols):
        print(f"Error: CSV file must contain columns: {required_cols}")
        return False
    
    # Создаём фигуру с подграфиками
    fig, axs = plt.subplots(1, 3, figsize=(18, 6))
    ax1, ax2, ax3 = axs
    
    # График 1: пропускная способность выделения (операций/сек) по сценариям
    pivot_data = df.pivot(index='Benchmark', columns='Allocator', values='AllocOpsPerSec')
    pivot_data.plot(kind='bar', ax=ax1, rot=45)
    ax1.set_title('Allocation Throughput (ops/sec)', fontsize=14, fontweight='bold')
    ax1.set_xlabel('Benchmark Type', fontsize=12)
    ax1.set_ylabel('Alloc ops/sec', fontsize=12)
    ax1.legend(title='Allocator', fontsize=10)
    ax1.grid(True, alpha=0.3)
    
    # График 2: пропускная способность освобождения (операций/сек) по сценариям
    pivot_free = df.pivot(index='Benchmark', columns='Allocator', values='FreeOpsPerSec')
    pivot_free.plot(kind='bar', ax=ax2, rot=45)
    ax2.set_title('Free Throughput (ops/sec)', fontsize=14, fontweight='bold')
    ax2.set_xlabel('Benchmark Type', fontsize=12)
    ax2.set_ylabel('Free ops/sec', fontsize=12)
    ax2.legend(title='Allocator', fontsize=10)
    ax2.grid(True, alpha=0.3)

    pivot_util = df.pivot(index='Benchmark', columns='Allocator', values='PeakUtilization')
    pivot_util.plot(kind='bar', ax=ax3, rot=45)
    ax3.set_title('Peak Utilization Factor', fontsize=14, fontweight='bold')
    ax3.set_xlabel('Benchmark Type', fontsize=12)
    ax3.set_ylabel('PeakRequested / HeapSize', fontsize=12)
    ax3.legend(title='Allocator', fontsize=10)
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
    
    # Создаём общий сравнительный график
    fig = plt.figure(figsize=(18, 10))
    
    # График 1: средняя скорость выделения
    ax1 = plt.subplot(2, 2, 1)
    pivot_ops = combined.pivot_table(
        index='Benchmark', 
        columns='Allocator', 
        values='AllocOpsPerSec', 
        aggfunc='mean'
    )
    pivot_ops.plot(kind='bar', ax=ax1, rot=45)
    ax1.set_title('Average Allocation Throughput', fontsize=12, fontweight='bold')
    ax1.set_xlabel('Benchmark Type')
    ax1.set_ylabel('Alloc ops/sec')
    ax1.legend(title='Allocator', fontsize=9)
    ax1.grid(True, alpha=0.3)
    
    # График 2: средняя скорость освобождения
    ax2 = plt.subplot(2, 2, 2)
    pivot_time = combined.pivot_table(
        index='Benchmark', 
        columns='Allocator', 
        values='FreeOpsPerSec', 
        aggfunc='mean'
    )
    pivot_time.plot(kind='bar', ax=ax2, rot=45)
    ax2.set_title('Average Free Throughput', fontsize=12, fontweight='bold')
    ax2.set_xlabel('Benchmark Type')
    ax2.set_ylabel('Free ops/sec')
    ax2.legend(title='Allocator', fontsize=9)
    ax2.grid(True, alpha=0.3)
    
    # График 3: утилизация памяти по аллокатору
    ax3 = plt.subplot(2, 2, 3)
    allocator_avg = combined.groupby('Allocator')['PeakUtilization'].mean()
    allocator_avg.plot(kind='bar', ax=ax3, rot=45, color=['#2ca02c', '#d62728'])
    ax3.set_title('Overall Peak Utilization', fontsize=12, fontweight='bold')
    ax3.set_xlabel('Allocator')
    ax3.set_ylabel('Avg PeakUtilization')
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
        summary_text += f"  Avg alloc ops/sec: {avg_alloc:,.0f}\n"
        summary_text += f"  Avg free ops/sec:  {avg_free:,.0f}\n"
        summary_text += f"  Avg peak util:     {avg_util:.4f}\n\n"
    
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
