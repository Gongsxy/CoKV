#ifndef __ARRAY_H__
#define __ARRAY_H__

#include "common.h"

#define KVS_ARRAY_INIT_CAP   1024

/*
 * 節點一次 malloc（與 hashnode_t 相同）：
 *   [kvs_arrnode_t][key ... '\0'][value ... '\0']
 */
typedef struct kvs_arrnode_s {
    char    *key;
    char    *value;
    int      key_len;
    int      value_len;
    uint8_t  cls;        /* 保留給 slab，0xFF = malloc */
} kvs_arrnode_t;

/*
 * table 是緊湊的指標陣列：[0, total) 全部有效，沒有空洞。
 * 刪除時把最後一個搬到被刪的位置，O(1)。
 */
typedef struct kvs_array_s {
    kvs_arrnode_t **table;
    size_t          total;
    size_t          cap;
} kvs_array_t;

int    kvs_array_create(kvs_array_t *inst);
void   kvs_array_destory(kvs_array_t *inst);

int    kvs_array_set(kvs_array_t *inst, char *key, char *value, size_t key_len, size_t value_len);
char  *kvs_array_get(kvs_array_t *inst, char *key, size_t key_len, size_t *value_len);
int    kvs_array_del(kvs_array_t *inst, char *key, size_t key_len);
int    kvs_array_mod(kvs_array_t *inst, char *key, char *value, size_t key_len, size_t value_len);
int    kvs_array_exist(kvs_array_t *inst, char *key, size_t key_len);
int    kvs_array_count(kvs_array_t *inst);

size_t kvs_array_save(kvs_array_t *inst, char **msg, size_t *length, uint32_t count);
int    kvs_array_save_slave(kvs_array_t *inst, char **msg, size_t *length);

#endif