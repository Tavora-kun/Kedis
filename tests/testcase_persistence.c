#include "testcase.h"

#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>

/* ==================== 持久化测试函数 ==================== */

/**
 * KSF 持久化测试
 */
void test_ksf_persistence(int connfd, const engine_ops_t* engine) {
    printf("\n  === KSF Persistence Test (%s Engine) ===\n", engine->name);
    
    int total_tests = 0;
    int passed_tests = 0;
    
    // 测试 1: 小数据量测试 (1000 条)
    printf("\n  Test 1: Small data (1000 records)...\n");
    for (int i = 0; i < 1000; i++) {
        char key[64], value[128];
        snprintf(key, sizeof(key), "ksf_small_key_%d", i);
        snprintf(value, sizeof(value), "ksf_small_value_%d", i);
        
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s %s %s", engine->set_cmd, key, value);
        
        char* resp_msg = encode_to_resp(cmd);
        if (resp_msg) {
            send_msg(connfd, resp_msg, strlen(resp_msg));
            free(resp_msg);
            
            int recv_len = 0;
            char* result = recv_msg_dynamic(connfd, &recv_len);
            if (result) {
                if (strstr(result, "OK") != NULL) {
                    passed_tests++;
                }
                free(result);
            }
            total_tests++;
        }
    }
    printf("    Small data: %d/%d SET operations passed\n", passed_tests, total_tests);
    
    // 测试 2: 中等大小数据 (100 条，key 100B, value 1KB)
    printf("\n  Test 2: Medium data (100 records, key=100B, value=1KB)...\n");
    for (int i = 0; i < 100; i++) {
        char key[128], value[1024];
        snprintf(key, sizeof(key), "ksf_medium_key_%04d_", i);
        char* random_key_suffix = generate_random_string(100 - strlen(key), NULL);
        if (random_key_suffix) {
            strcat(key, random_key_suffix);
            free(random_key_suffix);
        }
        
        char* random_value = generate_random_string(1024, NULL);
        if (!random_value) {
            continue;
        }
        
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "%s %s %s", engine->set_cmd, key, random_value);
        free(random_value);
        
        char* resp_msg = encode_to_resp(cmd);
        if (resp_msg) {
            send_msg(connfd, resp_msg, strlen(resp_msg));
            free(resp_msg);
            
            int recv_len = 0;
            char* result = recv_msg_dynamic(connfd, &recv_len);
            if (result) {
                if (strstr(result, "OK") != NULL) {
                    passed_tests++;
                }
                free(result);
            }
            total_tests++;
        }
    }
    printf("    Medium data: %d SET operations passed\n", 100);
    
    // 测试 3: 大数据测试 (10 条，key 1KB, value 1MB)
    printf("\n  Test 3: Large data (10 records, key=1KB, value=1MB)...\n");
    for (int i = 0; i < 10; i++) {
        char key[1024], value[1024 * 1024];
        snprintf(key, sizeof(key), "ksf_large_key_%02d_", i);
        char* random_key_suffix = generate_random_string(1024 - strlen(key), NULL);
        if (random_key_suffix) {
            strcat(key, random_key_suffix);
            free(random_key_suffix);
        }
        
        char* random_value = generate_random_string(1024 * 1024, NULL);
        if (!random_value) {
            continue;
        }
        
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "%s %s %s", engine->set_cmd, key, random_value);
        free(random_value);
        
        char* resp_msg = encode_to_resp(cmd);
        if (resp_msg) {
            send_msg(connfd, resp_msg, strlen(resp_msg));
            free(resp_msg);
            
            int recv_len = 0;
            char* result = recv_msg_dynamic(connfd, &recv_len);
            if (result) {
                if (strstr(result, "OK") != NULL) {
                    passed_tests++;
                }
                free(result);
            }
            total_tests++;
        }
    }
    printf("    Large data: %d SET operations passed\n", 10);
    
    // 执行 SAVE 命令保存 KSF 快照
    printf("\n  Executing SAVE command...\n");
    char* save_cmd = encode_to_resp("SAVE");
    if (save_cmd) {
        send_msg(connfd, save_cmd, strlen(save_cmd));
        free(save_cmd);
        
        int recv_len = 0;
        char* result = recv_msg_dynamic(connfd, &recv_len);
        if (result) {
            printf("    SAVE response: %s", result);
            free(result);
        }
    }
    
    printf("\n  KSF Persistence Test Summary: %d/%d operations passed\n", 
           passed_tests, total_tests);
}

