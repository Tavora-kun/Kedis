#ifndef __KVS_ARRAY_H__
#define __KVS_ARRAY_H__

#include "kvs_constants.h"

#if ENABLE_ARRAY
  #define KVS_ARRAY_SIZE 16384

  // 元素
  typedef struct kvs_array_item {
    char* key;
    char* value;
  } kvs_array_item_t;

  // 整个数组
  typedef struct kvs_array {
    kvs_array_item_t* table;
    int total;
  } kvs_array_t;

  int kvs_array_create(kvs_array_t *inst);
  void kvs_array_destroy(kvs_array_t *inst);

  int kvs_array_set(kvs_array_t *inst, char *key, char *value);
  char* kvs_array_get(kvs_array_t *inst, char *key);
  int kvs_array_del(kvs_array_t *inst, char *key);
  int kvs_array_mod(kvs_array_t *inst, char *key, char *value);
  int kvs_array_exist(kvs_array_t *inst, char *key);

#endif // ENABLE_ARRAY

#endif // __KVS_ARRAY_H__
