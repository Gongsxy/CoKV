#ifndef KVS_NODE_ALLOC_H
#define KVS_NODE_ALLOC_H

#include <stddef.h>
#include <stdint.h>
#include "alloc_conf.h"

#ifndef USE_SLAB
#define USE_SLAB (!USE_JEMALLOC)
#endif

#define KVS_CLS_MALLOC  0xFF       

void *kvs_node_alloc(size_t need, uint8_t *cls);

void  kvs_node_free(void *node, uint8_t cls);

void  kvs_node_pools_destroy(void);

int   kvs_node_pool_info(char *buf, size_t cap);

#endif