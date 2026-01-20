#ifndef __KVS_NETWORK_H__
#define __KVS_NETWORK_H__

#include "kvs_constants.h"

// 消息处理回调函数定义
typedef int (*msg_handler)(char *msg, int length, char *response);

// 网络模型启动函数声明
extern int reactor_start(unsigned short port, msg_handler handler);
extern int proactor_start(unsigned short port, msg_handler handler);
extern int ntyco_start(unsigned short port, msg_handler handler);

#endif // __KVS_NETWORK_H__
