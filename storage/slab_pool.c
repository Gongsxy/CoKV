#define _GNU_SOURCE

#include "slab_pool.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>



#define HDR     ((sizeof(slab_chunk_t) + 15) &~(size_t)15)


static inline slab_chunk_t *chunk_of(void *o){
    return (slab_chunk_t *)((uintptr_t)o & ~(uintptr_t)(SLAB_CHUNK_SIZE - 1));
}


static void partial_push(slab_pool_t *p, slab_chunk_t *c){

    c->prev = NULL;
    c->next = p->partial;
    if(p->partial) {
        p->partial->prev = c;
    }

    p->partial = c;

    c->in_list = 1;

}


static void partial_remove(slab_pool_t *p, slab_chunk_t *c){

    if(!c->in_list) return ;

    if(c->prev) c->prev->next = c->next;
    else p->partial = c->next;

    if(c->next) c->next->prev = c->prev;
    
    c->prev = c->next = NULL;

    c->in_list = 0;

}


static void all_push(slab_pool_t *p, slab_chunk_t *c){

    c->aprev = NULL;

    c->anext = p->all;
    
    if(p->all){
        p->all->aprev = c;
    }

    p->all = c;

}


static void all_remove(slab_pool_t *p, slab_chunk_t *c){

    if(c->aprev) c->aprev->anext = c->anext;
    else p->all = c->anext;

    if(c->anext) c->anext->aprev = c->aprev;

    c->aprev = c->anext = NULL;

}


static void *map_aligned(void){

    size_t len = SLAB_CHUNK_SIZE * 2;

    char *p = mmap(NULL, len, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return NULL;

    uintptr_t a = (uintptr_t)(p + (SLAB_CHUNK_SIZE - 1)) & ~ (uintptr_t)(SLAB_CHUNK_SIZE - 1);

    size_t head = a - (uintptr_t)p;
    size_t tail = len - head - SLAB_CHUNK_SIZE;

    if(head) munmap(p, head);
    if(tail) munmap((char *)a + SLAB_CHUNK_SIZE, tail);

    return (void *)a;
}

static slab_chunk_t *chunk_new(slab_pool_t *p){

    slab_chunk_t *c = map_aligned();
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));

    c->cap = p->cap;
    c->pool = p; 

    all_push(p, c);
    partial_push(p, c);

    p->num_chunks++;
    p->empty++;

    return c;
}




int slab_pool_init(slab_pool_t *p, size_t obj_size){

    if(!p) return -1;
    memset(p, 0, sizeof(*p));

    if(obj_size < sizeof(void *)) obj_size = sizeof(void *);

    obj_size = (obj_size + 15) & ~ (size_t)15;

    size_t cap = (SLAB_CHUNK_SIZE - HDR) / obj_size;
    if(cap == 0) return -2;

    p->cap = (uint32_t)cap;
    p->obj_size = obj_size;

    return 0;
}


void *slab_alloc(slab_pool_t *p){

    slab_chunk_t *c = p->partial;

    if(!c && !(c = chunk_new(p))) return NULL;

    void *o;
    if(c->free_head){
        o = c->free_head;
        c->free_head = *(void **)o;
    }else{
        o = (char *)c + HDR + (size_t)c->bump * p->obj_size;
        c->bump++;
    }

    if(c->used == 0) p->empty--;

    c->used++;
    p->num_used++;

    if(!c->free_head && c->bump == c->cap) partial_remove(p, c);

    return o;
}


void slab_free(void *o){
    if (!o) return;
    slab_chunk_t *c = chunk_of(o);
    slab_pool_t  *p = c->pool;          /* ÄºÐ×Ó˜Ëî^ÄÃµ½³Ø×Ó */
    int was_full = !c->in_list;

    *(void **)o = c->free_head;
    c->free_head = o;
    c->used--;
    p->num_used--;

    if (was_full) partial_push(p, c);

    if (c->used == 0) {
        p->empty++;
        if (p->empty > 1) {
            partial_remove(p, c);
            all_remove(p, c);
            p->empty--;
            p->num_chunks--;
            munmap(c, SLAB_CHUNK_SIZE);
        }
    }
}

void slab_pool_destroy(slab_pool_t *p){

    slab_chunk_t *c = p->all;
    slab_chunk_t *n;

    while(c){

        n = c->anext;

        munmap(c, SLAB_CHUNK_SIZE);

        c = n;
    }

    memset(p, 0, sizeof(*p));
}


