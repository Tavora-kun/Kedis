#ifndef __KVSTORE_H__
#define __KVSTORE_H__
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>

#include "kvs_constants.h"
#include "config.h"

#include "kvs_network.h"
#include "kvs_protocol.h"

#include "kvs_hash.h"
#include "kvs_rbtree.h"
#include "kvs_array.h"
#include "kvs_skiplist.h"

#include "kvs_aof.h"
#include "kvs_ksf.h"


#include "memory_pool.h"
#include "kvs_log.h"


void* kvs_calloc(size_t num, size_t size);
void *kvs_malloc(size_t size);
void kvs_free(void *ptr);

#endif