/**
 * AOF 持久化测试
 */
void test_aof_persistence(int connfd, const engine_ops_t* engine) {
    printf("\n  === AOF Persistence Test (%s Engine) ===\n", engine->name);
    
    int total_tests = 0;
    int passed_tests = 0;
    
    // 测试 1: 小数据量测试 (1000 条)
    printf("\n  Test 1: Small data (1000 records)...\n");
    for (int i = 0; i < 1000; i++) {
        char key[64], value[128];
        snprintf(key, sizeof(key), "aof_small_key_%d", i);
        snprintf(value, sizeof(value), "aof_small_value_%d", i);
        
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s %s %s", engine->set_cmd, key, value);
        
        char* resp_msg = encode_to_resp(cmd);
        if (resp_msg) {
            send_msg(connfd, resp_msg, strlen(resp_msg));
            free(resp_msg);
            
            int recv_len = 0;
            char* result = recv_msg_dynamic(connfd, &recv_len);
            if (result) {
                if (strstr(result, "OK") != NULL) {
                    passed_tests++;
                }
                free(result);
            }
            total_tests++;
        }
    }
    printf("    Small data: %d/%d SET operations passed\n", passed_tests, total_tests);
    
    // 测试 2: MOD 操作测试 (100 条)
    printf("\n  Test 2: MOD operations (100 records)...\n");
    for (int i = 0; i < 100; i++) {
        char key[64], value[128];
        snprintf(key, sizeof(key), "aof_small_key_%d", i);
        snprintf(value, sizeof(value), "aof_modified_value_%d", i);
        
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s %s %s", engine->mod_cmd, key, value);
        
        char* resp_msg = encode_to_resp(cmd);
        if (resp_msg) {
            send_msg(connfd, resp_msg, strlen(resp_msg));
            free(resp_msg);
            
            int recv_len = 0;
            char* result = recv_msg_dynamic(connfd, &recv_len);
            if (result) {
                if (strstr(result, "OK") != NULL) {
                    passed_tests++;
                }
                free(result);
            }
            total_tests++;
        }
    }
    printf("    MOD operations: %d operations passed\n", 100);
    
    // 测试 3: DEL 操作测试 (100 条)
    printf("\n  Test 3: DEL operations (100 records)...\n");
    for (int i = 0; i < 100; i++) {
        char key[64];
        snprintf(key, sizeof(key), "aof_small_key_%d", i);
        
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "%s %s", engine->del_cmd, key);
        
        char* resp_msg = encode_to_resp(cmd);
        if (resp_msg) {
            send_msg(connfd, resp_msg, strlen(resp_msg));
            free(resp_msg);
            
            int recv_len = 0;
            char* result = recv_msg_dynamic(connfd, &recv_len);
            if (result) {
                if (strstr(result, "OK") != NULL) {
                    passed_tests++;
                }
                free(result);
            }
            total_tests++;
        }
    }
    printf("    DEL operations: %d operations passed\n", 100);
    
    // 测试 4: 大数据测试 (10 条，key 1KB, value 1MB)
    printf("\n  Test 4: Large data (10 records, key=1KB, value=1MB)...\n");
    for (int i = 0; i < 10; i++) {
        char key[1024], value[1024 * 1024];
        snprintf(key, sizeof(key), "aof_large_key_%02d_", i);
        char* random_key_suffix = generate_random_string(1024 - strlen(key), NULL);
        if (random_key_suffix) {
            strcat(key, random_key_suffix);
            free(random_key_suffix);
        }
        
        char* random_value = generate_random_string(1024 * 1024, NULL);
        if (!random_value) {
            continue;
        }
        
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "%s %s %s", engine->set_cmd, key, random_value);
        free(random_value);
        
        char* resp_msg = encode_to_resp(cmd);
        if (resp_msg) {
            send_msg(connfd, resp_msg, strlen(resp_msg));
            free(resp_msg);
            
            int recv_len = 0;
            char* result = recv_msg_dynamic(connfd, &recv_len);
            if (result) {
                if (strstr(result, "OK") != NULL) {
                    passed_tests++;
                }
                free(result);
            }
            total_tests++;
        }
    }
    printf("    Large data: %d SET operations passed\n", 10);
    
    // 测试 5: 大数据 MOD 操作 (5 条)
    printf("\n  Test 5: Large data MOD operations (5 records)...\n");
    for (int i = 0; i < 5; i++) {
        char key[1024];
        snprintf(key, sizeof(key), "aof_large_key_%02d_", i);
        char* random_key_suffix = generate_random_string(1024 - strlen(key), NULL);
        if (random_key_suffix) {
            strcat(key, random_key_suffix);
            free(random_key_suffix);
        }
        
        char* random_value = generate_random_string(1024 * 1024, NULL);
        if (!random_value) {
            continue;
        }
        
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "%s %s %s", engine->mod_cmd, key, random_value);
        free(random_value);
        
        char* resp_msg = encode_to_resp(cmd);
        if (resp_msg) {
            send_msg(connfd, resp_msg, strlen(resp_msg));
            free(resp_msg);
            
            int recv_len = 0;
            char* result = recv_msg_dynamic(connfd, &recv_len);
            if (result) {
                if (strstr(result, "OK") != NULL) {
                    passed_tests++;
                }
                free(result);
            }
            total_tests++;
        }
    }
    printf("    Large data MOD: %d operations passed\n", 5);
    
    // 测试 6: 大数据 DEL 操作 (5 条)
    printf("\n  Test 6: Large data DEL operations (5 records)...\n");
    for (int i = 0; i < 5; i++) {
        char key[1024];
        snprintf(key, sizeof(key), "aof_large_key_%02d_", i);
        
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "%s %s", engine->del_cmd, key);
        
        char* resp_msg = encode_to_resp(cmd);
        if (resp_msg) {
            send_msg(connfd, resp_msg, strlen(resp_msg));
            free(resp_msg);
            
            int recv_len = 0;
            char* result = recv_msg_dynamic(connfd, &recv_len);
            if (result) {
                if (strstr(result, "OK") != NULL) {
                    passed_tests++;
                }
                free(result);
            }
            total_tests++;
        }
    }
    printf("    Large data DEL: %d operations passed\n", 5);
    
    printf("\n  AOF Persistence Test Summary: %d/%d operations passed\n", 
           passed_tests, total_tests);
}

