#ifndef SLAB_POOL_H
#define SLAB_POOL_H

#include <stddef.h>
#include <stdint.h>

#define SLAB_CHUNK_SIZE         (64 * 1024)     

typedef struct slab_chunk {

    struct slab_chunk *prev; struct slab_chunk *next;
    struct slab_chunk *aprev; struct slab_chunk *anext;
    void *free_head;   

    uint32_t used; 
    uint32_t bump;  

    uint32_t cap; 
    int in_list;  

    struct slab_pool *pool;   
} slab_chunk_t;


typedef struct slab_pool {

    size_t obj_size;    
    uint32_t cap;       

    slab_chunk_t *partial;  
    slab_chunk_t *all;       

    size_t empty;           
    size_t num_chunks;      
    size_t num_used;        

} slab_pool_t ;


int slab_pool_init(slab_pool_t *p, size_t obj_size);
void *slab_alloc(slab_pool_t *p);
void slab_free(void *o);
void slab_pool_destroy(slab_pool_t *p);


#endif