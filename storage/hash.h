#ifndef __HASH_H__
#define __HASH_H__

#include "common.h"


typedef struct hashnode_s {
    char *key;
    char *value;
    int   key_len;
    int   value_len;
    struct hashnode_s *next;
    uint8_t cls;
} hashnode_t;

typedef struct hashtable_s {
    hashnode_t **nodes;
    int          max_slots;
    size_t       count;
} kvs_hash_t;

extern kvs_hash_t *globle_hash;

int    kvs_hash_create(kvs_hash_t *hash);
void   kvs_hash_destory(kvs_hash_t *hash);
int    kvs_hash_set(kvs_hash_t *hash, char *key, char *value, size_t key_len, size_t value_len);
char  *kvs_hash_get(kvs_hash_t *hash, char *key, size_t key_len, size_t *value_len);
int    kvs_hash_count(kvs_hash_t *hash);
int    kvs_hash_del(kvs_hash_t *hash, char *key, size_t key_len);
int    kvs_hash_exist(kvs_hash_t *hash, char *key, size_t key_len);
int    kvs_hash_mod(kvs_hash_t *hash, char *key, char *value, size_t key_len, size_t value_len);

size_t kvs_hash_save(kvs_hash_t *hash, char **msg, size_t *length, uint32_t count);
int  kvs_hash_save_slave(kvs_hash_t *hash, char **msg, size_t *length);


#endif