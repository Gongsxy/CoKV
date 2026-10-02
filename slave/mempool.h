#ifndef __MEMPOOL_H__
#define __MEMPOOL_H__

#include <stdio.h>

struct mp_pool *mp_create(size_t size);
void mp_reset(struct mp_pool *pool);
void mp_large_free(struct mp_pool *pool, void *p);
void *mp_alloc(struct mp_pool *pool, size_t size);
void mp_destory(struct mp_pool *pool);

struct mp_block_s{
    unsigned char *last;
    unsigned char *end;

    struct mp_block_s *next;
    size_t failed;
};

struct mp_large_s{
    struct mp_large_s *next;
    void *alloc;
};

struct mp_pool{
    size_t max;
    struct mp_block_s *current;
    struct mp_large_s *large;
    
    struct mp_block_s head[0];
};

#endif