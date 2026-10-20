#ifndef __KVS_SKIPLIST_H__
#define __KVS_SKIPLIST_H__

#include <stddef.h>
#include <stdint.h>

#define KVS_SKIP_MAX_LEVEL   32
#define KVS_SKIP_P           0.25     /* 每層晉升機率，與 Redis 相同 */

/*
 * 節點一次 malloc：
 *   [kvs_sknode_t][forward[level]][key ... '\0'][value ... '\0']
 */
typedef struct kvs_sknode_s {
    char    *key;
    char    *value;
    int      key_len;
    int      value_len;

    uint8_t  cls;        /* 保留給 slab，0xFF = malloc */
    uint8_t  level;

    struct kvs_sknode_s *forward[];
} kvs_sknode_t;

typedef struct kvs_skiplist_s {
    kvs_sknode_t *header;
    int           level;
    size_t        count;
    uint32_t      rnd;   /* 自己的隨機數狀態，不影響全域 rand() */
} kvs_skiplist_t;

int    kvs_skiplist_create(kvs_skiplist_t *inst);
void   kvs_skiplist_destory(kvs_skiplist_t *inst);
#define kvs_skiplist_destroy kvs_skiplist_destory   /* 相容舊名字 */

int    kvs_skiplist_set(kvs_skiplist_t *inst, char *key, char *value, size_t key_len, size_t value_len);
char  *kvs_skiplist_get(kvs_skiplist_t *inst, char *key, size_t key_len, size_t *value_len);
int    kvs_skiplist_del(kvs_skiplist_t *inst, char *key, size_t key_len);
int    kvs_skiplist_mod(kvs_skiplist_t *inst, char *key, char *value, size_t key_len, size_t value_len);
int    kvs_skiplist_exist(kvs_skiplist_t *inst, char *key, size_t key_len);
int    kvs_skiplist_count(kvs_skiplist_t *inst);

size_t kvs_skiplist_save(kvs_skiplist_t *inst, char **msg, size_t *length, uint32_t count);
int    kvs_skiplist_save_slave(kvs_skiplist_t *inst, char **msg, size_t *length);

#endif