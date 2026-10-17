#ifndef SLAB_POOL_H
#define SLAB_POOL_H
#include <stddef.h>
#include <stdint.h>

#define SLAB_CHUNK_SIZE (64 * 1024)

typedef struct slab_chunk {
    struct slab_chunk *prev, *next;     /* partial 鏈表 */
    struct slab_chunk *aprev, *anext;   /* all 鏈表（destroy 用） */
    void     *free_head;                /* 已釋放的物件 */
    uint32_t  used;                     /* 使用中的物件數 */
    uint32_t  bump;                     /* 已切出的物件數 */
    uint32_t  cap;                      /* 每個 chunk 可放幾個 */
    uint32_t  in_list;                  /* 是否在 partial 鏈表 */
} slab_chunk_t;

typedef struct {
    size_t        obj_size;
    uint32_t      cap;
    slab_chunk_t *partial;
    slab_chunk_t *all;
    size_t        empty;     /* 全空的 chunk 數 */
    size_t        nchunks;
    size_t        nused;
} slab_pool_t;

int   slab_pool_init(slab_pool_t *p, size_t obj_size);
void *slab_alloc(slab_pool_t *p);
void  slab_free(slab_pool_t *p, void *obj);
void  slab_pool_destroy(slab_pool_t *p);

#endif