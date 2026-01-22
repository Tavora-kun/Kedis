#include <stdio.h>
#include <assert.h>
#include "../include/kvs_constants.h"
#include "../kvstore.h"

int main() {
    printf("Testing kvs_constants.h...\n");

    // 验证常量定义
    assert(NETWORK_REACTOR == 0);
    assert(NETWORK_PROACTOR == 1);
    assert(NETWORK_NTYCO == 2);
    
    assert(NETWORK_SELECT == NETWORK_PROACTOR);
    
    assert(ENABLE_MULTI_ENGINE == 1);
    
    assert(ENABLE_ARRAY == 1);
    assert(ENABLE_RBTREE == 1);
    assert(ENABLE_HASH == 1);
    assert(ENABLE_SKIPLIST == 1);
    
    printf("BUFFER_SIZE: %d\n", BUFFER_SIZE);
    assert(BUFFER_SIZE == 1024);
    
    printf("All constants verified successfully!\n");
    return 0;
}
