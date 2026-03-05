#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
混合负载性能对比图表生成脚本

根据 run_full_benchmark_suite.sh 的混合负载测试结果生成图表
测试结果文件命名格式: ${engine}_${scenario_name}_result.json
例如: A_Balanced_55_result.json, R_Read_Heavy_result.json

场景配置:
    - Write_Heavy: 100% SET, 0% GET
    - Write_Read_82: 80% SET, 20% GET
    - Balanced_55: 50% SET, 50% GET
    - Read_Cache_28: 20% SET, 80% GET
    - Read_Heavy: 0% SET, 100% GET

使用方法:
    python3 gen_mixed_charts.py <结果目录>

    示例:
    python3 gen_mixed_charts.py ./mixed_workload_results
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

# 场景配置
SCENARIOS = [
    'Write_Heavy',
    'Write_Read_82',
    'Balanced_55',
    'Read_Cache_28',
    'Read_Heavy'
]

SCENARIO_LABELS = {
    'Write_Heavy': 'Write Heavy\n(100% SET)',
    'Write_Read_82': 'Write/Read 8:2\n(80% SET, 20% GET)',
    'Balanced_55': 'Balanced\n(50% SET, 50% GET)',
    'Read_Cache_28': 'Read Cache\n(20% SET, 80% GET)',
    'Read_Heavy': 'Read Heavy\n(0% SET, 100% GET)'
}

SCENARIO_RATIOS = {
    'Write_Heavy': (100, 0),
    'Write_Read_82': (80, 20),
    'Balanced_55': (50, 50),
    'Read_Cache_28': (20, 80),
    'Read_Heavy': (0, 100)
}


def parse_json(filepath):
    """解析 memtier_benchmark JSON 文件"""
    try:
        with open(filepath, 'r') as f:
            data = json.load(f)

        # 获取 ALL STATS 部分
        all_stats = data.get('ALL STATS', {})

        # 查找命令统计 (排除 Runtime, Totals 等元数据)
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
    results = {}

    for engine in ENGINES:
        results[engine] = {}
        for scenario in SCENARIOS:
            filename = f"{engine}_{scenario}_result.json"
            filepath = os.path.join(result_dir, filename)

            if os.path.exists(filepath):
                data = parse_json(filepath)
                if data:
                    results[engine][scenario] = data
                    set_ratio, get_ratio = SCENARIO_RATIOS[scenario]
                    print(f"  ✓ {filename} - QPS: {data['ops_sec']:.0f} (SET:{set_ratio}%, GET:{get_ratio}%)")
            else:
                print(f"  ✗ 未找到: {filename}")

    return results