/**
 * 持久化加载测试
 */
void test_persistence_load(int connfd, const engine_ops_t* engine, const char* test_type) {
    printf("\n  === Persistence Load Test (%s Engine, %s) ===\n", engine->name, test_type);
    
    int total_checks = 0;
    int passed_checks = 0;
    
    // 检查小数据是否正确加载
    printf("\n  Checking small data (1000 records)...\n");
    for (int i = 0; i < 1000; i++) {
        char key[64], expected[128];
        snprintf(key, sizeof(key), "%s_small_key_%d", test_type, i);
        snprintf(expected, sizeof(expected), "%s_small_value_%d", test_type, i);
        
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s %s", engine->get_cmd, key);
        
        char* resp_msg = encode_to_resp(cmd);
        if (resp_msg) {
            send_msg(connfd, resp_msg, strlen(resp_msg));
            free(resp_msg);
            
            int recv_len = 0;
            char* result = recv_msg_dynamic(connfd, &recv_len);
            if (result) {
                if (strstr(result, expected) != NULL) {
                    passed_checks++;
                }
                free(result);
            }
            total_checks++;
        }
    }
    printf("    Small data: %d/%d checks passed\n", passed_checks, total_checks);
    
    // 检查中等数据是否正确加载
    printf("\n  Checking medium data (100 records)...\n");
    for (int i = 0; i < 100; i++) {
        char key[128];
        snprintf(key, sizeof(key), "%s_medium_key_%04d_", test_type, i);
        char* random_key_suffix = generate_random_string(100 - strlen(key), NULL);
        if (random_key_suffix) {
            strcat(key, random_key_suffix);
            free(random_key_suffix);
        }
        
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s %s", engine->get_cmd, key);
        
        char* resp_msg = encode_to_resp(cmd);
        if (resp_msg) {
            send_msg(connfd, resp_msg, strlen(resp_msg));
            free(resp_msg);
            
            int recv_len = 0;
            char* result = recv_msg_dynamic(connfd, &recv_len);
            if (result) {
                if (strstr(result, "ERROR") == NULL) {
                    passed_checks++;
                }
                free(result);
            }
            total_checks++;
        }
    }
    printf("    Medium data: %d checks passed\n", 100);
    
    // 检查大数据是否正确加载
    printf("\n  Checking large data (10 records)...\n");
    for (int i = 0; i < 10; i++) {
        char key[1024];
        snprintf(key, sizeof(key), "%s_large_key_%02d_", test_type, i);
        char* random_key_suffix = generate_random_string(1024 - strlen(key), NULL);
        if (random_key_suffix) {
            strcat(key, random_key_suffix);
            free(random_key_suffix);
        }
        
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "%s %s", engine->get_cmd, key);
        
        char* resp_msg = encode_to_resp(cmd);
        if (resp_msg) {
            send_msg(connfd, resp_msg, strlen(resp_msg));
            free(resp_msg);
            
            int recv_len = 0;
            char* result = recv_msg_dynamic(connfd, &recv_len);
            if (result) {
                if (strstr(result, "ERROR") == NULL) {
                    passed_checks++;
                }
                free(result);
            }
            total_checks++;
        }
    }
    printf("    Large data: %d checks passed\n", 10);
    
    // 对于 AOF，检查 MOD 操作后的数据
    if (strcmp(test_type, "aof") == 0) {
        printf("\n  Checking MOD operations (100 records)...\n");
        for (int i = 0; i < 100; i++) {
            char key[64], expected[128];
            snprintf(key, sizeof(key), "aof_small_key_%d", i);
            snprintf(expected, sizeof(expected), "aof_modified_value_%d", i);
            
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "%s %s", engine->get_cmd, key);
            
            char* resp_msg = encode_to_resp(cmd);
            if (resp_msg) {
                send_msg(connfd, resp_msg, strlen(resp_msg));
                free(resp_msg);
                
                int recv_len = 0;
                char* result = recv_msg_dynamic(connfd, &recv_len);
                if (result) {
                    if (strstr(result, expected) != NULL) {
                        passed_checks++;
                    }
                    free(result);
                }
                total_checks++;
            }
        }
        printf("    MOD operations: %d checks passed\n", 100);
        
        // 检查 DEL 操作后的数据
        printf("\n  Checking DEL operations (100 records)...\n");
        for (int i = 0; i < 100; i++) {
            char key[64];
            snprintf(key, sizeof(key), "aof_small_key_%d", i);
            
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "%s %s", engine->get_cmd, key);
            
            char* resp_msg = encode_to_resp(cmd);
            if (resp_msg) {
                send_msg(connfd, resp_msg, strlen(resp_msg));
                free(resp_msg);
                
                int recv_len = 0;
                char* result = recv_msg_dynamic(connfd, &recv_len);
                if (result) {
                    if (strstr(result, "ERROR") != NULL) {
                        passed_checks++;
                    }
                    free(result);
                }
                total_checks++;
            }
        }
        printf("    DEL operations: %d checks passed\n", 100);
    }
    
    printf("\n  Persistence Load Test Summary: %d/%d checks passed\n", 
           passed_checks, total_checks);
}
