# 0x08 KVstore 编译与开发指南

这是一个高性能、多引擎、支持持久化及 eBPF 增强功能的 C 语言 Key-Value 存储系统。项目集成了协程网络框架 (NtyCo)、多种存储引擎（Hash, RBTree, SkipList）以及 eBPF 监控/复制扩展。

---

## 🛠 环境准备

在开始编译之前，请确保您的系统中已安装以下依赖：

### 1. 基础构建工具
- **编译器**: `gcc`, `clang` (eBPF 编译必需)
- **构建器**: `make`, `cmake`
- **库依赖**: `libelf-dev`, `zlib1g-dev` (eBPF 组件依赖)

### 2. 开发语言环境
- **C/C++**: 现代标准工具链
- **Rust**: `cargo`, `rustc` (用于编译高性能客户端)
- **Python**: 用于运行自动化测试用例

---

## 🚀 编译步骤

项目采用模块化设计，可以根据需要编译不同组件。

### 1. 编译主 KV 存储服务器
主程序位于项目根目录，支持 Reactor/Proactor 模型及多种存储引擎。

```bash
# 清理并全量编译主程序
make clean
make
```
编译完成后，会在根目录生成 `kvstore` 可执行文件。

### 2. 编译 eBPF 增强模块 (mirror)
`mirror` 是一个独立的 eBPF 程序，用于内核态的数据监控或逻辑增强，拥有自包含的编译链。

```bash
cd mirror
# 1. 预构建依赖库 (libbpf & bpftool)，首次编译必做
make prebuild

# 2. 从当前系统内核生成 vmlinux.h (确保 CO-RE 特性)
make vmlinux

# 3. 编译 eBPF 字节码及用户态加载器
make all
```
编译产物位于 `mirror/src/mirror`。

### 3. 编译 Rust 客户端
如果您需要使用 Rust 编写的高性能客户端：

```bash
cd client
cargo build --release
cargo run -- -P 8888 -i
```

---

## 🧪 运行与测试

### 启动服务器
```bash
# 指定端口 8888 启动, 使用加载 KSF 快照的方式初始化
./kvstore 8888 snap

# 指定端口 9999 启动, 使用加载 AOF 文件的方式初始化
./kvstore 9999 aof
```

### 运行一致性测试
项目提供了 Python 编写的一致性与并发压力测试脚本。

```bash
cd tests
python3 conformance.py # 一致性测试
```

---

## 📂 项目结构说明

| 目录 | 说明 |
| :--- | :--: |
| `src/` | 核心源代码（协议解析、存储引擎、网络模型、持久化） |
| `include/` | 全局头文件定义 |
| `mirror/` | **[NEW]** eBPF 扩展模块，包含独立的 libbpf 构建系统 |
| `NtyCo/` | 集成的协程库，提供高性能 IO 支持 |
| `client/` | Rust 语言编写的官方客户端 |
| `kvs-client/` | 多语言客户端示例 (Go, Java, JS, Python, Rust) |
| `tests/` | 自动化测试套件与性能基准测试 |

---

## 🧹 清理
若需清理所有模块的编译产物：
```bash
make clean          # 清理根目录
cd mirror && make clean  # 清理 eBPF 模块
```

---
*Last Updated: 2026-02-10*