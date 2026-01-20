#ifndef __KVS_SKIPLIST_H__
#define __KVS_SKIPLIST_H__

#include "kvs_constants.h"

#if ENABLE_SKIPLIST

  #define SKIPLIST_MAX_LEVEL 6

  typedef struct skiplist_node_s {
    char *key;
    char *value;
    struct skiplist_node_s **forward;
  } skiplist_node_t;

  typedef struct skiplist_s {
    int level;
    skiplist_node_t *header;
  } skiplist_t;

  typedef struct skiplist_s kvs_skiplist_t;

  int kvs_skiplist_create(kvs_skiplist_t *inst);
  void kvs_skiplist_destroy(kvs_skiplist_t *inst);
  int kvs_skiplist_set(kvs_skiplist_t *inst, char *key, char *value);
  char* kvs_skiplist_get(kvs_skiplist_t *inst, char *key);
  int kvs_skiplist_del(kvs_skiplist_t *inst, char *key);
  int kvs_skiplist_mod(kvs_skiplist_t *inst, char *key, char *value);
  int kvs_skiplist_exist(kvs_skiplist_t *inst, char *key);

#endif // ENABLE_SKIPLIST

#endif // __KVS_SKIPLIST_H__
