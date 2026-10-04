
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "mempool.h"

#define MAX_BLOCK   4095


struct mp_pool *mp_create(size_t size);
static void *mp_alloc_large(struct mp_pool *pool, size_t size);
static void *mp_alloc_block(struct mp_pool *pool, size_t size);
void mp_reset(struct mp_pool *pool);
void mp_large_free(struct mp_pool *pool, void *p);
void *mp_alloc(struct mp_pool *pool, size_t size);
struct mp_pool *mp_create(size_t size);
void mp_destory(struct mp_pool *pool);


static void *mp_alloc_large(struct mp_pool *pool, size_t size){//ok
    if(!pool || size == 0) return NULL;

    void *p = malloc(size);
    if(!p) return NULL;

    struct mp_large_s *large;
    for(large = pool->large; large; large = large->next){
        if(large->alloc == NULL){
            large->alloc = p;
            return p;
        }
    }

 
    large = mp_alloc(pool, sizeof(struct mp_large_s));
    if (large == NULL) {
		free(p);
		return NULL;
	}
    
    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}   

static void *mp_alloc_block(struct mp_pool *pool, size_t size){//ok

    if(!pool || size == 0) return NULL;

    unsigned char *m;
    struct mp_block_s *new_block; struct mp_block_s *h; struct mp_block_s *p; struct mp_block_s *current;

    h = pool->head;
    size_t per_size = (size_t)(h->end - (unsigned char *)h);

    m = malloc(per_size);
    if(!m) return NULL;

    new_block = (struct mp_block_s *)m;
    new_block->next = NULL;
    new_block->failed = 0;
    new_block->end = m + per_size;

    m += sizeof(struct mp_block_s);
    new_block->last = m + size;

    current = pool->current;
    for(p = current; p->next; p = p->next){
        if(p->failed++ > 4){
            current = p->next;
        }
    }

    p->next = new_block;
    
    pool->current = current ? current : new_block;

    return m;
}

void mp_reset(struct mp_pool *pool){
    if(!pool) return ;

    struct mp_block_s *h;
    for(h = pool->head; h; h = h->next){
        h->last = (unsigned char *)h + sizeof(struct mp_block_s);
        h->failed = 0;
    }

    struct mp_large_s *l;
    for(l = pool->large; l; l = l->next){
        if(l->alloc){
            free(l->alloc);
        }
    }

    pool->large = NULL;
}

void mp_large_free(struct mp_pool *pool, void *p){
    if(!pool) return ;

    struct mp_large_s *l;
    for(l = pool->large; l; l = l->next){
        if(p == l->alloc){
            free(l->alloc);
            l->alloc = NULL;

            return ;
        }
    }

}

void *mp_alloc(struct mp_pool *pool, size_t size){
    if(!pool ||size == 0) return NULL;

    if(size <= pool->max){
        struct mp_block_s *n;
        for(n = pool->current; n; n = n->next){
            if(n->end - n->last >= size){
                unsigned char *m = n->last;
                n->last += size;
                return m;
            }
            n->failed++;
        }

        return mp_alloc_block(pool, size);
    }

    return mp_alloc_large(pool, size);
}

struct mp_pool *mp_create(size_t size){
    if(size == 0) return NULL;
    
    unsigned char *ret = malloc(size + sizeof(struct mp_pool) + sizeof(struct mp_block_s));
    if(!ret) return NULL;

    struct mp_pool *pool = (struct mp_pool *)ret;

    if(MAX_BLOCK > size) {
        pool->max = size;
    }else pool->max = MAX_BLOCK;

    pool->large = NULL;

    pool->current = pool->head;

    pool->head->end = ret + sizeof(struct mp_pool) + sizeof(struct mp_block_s) + size;
    pool->head->failed = 0;
    pool->head->next = NULL;
    pool->head->last = ret + sizeof(struct mp_block_s) + sizeof(struct mp_pool);

    return pool;
}

void mp_destory(struct mp_pool *pool){
    if(!pool) return ;

    struct mp_large_s *l;
    for(l = pool->large; l; l = l->next){
        if(l->alloc) free(l->alloc);
    }

    struct mp_block_s *h = pool->head->next; 
    struct mp_block_s *p;
    while(h){
        p = h->next;
        free(h);
        h = p;
    }

    free(pool);
}
