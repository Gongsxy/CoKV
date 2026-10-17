#include "slab_pool.h"
#include <stdlib.h>
#include <string.h>

#define HDR ((sizeof(slab_chunk_t) + 15) & ~(size_t)15)

static inline slab_chunk_t *chunk_of(void *o){
    return (slab_chunk_t *)((uintptr_t)o & ~(uintptr_t)(SLAB_CHUNK_SIZE - 1));
}

static void partial_push(slab_pool_t *p, slab_chunk_t *c){
    c->prev = NULL; c->next = p->partial;
    if (p->partial) p->partial->prev = c;
    p->partial = c; c->in_list = 1;
}
static void partial_remove(slab_pool_t *p, slab_chunk_t *c){
    if (c->prev) c->prev->next = c->next; else p->partial = c->next;
    if (c->next) c->next->prev = c->prev;
    c->prev = c->next = NULL; c->in_list = 0;
}
static void all_remove(slab_pool_t *p, slab_chunk_t *c){
    if (c->aprev) c->aprev->anext = c->anext; else p->all = c->anext;
    if (c->anext) c->anext->aprev = c->aprev;
}

int slab_pool_init(slab_pool_t *p, size_t obj_size){
    memset(p, 0, sizeof(*p));
    if (obj_size < sizeof(void *)) obj_size = sizeof(void *);
    obj_size = (obj_size + 7) & ~(size_t)7;              /* 8 位元組對齊 */
    if (obj_size > SLAB_CHUNK_SIZE - HDR) return -1;
    p->obj_size = obj_size;
    p->cap = (uint32_t)((SLAB_CHUNK_SIZE - HDR) / obj_size);
    return 0;
}

static slab_chunk_t *chunk_new(slab_pool_t *p){
    slab_chunk_t *c = aligned_alloc(SLAB_CHUNK_SIZE, SLAB_CHUNK_SIZE);
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));
    c->cap = p->cap;
    c->anext = p->all;
    if (p->all) p->all->aprev = c;
    p->all = c;
    p->nchunks++; p->empty++;
    partial_push(p, c);
    return c;
}

void *slab_alloc(slab_pool_t *p){
    slab_chunk_t *c = p->partial;
    if (!c && !(c = chunk_new(p))) return NULL;

    void *o;
    if (c->free_head) {
        o = c->free_head;
        c->free_head = *(void **)o;
    } else {
        o = (char *)c + HDR + (size_t)c->bump * p->obj_size;
        c->bump++;
    }
    if (c->used == 0) p->empty--;
    c->used++; p->nused++;

    if (!c->free_head && c->bump == c->cap) partial_remove(p, c);   /* 滿了 */
    return o;
}

void slab_free(slab_pool_t *p, void *o){
    if (!o) return;
    slab_chunk_t *c = chunk_of(o);

    int was_full = !c->in_list;
    *(void **)o = c->free_head;
    c->free_head = o;
    c->used--; p->nused--;
    if (was_full) partial_push(p, c);

    if (c->used == 0) {
        p->empty++;
        if (p->empty > 1) {                 /* 保留一個備用，其餘還給 OS */
            partial_remove(p, c);
            all_remove(p, c);
            p->empty--; p->nchunks--;
            free(c);
        }
    }
}

void slab_pool_destroy(slab_pool_t *p){
    slab_chunk_t *c = p->all, *n;
    while (c) { n = c->anext; free(c); c = n; }
    memset(p, 0, sizeof(*p));
}