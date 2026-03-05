#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
引擎性能对比图表生成脚本

根据 run_full_benchmark_suite.sh 的引擎对比测试结果生成图表
测试结果文件命名格式: ${op}_${engine}_${size}B_${key_label}_result.json
例如: SET_A_128B_Medium_50K_result.json

使用方法:
    python3 gen_engine_charts.py <结果目录>

    示例:
    python3 gen_engine_charts.py ./engine_benchmark_results
"""

import json
import os
import sys
from datetime import datetime

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial', 'Helvetica', 'SimHei']
    plt.rcParams['axes.unicode_minus'] = False
    plt.rcParams['font.family'] = 'sans-serif'
except ImportError as e:
    print(f"错误: 缺少必要的Python库 - {e}")
    print("请安装: pip3 install matplotlib numpy")
    sys.exit(1)

# 引擎配置
ENGINES = ['A', 'R', 'H', 'S']
ENGINE_NAMES = {
    'A': 'Array',
    'R': 'RBTree',
    'H': 'Hash',
    'S': 'SkipList'
}

# 配色方案 - 每个引擎一个颜色
ENGINE_COLORS = {
    'A': '#3498db',  # 蓝色
    'R': '#e74c3c',  # 红色
    'H': '#2ecc71',  # 绿色
    'S': '#f39c12'   # 橙色
}

# 数据大小和键空间配置
DATA_SIZES = [128, 512, 1024]
KEY_SPACES = [10000, 50000]
KEY_LABELS = {10000: 'Small_10K', 50000: 'Medium_50K'}


def parse_json(filepath):
    """解析 memtier_benchmark JSON 文件"""
    try:
        with open(filepath, 'r') as f:
            data = json.load(f)

        # 获取 ALL STATS 部分
        all_stats = data.get('ALL STATS', {})

        # 查找命令统计 (可能是 Hsets, Asets, Rsets, Ssets, Hgets, Agets 等)
        command_stats = None
        for key in all_stats.keys():
            if key not in ['Totals', 'Runtime']:
                command_stats = all_stats[key]
                break

        if not command_stats:
            print(f"  警告: 未找到命令统计信息")
            return None

        # 从 Percentile Latencies 获取百分位数据
        percentiles = command_stats.get('Percentile Latencies', {})

        result = {
            'ops_sec': float(command_stats.get('Ops/sec', 0)),
            'latency_avg': float(command_stats.get('Average Latency', command_stats.get('Latency', 0))),
            'p50': float(percentiles.get('p50.00', 0)),
            'p90': float(percentiles.get('p90.00', 0)),
            'p95': float(percentiles.get('p95.00', 0)),
            'p99': float(percentiles.get('p99.00', 0)),
            'p999': float(percentiles.get('p99.90', 0)),
            'count': int(command_stats.get('Count', 0)),
        }

        return result

    except Exception as e:
        print(f"  错误: 解析失败 - {e}")
        return None


def format_number(num):
    """格式化数字显示"""
    if num >= 1000000:
        return f"{num/1000000:.2f}M"
    elif num >= 1000:
        return f"{num/1000:.1f}K"
    return f"{num:.0f}"


def scan_results(result_dir):
    """扫描结果目录，收集所有测试结果"""
    results = {
        'SET': {},
        'GET': {}
    }

    for op in ['SET', 'GET']:
        for engine in ENGINES:
            for size in DATA_SIZES:
                for key_space in KEY_SPACES:
                    key_label = KEY_LABELS[key_space]
                    filename = f"{op}_{engine}_{size}B_{key_label}_result.json"
                    filepath = os.path.join(result_dir, filename)

                    if os.path.exists(filepath):
                        data = parse_json(filepath)
                        if data:
                            if engine not in results[op]:
                                results[op][engine] = {}
                            if size not in results[op][engine]:
                                results[op][engine][size] = {}
                            results[op][engine][size][key_space] = data
                            print(f"  ✓ {filename} - QPS: {data['ops_sec']:.0f}")
                    else:
                        print(f"  ✗ 未找到: {filename}")

    return results


def create_engine_comparison_chart(output_dir, results, operation, metric, title, ylabel, filename, unit=""):
    """创建引擎对比柱状图 (包含所有引擎和排除Array两个版本)"""

    # 第一张图：所有引擎
    fig, axes = plt.subplots(1, 2, figsize=(14, 6), facecolor='white')

    for idx, key_space in enumerate(KEY_SPACES):
        ax = axes[idx]
        ax.set_facecolor('#fafafa')

        x = np.arange(len(DATA_SIZES))
        width = 0.2

        # 为每个引擎绘制柱状图
        for i, engine in enumerate(ENGINES):
            values = []
            for size in DATA_SIZES:
                if engine in results[operation] and size in results[operation][engine] and key_space in results[operation][engine][size]:
                    values.append(results[operation][engine][size][key_space][metric])
                else:
                    values.append(0)

            bars = ax.bar(x + (i - 1.5) * width, values, width,
                         label=ENGINE_NAMES[engine],
                         color=ENGINE_COLORS[engine],
                         edgecolor='white', linewidth=1.5)

            # 添加数值标签
            for bar, val in zip(bars, values):
                if val > 0:
                    height = bar.get_height()
                    if unit == "":
                        text = format_number(val)
                    else:
                        text = f"{val:.2f}{unit}"
                    ax.text(bar.get_x() + bar.get_width()/2., height + height*0.02,
                           text, ha='center', va='bottom', fontsize=8, fontweight='bold')

        # 设置标签
        ax.set_xlabel('Data Size', fontsize=12, fontweight='bold')
        ax.set_ylabel(ylabel, fontsize=12, fontweight='bold')
        ax.set_title(f'{KEY_LABELS[key_space]} Keys', fontsize=13, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels([f'{s}B' for s in DATA_SIZES], fontsize=11)
        ax.legend(loc='upper right', fontsize=10)
        ax.grid(axis='y', alpha=0.3, linestyle='--')
        ax.set_axisbelow(True)
        ax.spines['top'].set_visible(False)
        ax.spines['right'].set_visible(False)

        # 调整Y轴范围
        max_val = max([results[operation][e][s][key_space][metric]
                      for e in ENGINES if e in results[operation]
                      for s in DATA_SIZES if s in results[operation][e] and key_space in results[operation][e][s]]
                      + [1]) * 1.2
        ax.set_ylim(0, max_val)

    fig.suptitle(f'{operation} Operation - {title}', fontsize=16, fontweight='bold', y=1.02)
    plt.tight_layout()

    filepath = os.path.join(output_dir, filename)
    plt.savefig(filepath, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()

    print(f"  ✓ {filepath}")

    # 第二张图：排除 Array 引擎（更清晰展示其他引擎差异）
    engines_excl_array = ['R', 'H', 'S']
    fig, axes = plt.subplots(1, 2, figsize=(14, 6), facecolor='white')

    for idx, key_space in enumerate(KEY_SPACES):
        ax = axes[idx]
        ax.set_facecolor('#fafafa')

        x = np.arange(len(DATA_SIZES))
        width = 0.25

        for i, engine in enumerate(engines_excl_array):
            values = []
            for size in DATA_SIZES:
                if engine in results[operation] and size in results[operation][engine] and key_space in results[operation][engine][size]:
                    values.append(results[operation][engine][size][key_space][metric])
                else:
                    values.append(0)

            bars = ax.bar(x + (i - 1) * width, values, width,
                         label=ENGINE_NAMES[engine],
                         color=ENGINE_COLORS[engine],
                         edgecolor='white', linewidth=1.5)

            # 添加数值标签
            for bar, val in zip(bars, values):
                if val > 0:
                    height = bar.get_height()
                    if unit == "":
                        text = format_number(val)
                    else:
                        text = f"{val:.2f}{unit}"
                    ax.text(bar.get_x() + bar.get_width()/2., height + height*0.02,
                           text, ha='center', va='bottom', fontsize=9, fontweight='bold')

        ax.set_xlabel('Data Size', fontsize=12, fontweight='bold')
        ax.set_ylabel(ylabel, fontsize=12, fontweight='bold')
        ax.set_title(f'{KEY_LABELS[key_space]} Keys', fontsize=13, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels([f'{s}B' for s in DATA_SIZES], fontsize=11)
        ax.legend(loc='upper right', fontsize=10)
        ax.grid(axis='y', alpha=0.3, linestyle='--')
        ax.set_axisbelow(True)
        ax.spines['top'].set_visible(False)
        ax.spines['right'].set_visible(False)

        max_val = max([results[operation][e][s][key_space][metric]
                      for e in engines_excl_array if e in results[operation]
                      for s in DATA_SIZES if s in results[operation][e] and key_space in results[operation][e][s]]
                      + [1]) * 1.15
        ax.set_ylim(0, max_val)

    fig.suptitle(f'{operation} Operation - {title} (RBTree/Hash/SkipList)', fontsize=16, fontweight='bold', y=1.02)
    plt.tight_layout()

    # 修改文件名添加 _detail 后缀
    base_name = filename.replace('.png', '_detail.png')
    filepath = os.path.join(output_dir, base_name)
    plt.savefig(filepath, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()

    print(f"  ✓ {filepath}")
    return filepath


def create_latency_percentiles_chart(output_dir, results, operation):
    """创建延迟百分位数对比图 (分两张图：一张全部引擎，一张排除Array)"""

    # 只使用实际存在的百分位数 (JSON中没有p90和p95)
    percentiles = ['p50', 'p99', 'p999']
    pct_colors = ['#1abc9c', '#e74c3c', '#9b59b6']

    # 第一张图：所有引擎
    fig, axes = plt.subplots(2, 2, figsize=(16, 12), facecolor='white')

    for size_idx, size in enumerate(DATA_SIZES[:2]):  # 只展示前两种数据大小
        for key_idx, key_space in enumerate(KEY_SPACES):
            ax = axes[size_idx][key_idx]
            ax.set_facecolor('#fafafa')

            x = np.arange(len(ENGINES))
            width = 0.25

            for i, pct in enumerate(percentiles):
                values = []
                for engine in ENGINES:
                    if (engine in results[operation] and
                        size in results[operation][engine] and
                        key_space in results[operation][engine][size]):
                        values.append(results[operation][engine][size][key_space][pct])
                    else:
                        values.append(0)

                bars = ax.bar(x + (i - 1) * width, values, width,
                             label=pct.upper(), color=pct_colors[i],
                             edgecolor='white', linewidth=1)

            # 设置标签
            ax.set_xlabel('Engine', fontsize=11, fontweight='bold')
            ax.set_ylabel('Latency (ms)', fontsize=11, fontweight='bold')
            ax.set_title(f'{size}B - {KEY_LABELS[key_space]}', fontsize=12, fontweight='bold')
            ax.set_xticks(x)
            ax.set_xticklabels([ENGINE_NAMES[e] for e in ENGINES], fontsize=10)
            ax.legend(loc='upper left', fontsize=9)
            ax.grid(axis='y', alpha=0.3, linestyle='--')
            ax.set_axisbelow(True)
            ax.spines['top'].set_visible(False)
            ax.spines['right'].set_visible(False)

    fig.suptitle(f'{operation} Operation - Latency Percentiles (All Engines)',
                 fontsize=16, fontweight='bold', y=0.98)
    plt.tight_layout(rect=[0, 0, 1, 0.96])

    filepath = os.path.join(output_dir, f'{operation.lower()}_latency_percentiles_all.png')
    plt.savefig(filepath, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f"  ✓ {filepath}")

    # 第二张图：排除 Array 引擎（更清晰的对比）
    engines_excl_array = ['R', 'H', 'S']

    fig, axes = plt.subplots(2, 2, figsize=(16, 12), facecolor='white')

    for size_idx, size in enumerate(DATA_SIZES[:2]):
        for key_idx, key_space in enumerate(KEY_SPACES):
            ax = axes[size_idx][key_idx]
            ax.set_facecolor('#fafafa')

            x = np.arange(len(engines_excl_array))
            width = 0.25

            for i, pct in enumerate(percentiles):
                values = []
                for engine in engines_excl_array:
                    if (engine in results[operation] and
                        size in results[operation][engine] and
                        key_space in results[operation][engine][size]):
                        values.append(results[operation][engine][size][key_space][pct])
                    else:
                        values.append(0)

                bars = ax.bar(x + (i - 1) * width, values, width,
                             label=pct.upper(), color=pct_colors[i],
                             edgecolor='white', linewidth=1)

                # 添加数值标签
                for bar, val in zip(bars, values):
                    if val > 0:
                        height = bar.get_height()
                        ax.text(bar.get_x() + bar.get_width()/2., height + height*0.02,
                               f'{val:.2f}', ha='center', va='bottom', fontsize=7)

            # 设置标签
            ax.set_xlabel('Engine', fontsize=11, fontweight='bold')
            ax.set_ylabel('Latency (ms)', fontsize=11, fontweight='bold')
            ax.set_title(f'{size}B - {KEY_LABELS[key_space]}', fontsize=12, fontweight='bold')
            ax.set_xticks(x)
            ax.set_xticklabels([ENGINE_NAMES[e] for e in engines_excl_array], fontsize=10)
            ax.legend(loc='upper left', fontsize=9)
            ax.grid(axis='y', alpha=0.3, linestyle='--')
            ax.set_axisbelow(True)
            ax.spines['top'].set_visible(False)
            ax.spines['right'].set_visible(False)

            # 调整Y轴范围，留出标签空间
            max_val = max([results[operation][e][size][key_space][pct]
                          for e in engines_excl_array if e in results[operation]
                          and size in results[operation][e] and key_space in results[operation][e][size]
                          for pct in percentiles] + [1]) * 1.15
            ax.set_ylim(0, max_val)

    fig.suptitle(f'{operation} Operation - Latency Percentiles (RBTree/Hash/SkipList)',
                 fontsize=16, fontweight='bold', y=0.98)
    plt.tight_layout(rect=[0, 0, 1, 0.96])

    filepath = os.path.join(output_dir, f'{operation.lower()}_latency_percentiles_detail.png')
    plt.savefig(filepath, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()
    print(f"  ✓ {filepath}")

    return filepath


def create_engine_radar_chart(output_dir, results):
    """创建引擎综合性能雷达图"""

    # 选择参考配置 (128B, 50K keys)
    ref_size = 128
    ref_key_space = 50000

    fig, axes = plt.subplots(1, 2, figsize=(14, 7), facecolor='white',
                             subplot_kw=dict(projection='polar'))

    for op_idx, operation in enumerate(['SET', 'GET']):
        ax = axes[op_idx]

        # 指标: QPS, 1/Avg Latency, 1/P99, 1/P999 (取倒数使得越大越好)
        categories = ['Throughput\n(QPS)', 'Low Avg\nLatency', 'Low P99\nLatency', 'Low P999\nLatency']
        num_vars = len(categories)

        # 计算角度
        angles = [n / float(num_vars) * 2 * np.pi for n in range(num_vars)]
        angles += angles[:1]  # 闭合

        # 归一化数据
        max_qps = 1
        max_avg = 1
        max_p99 = 1
        max_p999 = 1

        for engine in ENGINES:
            if (engine in results[operation] and
                ref_size in results[operation][engine] and
                ref_key_space in results[operation][engine][ref_size]):
                data = results[operation][engine][ref_size][ref_key_space]
                max_qps = max(max_qps, data['ops_sec'])
                max_avg = max(max_avg, 1.0 / (data['latency_avg'] + 0.001))
                max_p99 = max(max_p99, 1.0 / (data['p99'] + 0.001))
                max_p999 = max(max_p999, 1.0 / (data['p999'] + 0.001))

        # 绘制每个引擎
        for engine in ENGINES:
            if (engine in results[operation] and
                ref_size in results[operation][engine] and
                ref_key_space in results[operation][engine][ref_size]):
                data = results[operation][engine][ref_size][ref_key_space]

                values = [
                    data['ops_sec'] / max_qps,
                    (1.0 / (data['latency_avg'] + 0.001)) / max_avg,
                    (1.0 / (data['p99'] + 0.001)) / max_p99,
                    (1.0 / (data['p999'] + 0.001)) / max_p999
                ]
                values += values[:1]  # 闭合

                ax.plot(angles, values, 'o-', linewidth=2,
                       label=ENGINE_NAMES[engine], color=ENGINE_COLORS[engine])
                ax.fill(angles, values, alpha=0.15, color=ENGINE_COLORS[engine])

        ax.set_xticks(angles[:-1])
        ax.set_xticklabels(categories, fontsize=10)
        ax.set_ylim(0, 1)
        ax.set_title(f'{operation} Performance Profile\n(128B, 50K keys)',
                    fontsize=13, fontweight='bold', pad=20)
        ax.legend(loc='upper right', bbox_to_anchor=(1.3, 1.0), fontsize=10)
        ax.grid(True)

    plt.tight_layout()

    filepath = os.path.join(output_dir, 'engine_radar_chart.png')
    plt.savefig(filepath, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()

    print(f"  ✓ {filepath}")
    return filepath


def create_summary_table(output_dir, results):
    """创建汇总表格图"""

    # 选择参考配置
    ref_size = 128
    ref_key_space = 50000

    fig, axes = plt.subplots(1, 2, figsize=(16, 8), facecolor='white')

    for op_idx, operation in enumerate(['SET', 'GET']):
        ax = axes[op_idx]
        ax.axis('off')

        # 准备数据 (只包含实际存在的百分位数)
        headers = ['Engine', 'QPS (ops/sec)', 'Avg Latency', 'P50 (ms)', 'P99 (ms)', 'P99.9 (ms)']

        table_data = []
        for engine in ENGINES:
            if (engine in results[operation] and
                ref_size in results[operation][engine] and
                ref_key_space in results[operation][engine][ref_size]):
                data = results[operation][engine][ref_size][ref_key_space]
                table_data.append([
                    ENGINE_NAMES[engine],
                    f"{data['ops_sec']:,.0f}",
                    f"{data['latency_avg']:.2f} ms",
                    f"{data['p50']:.2f}",
                    f"{data['p99']:.2f}",
                    f"{data['p999']:.2f}"
                ])

        if table_data:
            table = ax.table(
                cellText=table_data,
                colLabels=headers,
                cellLoc='center',
                loc='center',
                colWidths=[0.15, 0.18, 0.16, 0.12, 0.12, 0.15]
            )

            table.auto_set_font_size(False)
            table.set_fontsize(10)
            table.scale(1, 2.5)

            # 表头样式
            for i in range(len(headers)):
                cell = table[(0, i)]
                cell.set_facecolor('#2c3e50')
                cell.set_text_props(weight='bold', color='white', fontsize=11)

            # 数据行样式 - 每行使用引擎颜色
            for i in range(1, len(table_data) + 1):
                engine_idx = i - 1
                engine = ENGINES[engine_idx]
                for j in range(len(headers)):
                    cell = table[(i, j)]
                    if i % 2 == 0:
                        cell.set_facecolor('#ecf0f1')
                    else:
                        cell.set_facecolor('white')

                    # 高亮QPS列
                    if j == 1:
                        cell.set_text_props(weight='bold', color=ENGINE_COLORS[engine])

            ax.set_title(f'{operation} Performance Summary (128B, 50K keys)',
                        fontsize=14, fontweight='bold', pad=20)

    plt.tight_layout()

    filepath = os.path.join(output_dir, 'engine_summary_table.png')
    plt.savefig(filepath, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()

    print(f"  ✓ {filepath}")
    return filepath


def main():
    if len(sys.argv) < 2:
        print("用法: python3 gen_engine_charts.py <结果目录>")
        print("示例: python3 gen_engine_charts.py ./engine_benchmark_results")
        sys.exit(1)

    result_dir = sys.argv[1]

    if not os.path.isdir(result_dir):
        print(f"错误: 目录不存在: {result_dir}")
        sys.exit(1)

    # 创建输出目录
    charts_dir = os.path.join(result_dir, 'charts')
    os.makedirs(charts_dir, exist_ok=True)

    print("=" * 60)
    print("引擎性能对比图表生成")
    print("=" * 60)
    print(f"\n扫描目录: {result_dir}\n")

    # 扫描结果
    results = scan_results(result_dir)

    if not results['SET'] and not results['GET']:
        print("\n错误: 未找到任何有效结果文件")
        sys.exit(1)

    print(f"\n开始生成图表...\n")

    # 生成 SET 操作图表
    if results['SET']:
        print("生成 SET 操作图表:")
        create_engine_comparison_chart(charts_dir, results, 'SET', 'ops_sec',
                                      'Throughput', 'Operations Per Second',
                                      'set_throughput_comparison.png')
        create_engine_comparison_chart(charts_dir, results, 'SET', 'latency_avg',
                                      'Average Latency', 'Latency (ms)',
                                      'set_average_latency.png', ' ms')
        create_engine_comparison_chart(charts_dir, results, 'SET', 'p99',
                                      'P99 Latency', 'Latency (ms)',
                                      'set_p99_latency.png', ' ms')
        create_latency_percentiles_chart(charts_dir, results, 'SET')
        print()

    # 生成 GET 操作图表
    if results['GET']:
        print("生成 GET 操作图表:")
        create_engine_comparison_chart(charts_dir, results, 'GET', 'ops_sec',
                                      'Throughput', 'Operations Per Second',
                                      'get_throughput_comparison.png')
        create_engine_comparison_chart(charts_dir, results, 'GET', 'latency_avg',
                                      'Average Latency', 'Latency (ms)',
                                      'get_average_latency.png', ' ms')
        create_engine_comparison_chart(charts_dir, results, 'GET', 'p99',
                                      'P99 Latency', 'Latency (ms)',
                                      'get_p99_latency.png', ' ms')
        create_latency_percentiles_chart(charts_dir, results, 'GET')
        print()

    # 生成综合分析图表
    print("生成综合分析图表:")
    create_engine_radar_chart(charts_dir, results)
    create_summary_table(charts_dir, results)

    print(f"\n" + "=" * 60)
    print("图表生成完成!")
    print("=" * 60)
    print(f"\n输出目录: {charts_dir}/")
    print("  set_throughput_comparison.png          - SET 吞吐量对比 (全部引擎)")
    print("  set_throughput_comparison_detail.png   - SET 吞吐量对比 (排除Array)")
    print("  set_average_latency.png                - SET 平均延迟对比 (全部引擎)")
    print("  set_average_latency_detail.png         - SET 平均延迟对比 (排除Array)")
    print("  set_p99_latency.png                    - SET P99延迟对比 (全部引擎)")
    print("  set_p99_latency_detail.png             - SET P99延迟对比 (排除Array)")
    print("  set_latency_percentiles_all.png        - SET 延迟百分位数 (全部引擎)")
    print("  set_latency_percentiles_detail.png     - SET 延迟百分位数 (排除Array)")
    print("  get_throughput_comparison.png          - GET 吞吐量对比 (全部引擎)")
    print("  get_throughput_comparison_detail.png   - GET 吞吐量对比 (排除Array)")
    print("  get_average_latency.png                - GET 平均延迟对比 (全部引擎)")
    print("  get_average_latency_detail.png         - GET 平均延迟对比 (排除Array)")
    print("  get_p99_latency.png                    - GET P99延迟对比 (全部引擎)")
    print("  get_p99_latency_detail.png             - GET P99延迟对比 (排除Array)")
    print("  get_latency_percentiles_all.png        - GET 延迟百分位数 (全部引擎)")
    print("  get_latency_percentiles_detail.png     - GET 延迟百分位数 (排除Array)")
    print("  engine_radar_chart.png           - 引擎性能雷达图")
    print("  engine_summary_table.png         - 性能汇总表格")
    print()


if __name__ == '__main__':
    main()
