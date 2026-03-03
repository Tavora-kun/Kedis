/*
 * 从节点存量同步管理模块
 *
 * 功能：
 * 1. 管理存量同步状态（IDLE -> SYNCING -> READY）
 * 2. 提供积压命令队列（仅主线程访问，无锁）
 * 3. RDMA 线程完成后通过 eventfd 通知主线程
 */

#include "../../include/kvstore.h"
#include "../../include/kvs_rdma_sync.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <string.h>

/* ============================================================================
 * 全局状态
 * ============================================================================ */

/* 同步状态 - 使用 volatile 保证可见性 */
static volatile int g_sync_state = SLAVE_STATE_IDLE;

/* eventfd - 用于 RDMA 线程通知主线程 */
static int g_event_fd = -1;

/* 积压队列 - 仅主线程访问，无锁 */
struct backlog_queue {
    struct backlog_cmd *head;
    struct backlog_cmd *tail;
    uint64_t count;
};
static struct backlog_queue g_backlog = {0};

/* 外部声明 */
extern int rdma_sync_client_start_via_tcp(const char *master_host,
                                          uint16_t master_port,
                                          rdma_engine_type_t engine_type);

/* ============================================================================
 * RDMA 同步线程
 * ============================================================================ */

static void *rdma_sync_thread_fn(void *arg) {
    struct sync_thread_args *args = (struct sync_thread_args *)arg;

    kvs_logInfo("[RDMA Thread] 存量同步线程启动\n");

    /* 执行存量同步（阻塞式） */
    int ret = rdma_sync_client_start_via_tcp(args->master_host,
                                              args->master_port,
                                              ENGINE_COUNT);

    if (ret < 0) {
        kvs_logError("[RDMA Thread] 存量同步失败\n");
    } else {
        kvs_logInfo("[RDMA Thread] 存量同步成功完成\n");
    }

    /* 更新状态 */
    g_sync_state = (ret == 0) ? SLAVE_STATE_READY : SLAVE_STATE_IDLE;

    /* 通知主线程 - 写入 eventfd */
    if (g_event_fd >= 0) {
        uint64_t val = 1;
        if (write(g_event_fd, &val, sizeof(val)) != sizeof(val)) {
            kvs_logError("[RDMA Thread] eventfd 写入失败: %s\n", strerror(errno));
        }
    }

    /* 清理参数 */
    kvs_free(args->master_host);
    kvs_free(args);

    return NULL;
}

/* ============================================================================
 * 公开 API 实现
 * ============================================================================ */

/* 初始化从节点同步系统 */
int slave_sync_init(void) {
    /* 创建 eventfd */
    g_event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (g_event_fd < 0) {
        kvs_logError("slave_sync_init: eventfd 创建失败: %s\n", strerror(errno));
        return -1;
    }

    /* 初始化积压队列 */
    g_backlog.head = NULL;
    g_backlog.tail = NULL;
    g_backlog.count = 0;

    g_sync_state = SLAVE_STATE_IDLE;

    kvs_logInfo("[Slave Sync] 初始化完成，event_fd=%d\n", g_event_fd);
    return g_event_fd;  /* 返回 eventfd，让 proactor 注册 */
}

/* 清理资源 */
void slave_sync_cleanup(void) {
    if (g_event_fd >= 0) {
        close(g_event_fd);
        g_event_fd = -1;
    }

    /* 清空积压队列 */
    slave_sync_clear_backlog();
}

/* 获取 eventfd（供 proactor 注册到 io_uring） */
int slave_sync_get_eventfd(void) {
    return g_event_fd;
}

/* 获取当前状态 */
int slave_sync_get_state(void) {
    return g_sync_state;
}

/* 启动存量同步（创建 RDMA 线程） */
int slave_sync_start(const char *master_host, uint16_t master_port) {
    if (g_sync_state == SLAVE_STATE_SYNCING) {
        kvs_logWarn("[Slave Sync] 存量同步已在进行中\n");
        return -1;
    }

    /* 分配线程参数 */
    struct sync_thread_args *args = kvs_malloc(sizeof(*args));
    if (!args) {
        return -1;
    }

    size_t host_len = strlen(master_host);
    args->master_host = kvs_malloc(host_len + 1);
    if (!args->master_host) {
        kvs_free(args);
        return -1;
    }
    memcpy(args->master_host, master_host, host_len + 1);
    args->master_port = master_port;

    /* 设置状态为同步中 */
    g_sync_state = SLAVE_STATE_SYNCING;

    /* 创建 RDMA 线程 */
    pthread_t thread;
    if (pthread_create(&thread, NULL, rdma_sync_thread_fn, args) != 0) {
        kvs_logError("[Slave Sync] RDMA 线程创建失败\n");
        kvs_free(args->master_host);
        kvs_free(args);
        g_sync_state = SLAVE_STATE_IDLE;
        return -1;
    }

    /* 分离线程，自动回收资源 */
    pthread_detach(thread);

    kvs_logInfo("[Slave Sync] RDMA 同步线程已启动\n");
    return 0;
}

