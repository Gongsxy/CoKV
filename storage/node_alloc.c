

#include "node_alloc.h"
#include "slab_pool.h"

#include <stdio.h>
#include <stdlib.h>

#define NCLS 8
static const size_t g_cls_size[NCLS] = { 32, 48, 64, 80, 96, 128, 192, 256 };
static slab_pool_t  g_cls_pool[NCLS];

#if USE_SLAB
static int g_pools_ready = 0;

static void pools_init(void){
    for (int i = 0; i < NCLS; i++) {
        if (slab_pool_init(&g_cls_pool[i], g_cls_size[i]) != 0) {
            fprintf(stderr, "[slab] init pool %zu failed\n", g_cls_size[i]);
            abort();
        }
    }
    g_pools_ready = 1;
}
#endif


void *kvs_node_alloc(size_t need, uint8_t *cls){
#if USE_SLAB
    if (!g_pools_ready) pools_init();

    for (int i = 0; i < NCLS; i++) {
        if (need <= g_cls_size[i]) {
            void *p = slab_alloc(&g_cls_pool[i]);
            if (p) { *cls = (uint8_t)i; return p; }
            break;                       /* 池子要不到記憶體，改用 malloc */
        }
    }
#endif
    *cls = KVS_CLS_MALLOC;               /* 太大，或不用池子 */
    return malloc(need);
}


void kvs_node_free(void *node, uint8_t cls){
    if (!node) return;
#if USE_SLAB
    if (cls != KVS_CLS_MALLOC) { slab_free(node); return; }
#else
    (void)cls;
#endif
    free(node);
}


void kvs_node_pools_destroy(void){
#if USE_SLAB
    for (int i = 0; i < NCLS; i++) slab_pool_destroy(&g_cls_pool[i]);
    g_pools_ready = 0;
#endif
}


int kvs_node_pool_info(char *buf, size_t cap){
    int off = 0;
    for (int i = 0; i < NCLS && (size_t)off < cap; i++) {
        if (g_cls_pool[i].num_chunks == 0) continue;     /* 沒用到的大小就不印 */
        off += snprintf(buf + off, cap - off, " pool%zu:chunks=%zu,used=%zu",
                        g_cls_size[i], g_cls_pool[i].num_chunks, g_cls_pool[i].num_used);
    }
    return off;
}