def create_mixed_throughput_comparison(output_dir, results):
    """创建混合负载吞吐量对比图"""

    # 过滤出存在的场景
    available_scenarios = []
    for scenario in SCENARIOS:
        for engine in ENGINES:
            if engine in results and scenario in results[engine]:
                available_scenarios.append(scenario)
                break

    if not available_scenarios:
        print("  ! 没有可用的测试结果")
        return None

    fig, ax = plt.subplots(figsize=(14, 8), facecolor='white')
    ax.set_facecolor('#fafafa')

    x = np.arange(len(available_scenarios))
    width = 0.2

    # 为每个引擎绘制柱状图
    for i, engine in enumerate(ENGINES):
        values = []
        for scenario in available_scenarios:
            if engine in results and scenario in results[engine]:
                values.append(results[engine][scenario]['ops_sec'])
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
                text = format_number(val)
                ax.text(bar.get_x() + bar.get_width()/2., height + height*0.02,
                       text, ha='center', va='bottom', fontsize=9, fontweight='bold',
                       rotation=45)

    # 设置标签
    ax.set_xlabel('Workload Scenario', fontsize=13, fontweight='bold')
    ax.set_ylabel('Throughput (ops/sec)', fontsize=13, fontweight='bold')
    ax.set_title('Mixed Workload - Throughput Comparison by Engine', fontsize=16, fontweight='bold', pad=20)
    ax.set_xticks(x)
    ax.set_xticklabels([SCENARIO_LABELS[s] for s in available_scenarios], fontsize=10)
    ax.legend(loc='upper right', fontsize=11)
    ax.grid(axis='y', alpha=0.3, linestyle='--')
    ax.set_axisbelow(True)
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)

    # 调整Y轴范围
    max_val = max([results[e][s]['ops_sec']
                  for e in ENGINES if e in results
                  for s in available_scenarios if s in results[e]]
                  + [1]) * 1.25
    ax.set_ylim(0, max_val)

    plt.tight_layout()

    filepath = os.path.join(output_dir, 'mixed_throughput_comparison.png')
    plt.savefig(filepath, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()

    print(f"  ✓ {filepath}")
    return filepath


def create_mixed_latency_comparison(output_dir, results):
    """创建混合负载延迟对比图 (全部引擎和排除Array两个版本)"""

    # 过滤出存在的场景
    available_scenarios = []
    for scenario in SCENARIOS:
        for engine in ENGINES:
            if engine in results and scenario in results[engine]:
                available_scenarios.append(scenario)
                break

    if not available_scenarios:
        return None

    # 第一张图：全部引擎
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 7), facecolor='white')

    # 平均延迟对比
    ax1.set_facecolor('#fafafa')
    x = np.arange(len(available_scenarios))
    width = 0.2

    for i, engine in enumerate(ENGINES):
        values = []
        for scenario in available_scenarios:
            if engine in results and scenario in results[engine]:
                values.append(results[engine][scenario]['latency_avg'])
            else:
                values.append(0)

        bars = ax1.bar(x + (i - 1.5) * width, values, width,
                      label=ENGINE_NAMES[engine],
                      color=ENGINE_COLORS[engine],
                      edgecolor='white', linewidth=1.5)

        for bar, val in zip(bars, values):
            if val > 0:
                height = bar.get_height()
                ax1.text(bar.get_x() + bar.get_width()/2., height + height*0.02,
                        f'{val:.2f}', ha='center', va='bottom', fontsize=8)

    ax1.set_xlabel('Workload Scenario', fontsize=12, fontweight='bold')
    ax1.set_ylabel('Average Latency (ms)', fontsize=12, fontweight='bold')
    ax1.set_title('Average Latency', fontsize=14, fontweight='bold')
    ax1.set_xticks(x)
    ax1.set_xticklabels([SCENARIO_LABELS[s].split('\n')[0] for s in available_scenarios], fontsize=9, rotation=15)
    ax1.legend(loc='upper left', fontsize=10)
    ax1.grid(axis='y', alpha=0.3, linestyle='--')
    ax1.set_axisbelow(True)
    ax1.spines['top'].set_visible(False)
    ax1.spines['right'].set_visible(False)

    # P99 延迟对比
    ax2.set_facecolor('#fafafa')

    for i, engine in enumerate(ENGINES):
        values = []
        for scenario in available_scenarios:
            if engine in results and scenario in results[engine]:
                values.append(results[engine][scenario]['p99'])
            else:
                values.append(0)

        bars = ax2.bar(x + (i - 1.5) * width, values, width,
                      label=ENGINE_NAMES[engine],
                      color=ENGINE_COLORS[engine],
                      edgecolor='white', linewidth=1.5)

        for bar, val in zip(bars, values):
            if val > 0:
                height = bar.get_height()
                ax2.text(bar.get_x() + bar.get_width()/2., height + height*0.02,
                        f'{val:.2f}', ha='center', va='bottom', fontsize=8)

    ax2.set_xlabel('Workload Scenario', fontsize=12, fontweight='bold')
    ax2.set_ylabel('P99 Latency (ms)', fontsize=12, fontweight='bold')
    ax2.set_title('P99 Tail Latency', fontsize=14, fontweight='bold')
    ax2.set_xticks(x)
    ax2.set_xticklabels([SCENARIO_LABELS[s].split('\n')[0] for s in available_scenarios], fontsize=9, rotation=15)
    ax2.legend(loc='upper left', fontsize=10)
    ax2.grid(axis='y', alpha=0.3, linestyle='--')
    ax2.set_axisbelow(True)
    ax2.spines['top'].set_visible(False)
    ax2.spines['right'].set_visible(False)

    fig.suptitle('Mixed Workload - Latency Comparison by Engine', fontsize=16, fontweight='bold', y=1.02)
    plt.tight_layout()

    filepath = os.path.join(output_dir, 'mixed_latency_comparison.png')
    plt.savefig(filepath, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()

    print(f"  ✓ {filepath}")

    # 第二张图：排除 Array（更清晰的对比）
    engines_excl_array = ['R', 'H', 'S']
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 7), facecolor='white')

    # 平均延迟对比
    ax1.set_facecolor('#fafafa')
    x = np.arange(len(available_scenarios))
    width = 0.25

    for i, engine in enumerate(engines_excl_array):
        values = []
        for scenario in available_scenarios:
            if engine in results and scenario in results[engine]:
                values.append(results[engine][scenario]['latency_avg'])
            else:
                values.append(0)

        bars = ax1.bar(x + (i - 1) * width, values, width,
                      label=ENGINE_NAMES[engine],
                      color=ENGINE_COLORS[engine],
                      edgecolor='white', linewidth=1.5)

        for bar, val in zip(bars, values):
            if val > 0:
                height = bar.get_height()
                ax1.text(bar.get_x() + bar.get_width()/2., height + height*0.02,
                        f'{val:.2f}', ha='center', va='bottom', fontsize=9)

    ax1.set_xlabel('Workload Scenario', fontsize=12, fontweight='bold')
    ax1.set_ylabel('Average Latency (ms)', fontsize=12, fontweight='bold')
    ax1.set_title('Average Latency', fontsize=14, fontweight='bold')
    ax1.set_xticks(x)
    ax1.set_xticklabels([SCENARIO_LABELS[s].split('\n')[0] for s in available_scenarios], fontsize=9, rotation=15)
    ax1.legend(loc='upper left', fontsize=10)
    ax1.grid(axis='y', alpha=0.3, linestyle='--')
    ax1.set_axisbelow(True)
    ax1.spines['top'].set_visible(False)
    ax1.spines['right'].set_visible(False)

    # P99 延迟对比
    ax2.set_facecolor('#fafafa')

    for i, engine in enumerate(engines_excl_array):
        values = []
        for scenario in available_scenarios:
            if engine in results and scenario in results[engine]:
                values.append(results[engine][scenario]['p99'])
            else:
                values.append(0)

        bars = ax2.bar(x + (i - 1) * width, values, width,
                      label=ENGINE_NAMES[engine],
                      color=ENGINE_COLORS[engine],
                      edgecolor='white', linewidth=1.5)

        for bar, val in zip(bars, values):
            if val > 0:
                height = bar.get_height()
                ax2.text(bar.get_x() + bar.get_width()/2., height + height*0.02,
                        f'{val:.2f}', ha='center', va='bottom', fontsize=9)

    ax2.set_xlabel('Workload Scenario', fontsize=12, fontweight='bold')
    ax2.set_ylabel('P99 Latency (ms)', fontsize=12, fontweight='bold')
    ax2.set_title('P99 Tail Latency', fontsize=14, fontweight='bold')
    ax2.set_xticks(x)
    ax2.set_xticklabels([SCENARIO_LABELS[s].split('\n')[0] for s in available_scenarios], fontsize=9, rotation=15)
    ax2.legend(loc='upper left', fontsize=10)
    ax2.grid(axis='y', alpha=0.3, linestyle='--')
    ax2.set_axisbelow(True)
    ax2.spines['top'].set_visible(False)
    ax2.spines['right'].set_visible(False)

    fig.suptitle('Mixed Workload - Latency Comparison (RBTree/Hash/SkipList)', fontsize=16, fontweight='bold', y=1.02)
    plt.tight_layout()

    filepath = os.path.join(output_dir, 'mixed_latency_comparison_detail.png')
    plt.savefig(filepath, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()

    print(f"  ✓ {filepath}")
    return filepath


def create_workload_trend_chart(output_dir, results):
    """创建工作负载趋势图 (展示不同读写比例下的性能变化)"""

    fig, axes = plt.subplots(2, 2, figsize=(14, 12), facecolor='white')

    # 获取存在的场景并按读写比例排序
    available_scenarios = []
    for scenario in SCENARIOS:
        for engine in ENGINES:
            if engine in results and scenario in results[engine]:
                available_scenarios.append(scenario)
                break

    if len(available_scenarios) < 2:
        print("  ! 场景数量不足，跳过趋势图")
        return None

    # 按读比例排序
    sorted_scenarios = sorted(available_scenarios,
                              key=lambda s: SCENARIO_RATIOS[s][1])  # 按GET比例排序

    x_labels = [f"{SCENARIO_RATIOS[s][0]}%W\n{SCENARIO_RATIOS[s][1]}%R" for s in sorted_scenarios]

    # QPS 趋势
    ax = axes[0, 0]
    ax.set_facecolor('#fafafa')
    for engine in ENGINES:
        values = []
        for scenario in sorted_scenarios:
            if engine in results and scenario in results[engine]:
                values.append(results[engine][scenario]['ops_sec'])
            else:
                values.append(0)
        ax.plot(range(len(sorted_scenarios)), values, 'o-', linewidth=2,
               markersize=8, label=ENGINE_NAMES[engine], color=ENGINE_COLORS[engine])
    ax.set_xlabel('Write/Read Ratio', fontsize=11, fontweight='bold')
    ax.set_ylabel('Throughput (ops/sec)', fontsize=11, fontweight='bold')
    ax.set_title('Throughput Trend', fontsize=13, fontweight='bold')
    ax.set_xticks(range(len(sorted_scenarios)))
    ax.set_xticklabels(x_labels, fontsize=9)
    ax.legend(loc='best', fontsize=10)
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)

    # 平均延迟趋势
    ax = axes[0, 1]
    ax.set_facecolor('#fafafa')
    for engine in ENGINES:
        values = []
        for scenario in sorted_scenarios:
            if engine in results and scenario in results[engine]:
                values.append(results[engine][scenario]['latency_avg'])
            else:
                values.append(0)
        ax.plot(range(len(sorted_scenarios)), values, 'o-', linewidth=2,
               markersize=8, label=ENGINE_NAMES[engine], color=ENGINE_COLORS[engine])
    ax.set_xlabel('Write/Read Ratio', fontsize=11, fontweight='bold')
    ax.set_ylabel('Average Latency (ms)', fontsize=11, fontweight='bold')
    ax.set_title('Average Latency Trend', fontsize=13, fontweight='bold')
    ax.set_xticks(range(len(sorted_scenarios)))
    ax.set_xticklabels(x_labels, fontsize=9)
    ax.legend(loc='best', fontsize=10)
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)

    # P99 延迟趋势
    ax = axes[1, 0]
    ax.set_facecolor('#fafafa')
    for engine in ENGINES:
        values = []
        for scenario in sorted_scenarios:
            if engine in results and scenario in results[engine]:
                values.append(results[engine][scenario]['p99'])
            else:
                values.append(0)
        ax.plot(range(len(sorted_scenarios)), values, 'o-', linewidth=2,
               markersize=8, label=ENGINE_NAMES[engine], color=ENGINE_COLORS[engine])
    ax.set_xlabel('Write/Read Ratio', fontsize=11, fontweight='bold')
    ax.set_ylabel('P99 Latency (ms)', fontsize=11, fontweight='bold')
    ax.set_title('P99 Tail Latency Trend', fontsize=13, fontweight='bold')
    ax.set_xticks(range(len(sorted_scenarios)))
    ax.set_xticklabels(x_labels, fontsize=9)
    ax.legend(loc='best', fontsize=10)
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)

    # P99.9 延迟趋势
    ax = axes[1, 1]
    ax.set_facecolor('#fafafa')
    for engine in ENGINES:
        values = []
        for scenario in sorted_scenarios:
            if engine in results and scenario in results[engine]:
                values.append(results[engine][scenario]['p999'])
            else:
                values.append(0)
        ax.plot(range(len(sorted_scenarios)), values, 'o-', linewidth=2,
               markersize=8, label=ENGINE_NAMES[engine], color=ENGINE_COLORS[engine])
    ax.set_xlabel('Write/Read Ratio', fontsize=11, fontweight='bold')
    ax.set_ylabel('P99.9 Latency (ms)', fontsize=11, fontweight='bold')
    ax.set_title('P99.9 Tail Latency Trend', fontsize=13, fontweight='bold')
    ax.set_xticks(range(len(sorted_scenarios)))
    ax.set_xticklabels(x_labels, fontsize=9)
    ax.legend(loc='best', fontsize=10)
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)

    fig.suptitle('Performance Trends Across Workload Patterns', fontsize=16, fontweight='bold', y=0.98)
    plt.tight_layout(rect=[0, 0, 1, 0.96])

    filepath = os.path.join(output_dir, 'mixed_workload_trends.png')
    plt.savefig(filepath, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()

    print(f"  ✓ {filepath}")
    return filepath


def create_heatmap(output_dir, results):
    """创建引擎-场景性能热力图"""

    # 过滤出存在的场景
    available_scenarios = []
    for scenario in SCENARIOS:
        for engine in ENGINES:
            if engine in results and scenario in results[engine]:
                available_scenarios.append(scenario)
                break

    if not available_scenarios:
        return None

    fig, ax = plt.subplots(figsize=(12, 8), facecolor='white')

    # 构建数据矩阵
    data_matrix = []
    for engine in ENGINES:
        row = []
        for scenario in available_scenarios:
            if engine in results and scenario in results[engine]:
                row.append(results[engine][scenario]['ops_sec'])
            else:
                row.append(0)
        data_matrix.append(row)

    # 归一化到 0-100 用于颜色显示
    max_val = max(max(row) for row in data_matrix) if data_matrix else 1
    normalized_data = [[(v / max_val) * 100 for v in row] for row in data_matrix]

    im = ax.imshow(normalized_data, cmap='YlOrRd', aspect='auto')

    # 设置刻度
    ax.set_xticks(np.arange(len(available_scenarios)))
    ax.set_yticks(np.arange(len(ENGINES)))
    ax.set_xticklabels([SCENARIO_LABELS[s].split('\n')[0] for s in available_scenarios], fontsize=10, rotation=30, ha='right')
    ax.set_yticklabels([ENGINE_NAMES[e] for e in ENGINES], fontsize=11)

    # 在每个单元格中添加数值
    for i in range(len(ENGINES)):
        for j in range(len(available_scenarios)):
            value = data_matrix[i][j]
            text = ax.text(j, i, format_number(value),
                          ha="center", va="center", color="black" if normalized_data[i][j] < 50 else "white",
                          fontsize=11, fontweight='bold')

    ax.set_title('Throughput Heatmap by Engine and Workload\n(ops/sec - normalized color scale)',
                fontsize=14, fontweight='bold', pad=20)
    ax.set_xlabel('Workload Scenario', fontsize=12, fontweight='bold')
    ax.set_ylabel('Storage Engine', fontsize=12, fontweight='bold')

    # 添加颜色条
    cbar = plt.colorbar(im, ax=ax)
    cbar.set_label('Relative Performance (%)', fontsize=11, fontweight='bold')

    plt.tight_layout()

    filepath = os.path.join(output_dir, 'mixed_throughput_heatmap.png')
    plt.savefig(filepath, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()

    print(f"  ✓ {filepath}")
    return filepath


def create_summary_table(output_dir, results):
    """创建混合负载汇总表格图"""

    # 选择 Balanced_55 场景作为代表
    ref_scenario = 'Balanced_55'

    if ref_scenario not in [s for e in ENGINES for s in results.get(e, {})]:
        # 选择第一个可用的场景
        for scenario in SCENARIOS:
            for engine in ENGINES:
                if engine in results and scenario in results[engine]:
                    ref_scenario = scenario
                    break
            else:
                continue
            break

    set_ratio, get_ratio = SCENARIO_RATIOS[ref_scenario]

    fig, ax = plt.subplots(figsize=(14, 8), facecolor='white')
    ax.axis('off')

    # 准备数据 (只包含实际存在的百分位数)
    headers = ['Engine', 'QPS (ops/sec)', 'Avg Latency', 'P50 (ms)', 'P99 (ms)', 'P99.9 (ms)']

    table_data = []
    for engine in ENGINES:
        if engine in results and ref_scenario in results[engine]:
            data = results[engine][ref_scenario]
            table_data.append([
                ENGINE_NAMES[engine],
                f"{data['ops_sec']:,.0f}",
                f"{data['latency_avg']:.2f} ms",
                f"{data['p50']:.2f}",
                f"{data['p99']:.2f}",
                f"{data['p999']:.2f}"
            ])

    if not table_data:
        print("  ! 没有可用的数据生成表格")
        return None

    table = ax.table(
        cellText=table_data,
        colLabels=headers,
        cellLoc='center',
        loc='center',
        colWidths=[0.15, 0.18, 0.16, 0.12, 0.12, 0.15]
    )

    table.auto_set_font_size(False)
    table.set_fontsize(11)
    table.scale(1, 2.5)

    # 表头样式
    for i in range(len(headers)):
        cell = table[(0, i)]
        cell.set_facecolor('#2c3e50')
        cell.set_text_props(weight='bold', color='white', fontsize=12)

    # 数据行样式
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

    scenario_label = SCENARIO_LABELS[ref_scenario].replace('\n', ' ')
    ax.set_title(f'Mixed Workload Performance Summary\n({scenario_label})',
                fontsize=16, fontweight='bold', pad=20)

    plt.tight_layout()

    filepath = os.path.join(output_dir, 'mixed_summary_table.png')
    plt.savefig(filepath, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()

    print(f"  ✓ {filepath}")
    return filepath


def main():
    if len(sys.argv) < 2:
        print("用法: python3 gen_mixed_charts.py <结果目录>")
        print("示例: python3 gen_mixed_charts.py ./mixed_workload_results")
        sys.exit(1)

    result_dir = sys.argv[1]

    if not os.path.isdir(result_dir):
        print(f"错误: 目录不存在: {result_dir}")
        sys.exit(1)

    # 创建输出目录
    charts_dir = os.path.join(result_dir, 'charts')
    os.makedirs(charts_dir, exist_ok=True)

    print("=" * 60)
    print("混合负载性能对比图表生成")
    print("=" * 60)
    print(f"\n扫描目录: {result_dir}\n")

    # 扫描结果
    results = scan_results(result_dir)

    if not any(results[e] for e in ENGINES):
        print("\n错误: 未找到任何有效结果文件")
        sys.exit(1)

    print(f"\n开始生成图表...\n")

    # 生成图表
    print("生成对比图表:")
    create_mixed_throughput_comparison(charts_dir, results)
    create_mixed_latency_comparison(charts_dir, results)
    print()

    print("生成趋势分析图表:")
    create_workload_trend_chart(charts_dir, results)
    create_heatmap(charts_dir, results)
    print()

    print("生成汇总图表:")
    create_summary_table(charts_dir, results)

    print(f"\n" + "=" * 60)
    print("图表生成完成!")
    print("=" * 60)
    print(f"\n输出目录: {charts_dir}/")
    print("  mixed_throughput_comparison.png  - 吞吐量对比")
    print("  mixed_latency_comparison.png     - 延迟对比")
    print("  mixed_workload_trends.png        - 工作负载趋势")
    print("  mixed_throughput_heatmap.png     - 性能热力图")
    print("  mixed_summary_table.png          - 汇总表格")
    print()


if __name__ == '__main__':
    main()
