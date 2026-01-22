#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../include/kvs_protocol.h"
#include "../../kvstore.h"

/* ---------------- 从 proactor.c 迁移过来的 RESP 协议解析逻辑 ---------------- */

void kvs_resp_reset(struct conn* c) {
  // 释放旧参数
  for (int i = 0; i < c->argc; i++) {
    if (c->argv[i].ptr) {
      kvs_free(c->argv[i].ptr);
      c->argv[i].ptr = NULL;
    }
  }

  if (c->seg_buf) {
    kvs_free(c->seg_buf);
    c->seg_buf = NULL;
  }

  c->argc = 0;
  c->multibulk_len = 0;
  c->bulk_len = 0;
  c->seg_used = 0;
  c->resp_state = ST_RESP_HDR;
}

void kvs_resp_free_resources(struct conn* c) {
  // 释放当前正在解析的 buffer
  if (c->seg_buf) {
    kvs_free(c->seg_buf);
    c->seg_buf = NULL;
  }

  // 释放已解析的参数
  for (int i = 0; i < c->argc; i++) {
    if (c->argv[i].ptr) {
      kvs_free(c->argv[i].ptr);
      c->argv[i].ptr = NULL;
    }
  }
  
  // wbuf 是由网络层分配和管理的，这里我们只负责 argv 相关的内存
}

/* --------------  RESP 流式解析：啃掉 data[]，返回是否完成一条完整命令 -------------- */
int kvs_resp_feed(struct conn* c) {
  size_t len = c->r_len;
  char* data = c->frame;
  size_t done = 0;

  // 目标, 把 r_len: 目前收到的数据, 全部处理成 RESP
  while (done < len) {
    switch (c->resp_state) {
      case ST_RESP_HDR: {
        // 期待 *<argc>\r\n
        char* p = data + done;
        char* nl = memchr(p, '\n', len - done);
        if (!nl) return done;  // 还没收全一行

        if (nl <= p || *(nl - 1) != '\r') return -1;  // 格式错误

        char prefix = *p;
        long num = strtol(p + 1, NULL, 10);  // 跳过前缀解析数字

        if (prefix == '*') {
          c->multibulk_len = num; // 找到了想要的值
          c->argc = 0; // 接下来开始解析各个段咯,argc是已经收取的段数量
          if (num <= 0 || num > MAX_ARGC) return -1;
          c->resp_state = ST_RESP_BULK_LEN;  // 接下来期待参数长度
        } else {
          // 如果不是 *, 可能直接是 Inline command? 这里只支持标准 RESP 数组
          return -1;
        }

        done += (nl - p) + 1;  // 跳过这行
        break;
      }
      case ST_RESP_BULK_LEN: {
        // 期待 $<len>\r\n
        char* p = data + done;  // 当前所在位置
        char* nl = memchr(p, '\n', len - done);
        if (!nl) return done;

        if (nl <= p || *(nl - 1) != '\r' || *p != '$') return -1; // 检查是否合法(RESP协议)

        long len_val = strtol(p + 1, NULL, 10);
        c->bulk_len = len_val; // 获取到想要的了!

        if (len_val < 0) {  // NULL Bulk String ($ -1)
          return -1;
        }
        if (len_val > MAX_SEG_SIZE) return -1; // 超过1GB的 Key 或者 Value,不读

        // 分配内存准备接收数据
        c->seg_buf = kvs_malloc(len_val + 1);  // +1 for null terminator
        if (!c->seg_buf) {
          return -1;  // 内存分配失败
        }
        c->seg_buf[len_val] = '\0';
        c->seg_used = 0;

        c->resp_state = ST_RESP_BULK_DATA;
        done += (nl - p) + 1;
        break;
      }
      case ST_RESP_BULK_DATA: {
        // 读取 bulk_len 字节
        size_t want = c->bulk_len - c->seg_used;
        size_t avail = len - done;
        size_t cp = (want < avail) ? want : avail;

        memcpy(c->seg_buf + c->seg_used, data + done, cp);
        c->seg_used += cp;
        done += cp;

        if (c->seg_used == (size_t)c->bulk_len) {
          // 数据读完了，期待 \r\n
          if (done + 2 > len) {
            // 还没收到 \r\n，等待
            return done;
          }

          if (data[done] != '\r' || data[done + 1] != '\n') {
            return -1;
          }
          done += 2;

          // 参数完整，存入 argv
          c->argv[c->argc++] = (robj){c->seg_buf, c->bulk_len};
          c->seg_buf = NULL;  // 权责移交

          if (c->argc == c->multibulk_len) {
            // 所有参数解析完毕

            // 移除已处理数据
            int left = len - done;
            if (left > 0) {
              memmove(c->frame, c->frame + done, left);
            }
            c->r_len = left;

            return PARSE_OK;
          } else {
            // 继续下一个参数
            c->resp_state = ST_RESP_BULK_LEN;
          }
        }
        break;
      }
    }
  }

  // 循环结束（数据耗尽），移除已处理数据
  int left = len - done;
  if (left > 0 && done > 0) {
    memmove(c->frame, c->frame + done, left);
  }
  c->r_len = left;

  return 0;  // 需要更多数据
}
