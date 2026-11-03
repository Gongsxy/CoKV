#ifndef KVS_NODE_ALLOC_H
#define KVS_NODE_ALLOC_H

#include <stddef.h>
#include <stdint.h>
#include "build_conf.h"

/* 1 = 節點走記憶體池；jemalloc 版本預設不用 */
#ifndef USE_SLAB
#define USE_SLAB (!USE_JEMALLOC)
#endif

#define KVS_CLS_MALLOC  0xFF            /* 這個節點是 malloc 來的 */

/* 配置 need bytes，*cls 記下來源（池子編號或 KVS_CLS_MALLOC），失敗回傳 NULL */
void *kvs_node_alloc(size_t need, uint8_t *cls);

/* 依 cls 還給池子或 free */
void  kvs_node_free(void *node, uint8_t cls);

/* 銷毀所有池子（所有節點都釋放之後才能呼叫） */
void  kvs_node_pools_destroy(void);

/* INFO 用：印出每個有用到的池子 " poolN:chunks=x,used=y"，回傳寫了幾個 bytes */
int   kvs_node_pool_info(char *buf, size_t cap);

#endif