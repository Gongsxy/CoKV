#ifndef __MEM_POOL_H__
#define __MEM_POOL_H__

#include <stdio.h>

struct mp_pool *mp_create(size_t size);
static void *mp_alloc_large(struct mp_pool *pool, size_t size);
static void *mp_alloc_block(struct mp_pool *pool, size_t size);
void mp_reset(struct mp_pool *pool);
void mp_large_free(struct mp_pool *pool, void *p);
void *mp_alloc(struct mp_pool *pool, size_t size);
struct mp_pool *mp_create(size_t size);
void mp_destory(struct mp_pool *pool);

#endif