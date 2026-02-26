/*
 * RDMA 示例程序的公共例程头文件。
 * 该文件定义了客户端和服务器共同使用的数据结构、宏定义和函数原型。
 */

#ifndef RDMA_COMMON_H // 如果没有定义 RDMA_COMMON_H
#define RDMA_COMMON_H // 则定义 RDMA_COMMON_H，防止头文件被重复包含

#include <stdio.h>    // 包含标准输入输出库，用于 printf 等
#include <stdlib.h>   // 包含标准库，用于 calloc, free, exit 等内存管理和进程控制
#include <unistd.h>   // 包含 UNIX 标准函数库，用于 getopt, sleep 等
#include <string.h>   // 包含字符串处理库，用于 memset, memcpy, strlen 等
#include <errno.h>    // 包含错误号库，用于获取系统调用的错误原因
#include <getopt.h>   // 包含命令行选项解析库

#include <netdb.h>       // 包含网络数据库操作库，用于 getaddrinfo 地址解析
#include <netinet/in.h>  // 包含互联网协议族定义，如 sockaddr_in
#include <arpa/inet.h>   // 包含互联网操作库，用于 IP 地址转换函数（如 inet_ntoa）
#include <sys/socket.h>  // 包含套接字接口库

#include <rdma/rdma_cma.h>    // 核心：RDMA 连接管理器（Connection Manager）库，处理连接建立和拆除
#include <infiniband/verbs.h> // 核心：RDMA Verbs 库，提供底层数据传输操作 API（如发送、接收、读、写）

/* 错误处理宏定义 */
#define rdma_error(msg, args...) do {\
	fprintf(stderr, "%s : %d : ERROR : "msg, __FILE__, __LINE__, ## args);\
}while(0); // 打印错误发生的文件名、行号以及自定义错误消息到标准错误流

#ifdef ACN_RDMA_DEBUG // 如果定义了调试模式
/* 调试信息打印宏 */
#define debug(msg, args...) do {\
    printf("DEBUG: "msg, ## args);\
}while(0); // 在屏幕上打印调试信息

#else // 如果没有定义调试模式

#define debug(msg, args...) // 宏内容为空，编译时会移除调试代码，提高性能

#endif /* ACN_RDMA_DEBUG */

/* 完成队列 (Completion Queue, CQ) 的最大容量 */
#define CQ_CAPACITY (16) // CQ 用于存放已完成的“工作完成”(Work Completion, WC) 消息
/* 每个工作请求 (Work Request) 支持的最大散集/聚合元素 (SGE) 数量 */
#define MAX_SGE (2) // 一个 SGE (Scatter/Gather Element) 指向一段连续内存，这里允许一次请求涉及 2 段内存
/* 队列对中允许挂起的最大工作请求 (WR) 数量 */
#define MAX_WR (8) // 发送和接收队列的最大深度
/* RDMA 服务器默认监听的 TCP 端口 */
#define DEFAULT_RDMA_PORT (20886) // 客户端连接时若未指定端口则使用此值

/* 
 * 内存缓冲区属性结构体。
 * 使用 __attribute((packed)) 确保编译器不会在字段间添加填充字节。
 * 该结构体用于在网络上交换内存信息，使得一端可以知道另一端的内存地址和密钥。
 */
struct __attribute((packed)) rdma_buffer_attr {
  uint64_t address; // 内存区域的起始虚拟地址
  uint32_t length;  // 内存区域的总长度（字节）
  union stag {      // 内存密钥 (Steering Tag / Key)
	  /* 本地访问时使用的密钥 (Local Key, lkey) */
	  uint32_t local_stag;
	  /* 远程访问时（如 RDMA READ/WRITE）使用的密钥 (Remote Key, rkey) */
	  uint32_t remote_stag;
  }stag; // 这是一个联合体，因为对于同一块内存，发送端和接收端对其视角不同
};

/* 
 * 将给定的目标主机名或 IP 地址解析并填充到 sockaddr 结构体中。
 * 返回 0 表示成功，非 0 表示解析失败。
 */
int get_addr(char *dst, struct sockaddr *addr);

/* 
 * 在控制台上格式化并打印 rdma_buffer_attr 结构体的内容。
 */
void show_rdma_buffer_attr(struct rdma_buffer_attr *attr);

/* 
 * 阻塞式处理 RDMA 连接管理 (CM) 事件。
 * @echannel: 接收事件的通道。
 * @expected_event: 预期的事件类型（如 ADDR_RESOLVED）。
 * @cm_event: 用于存储捕获到的事件对象的指针。
 * 返回 0 表示成功匹配到预期事件。
 */
int process_rdma_cm_event(struct rdma_event_channel *echannel, 
		enum rdma_cm_event_type expected_event,
		struct rdma_cm_event **cm_event);

/* 
 * 分配系统内存并立即将其注册为 RDMA 内存区域 (Memory Region, MR)。
 * MR 是 RDMA 网卡能直接访问的受保护内存。
 * @pd: 保护域 (Protection Domain)，定义了资源访问的上下文。
 * @length: 要分配的字节数。
 * @permission: 访问权限（如 IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ）。
 * 返回注册成功的 MR 结构指针。
 */
struct ibv_mr* rdma_buffer_alloc(struct ibv_pd *pd, 
		uint32_t length, 
		enum ibv_access_flags permission);

/* 
 * 释放由 rdma_buffer_alloc 分配的 MR，并释放底层系统内存。
 * @mr: 要销毁的内存区域指针。
 */
void rdma_buffer_free(struct ibv_mr *mr);

/* 
 * 将一段已经存在的内存地址注册到指定的 PD 中，使其对网卡可见。
 * @pd: 关联的保护域。
 * @addr: 现有内存的起始指针。
 * @length: 内存长度。
 * @permission: 赋予该 MR 的 RDMA 访问权限。
 */
struct ibv_mr *rdma_buffer_register(struct ibv_pd *pd, 
		void *addr, 
		uint32_t length, 
		enum ibv_access_flags permission);

/* 
 * 从网卡上注销指定的 MR，释放网卡端的资源，但不会释放 addr 指向的内存。
 * @mr: 要注销的内存区域指针。
 */
void rdma_buffer_deregister(struct ibv_mr *mr);

/* 
 * 处理数据路径上的工作完成 (Work Completion, WC) 消息。
 * 该函数会阻塞直到有 WC 消息到达完成通道。
 * @comp_channel: 关联的 I/O 完成通道。
 * @wc: 存储捕获到的 WC 消息的数组。
 * @max_wc: 要捕获的最大消息数量。
 * 返回成功捕获的 WC 数量。
 */
int process_work_completion_events(struct ibv_comp_channel *comp_channel, 
		struct ibv_wc *wc, 
		int max_wc);

/* 
 * 打印连接标识符 (cm_id) 的内部详细信息，如关联的设备、端口等。
 */
void show_rdma_cmid(struct rdma_cm_id *id);

#endif /* RDMA_COMMON_H */
