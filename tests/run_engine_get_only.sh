#!/bin/bash

# =============================================================================
# Engine GET 测试脚本 - 仅运行 GET 操作测试，覆盖原有数据
#
# 使用方法:
#   ./run_engine_get_only.sh
# =============================================================================

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
PURPLE='\033[0;35m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_highlight() { echo -e "${CYAN}$1${NC}"; }
log_phase() { echo -e "${PURPLE}$1${NC}"; }

# 启动 kvstore（轻量版，用于单个测试点）
start_kvstore_for_test() {
    # 检查是否已有 kvstore 在运行，如果有则停止
    if pgrep -x "kvstore" > /dev/null; then
        pkill -x "kvstore" 2>/dev/null || true
        sleep 1
    fi

    # 启动 kvstore
    cd ..
    ./kvstore > /dev/null 2>&1 &
    local pid=$!
    cd - > /dev/null

    # 等待 kvstore 就绪（最多10秒）
    local retries=0
    while ! nc -z 127.0.0.1 8888 2>/dev/null; do
        sleep 0.5
        retries=$((retries + 1))
        if [[ $retries -gt 20 ]]; then
            log_error "kvstore 启动超时"
            return 1
        fi
    done

    echo $pid
}

# 停止 kvstore 并清理（轻量版）
stop_kvstore_and_clean() {
    local pid=$1

    # 停止 kvstore
    if [[ -n "$pid" ]] && kill -0 $pid 2>/dev/null; then
        kill $pid 2>/dev/null || true
        wait $pid 2>/dev/null || true
    fi

    # 确保没有残留进程
    pkill -x "kvstore" 2>/dev/null || true
    sleep 0.5

    # 运行 clean.sh 清理数据
    if [[ -x "../clean.sh" ]]; then
        cd ..
        ./clean.sh > /dev/null 2>&1 || true
        cd - > /dev/null
    fi
}

# 显示测试进度
print_progress() {
    local current=$1
    local total=$2
    local label=$3
    local percent=$((current * 100 / total))
    local bar_length=40
    local filled=$((percent * bar_length / 100))
    local empty=$((bar_length - filled))

    printf "\r["
    printf "%${filled}s" | tr ' ' '█'
    printf "%${empty}s" | tr ' ' '░'
    printf "] %3d%% (%d/%d) %s" "$percent" "$current" "$total" "$label"
}

# 主函数
main() {
    local start_time=$(date +%s)

    # 检查 kvstore 可执行文件
    if [[ ! -x "../kvstore" ]]; then
        log_error "未找到 kvstore 可执行文件"
        echo "请先编译: cd .. && make"
        exit 1
    fi

    local engines=("A" "R" "H" "S")
    local engine_names=("Array" "RBTree" "Hash" "SkipList")
    local data_sizes=(128 512 1024)
    local key_spaces=(10000 50000)
    local key_labels=("Small_10K" "Medium_50K")

    local total_tests=$((${#engines[@]} * ${#data_sizes[@]} * ${#key_spaces[@]}))
    local current=0

    echo ""
    log_highlight "╔════════════════════════════════════════════════════════════════╗"
    log_highlight "║         Engine GET 测试 - 覆盖原有数据                         ║"
    log_highlight "╚════════════════════════════════════════════════════════════════╝"
    echo ""

    log_info "总测试点数: $total_tests"
    log_info "引擎: ${engine_names[*]}"
    log_info "数据大小: ${data_sizes[*]} B"
    log_info "键空间: ${key_labels[*]}"
    echo ""
    log_warn "每个测试点使用独立的 kvstore 实例"
    log_warn "将覆盖原有的 GET 测试结果!"
    echo ""

    read -p "按 Enter 开始测试，或 Ctrl+C 取消..."
    echo ""

    log_phase "▶ GET 操作测试"
    echo ""

    for i in "${!engines[@]}"; do
        local engine="${engines[$i]}"
        local engine_name="${engine_names[$i]}"

        log_highlight "  测试引擎: ${engine_name} (${engine})"

        for size in "${data_sizes[@]}"; do
            for k in "${!key_spaces[@]}"; do
                local key_space="${key_spaces[$k]}"
                local key_label="${key_labels[$k]}"

                current=$((current + 1))
                local config_name="GET_${engine}_${size}B_${key_label}"

                print_progress $current $total_tests "${engine} GET ${size}B ${key_label}"
                echo ""

                # 启动 kvstore
                local kvs_pid=$(start_kvstore_for_test)
                if [[ -z "$kvs_pid" ]]; then
                    log_error "启动 kvstore 失败，跳过此测试"
                    continue
                fi

                # 预热写入
                memtier_benchmark \
                    -s 127.0.0.1 -p 8888 \
                    --command="${engine}SET __key__ __data__" \
                    --command-ratio=1 --command-key-pattern=P \
                    -t 4 -c 10 --test-time=5 \
                    -d ${size} \
                    --key-prefix="warm_${engine}_" \
                    --key-minimum=1 --key-maximum=${key_space} \
                    > /dev/null 2>&1 || true

                # GET 测试 - 覆盖原有结果
                memtier_benchmark \
                    -s 127.0.0.1 -p 8888 \
                    --command="${engine}GET __key__" \
                    --command-ratio=1 --command-key-pattern=R \
                    -t 8 -c 50 --test-time=30 \
                    --key-prefix="warm_${engine}_" \
                    --key-minimum=1 --key-maximum=${key_space} \
                    --hide-histogram \
                    --json-out-file="./engine_benchmark_results/${config_name}_result.json" \
                    > /dev/null 2>&1 || true

                # 停止并清理
                stop_kvstore_and_clean "$kvs_pid"
            done
        done
    done

    echo ""
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))

    echo ""
    log_success "GET 测试完成，耗时 $((duration / 60)) 分 $((duration % 60)) 秒"
    echo ""
    log_info "原有 GET 测试结果已被覆盖"
    echo ""
    log_info "重新生成图表:"
    echo "  python3 gen_engine_charts.py ./engine_benchmark_results"
    echo ""
}

main "$@"