/* ============================================================================
 * 积压队列操作（仅主线程访问）
 * ============================================================================ */

/* 深拷贝 robj 数组 */
static robj *robj_array_dup(int argc, robj *argv) {
    if (argc <= 0 || !argv) return NULL;

    robj *new_argv = kvs_malloc(sizeof(robj) * argc);
    if (!new_argv) return NULL;

    for (int i = 0; i < argc; i++) {
        new_argv[i].len = argv[i].len;
        if (argv[i].len > 0 && argv[i].ptr) {
            new_argv[i].ptr = kvs_malloc(argv[i].len + 1);
            if (new_argv[i].ptr) {
                memcpy(new_argv[i].ptr, argv[i].ptr, argv[i].len);
                new_argv[i].ptr[argv[i].len] = '\0';
            }
        } else {
            new_argv[i].ptr = NULL;
        }
    }

    return new_argv;
}

/* 入队 - 主线程在 SYNCING 状态下调用 */
int slave_sync_enqueue(int argc, robj *argv) {
    if (argc <= 0 || !argv) return -1;

    struct backlog_cmd *cmd = kvs_malloc(sizeof(*cmd));
    if (!cmd) return -1;

    cmd->argc = argc;
    cmd->argv = robj_array_dup(argc, argv);
    if (!cmd->argv) {
        kvs_free(cmd);
        return -1;
    }
    cmd->next = NULL;

    /* 链表尾部插入 - 仅主线程访问，无锁 */
    if (g_backlog.tail) {
        g_backlog.tail->next = cmd;
    } else {
        g_backlog.head = cmd;
    }
    g_backlog.tail = cmd;
    g_backlog.count++;

    kvs_logDebug("[Slave Sync] 命令入队，当前积压: %lu\n", (unsigned long)g_backlog.count);
    return 0;
}

/* 处理积压队列 - 主线程在 RDMA 完成后调用 */
void slave_sync_drain_backlog(msg_handler handler) {
    if (!handler) return;

    kvs_logInfo("[Slave Sync] 开始处理积压队列，共 %lu 条命令\n",
                (unsigned long)g_backlog.count);

    struct backlog_cmd *cmd;
    int processed = 0;

    while ((cmd = g_backlog.head) != NULL) {
        /* 从队列移除 */
        g_backlog.head = cmd->next;
        if (!g_backlog.head) {
            g_backlog.tail = NULL;
        }
        g_backlog.count--;

        /* 创建临时 conn 结构执行命令 */
        struct conn temp_conn = {0};
        temp_conn.argc = cmd->argc;
        /* argv 是固定大小数组，需要逐个元素复制 */
        for (int i = 0; i < cmd->argc && i < MAX_ARGC; i++) {
            temp_conn.argv[i] = cmd->argv[i];
        }
        temp_conn.wbuf = kvs_malloc(RESP_BUF_SIZE);

        if (temp_conn.wbuf) {
            /* 执行命令 */
            handler(&temp_conn);
            kvs_free(temp_conn.wbuf);
        }

        /* 释放命令资源 */
        for (int i = 0; i < cmd->argc; i++) {
            if (cmd->argv[i].ptr) {
                kvs_free(cmd->argv[i].ptr);
            }
        }
        kvs_free(cmd->argv);
        kvs_free(cmd);

        processed++;
    }

    g_backlog.tail = NULL;
    g_backlog.count = 0;

    kvs_logInfo("[Slave Sync] 积压队列处理完成，共处理 %d 条命令\n", processed);
}

/* 清空积压队列（不执行） */
void slave_sync_clear_backlog(void) {
    struct backlog_cmd *cmd = g_backlog.head;
    while (cmd) {
        struct backlog_cmd *next = cmd->next;

        for (int i = 0; i < cmd->argc; i++) {
            if (cmd->argv[i].ptr) {
                kvs_free(cmd->argv[i].ptr);
            }
        }
        kvs_free(cmd->argv);
        kvs_free(cmd);

        cmd = next;
    }

    g_backlog.head = NULL;
    g_backlog.tail = NULL;
    g_backlog.count = 0;
}
