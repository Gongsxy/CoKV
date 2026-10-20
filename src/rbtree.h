#ifndef __KVS_RBTREE_H__
#define __KVS_RBTREE_H__

#include <stddef.h>
#include <stdint.h>

#define KVS_RB_RED      0
#define KVS_RB_BLACK    1

/*
 * 節點佈局與 hashnode_t 相同：一次 malloc
 *   [kvs_rbnode_t][key ... '\0'][value ... '\0']
 */
typedef struct kvs_rbnode_s {
    struct kvs_rbnode_s *left;
    struct kvs_rbnode_s *right;
    struct kvs_rbnode_s *parent;

    char *key;
    char *value;
    int   key_len;
    int   value_len;

    unsigned char color;
    uint8_t       cls;      /* 保留給 slab，0xFF = malloc */
} kvs_rbnode_t;

typedef struct kvs_rbtree_s {
    kvs_rbnode_t *root;
    kvs_rbnode_t  nil;      /* 哨兵，嵌在結構裡，永遠用 &tree->nil 取 */
    size_t        count;
} kvs_rbtree_t;

int    kvs_rbtree_create(kvs_rbtree_t *tree);
void   kvs_rbtree_destory(kvs_rbtree_t *tree);

int    kvs_rbtree_set(kvs_rbtree_t *tree, char *key, char *value, size_t key_len, size_t value_len);
char  *kvs_rbtree_get(kvs_rbtree_t *tree, char *key, size_t key_len, size_t *value_len);
int    kvs_rbtree_del(kvs_rbtree_t *tree, char *key, size_t key_len);
int    kvs_rbtree_mod(kvs_rbtree_t *tree, char *key, char *value, size_t key_len, size_t value_len);
int    kvs_rbtree_exist(kvs_rbtree_t *tree, char *key, size_t key_len);
int    kvs_rbtree_count(kvs_rbtree_t *tree);

size_t kvs_rbtree_save(kvs_rbtree_t *tree, char **msg, size_t *length, uint32_t count);
int    kvs_rbtree_save_slave(kvs_rbtree_t *tree, char **msg, size_t *length);

#endif