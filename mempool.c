

#include <stdio.h>
#include <stdlib.h>

#define LIST_INSERT(item,list)do{   \
    item->prev = NULL;              \
    item->next = list;              \
    if((list) != NULL){             \
        (list)->prev = item;        \
    }                               \
    (list) = item;                  \
}while(0);

#define LIST_REMOVE(item,list)do{       \
    if(item->prev != NULL){             \
        item->prev->next = item->next;  \
    }                                   \
    if(item->next != NULL){             \
        item->next->prev = item->prev;  \
    }                                   \
    if(item == list){                   \
        list = item->next;              \
    }                                   \
    item->next = item->prev = NULL;     \
}while(0);

typedef struct mem_block {
    int id;
    struct mem_block *next;
    struct mem_block *prev;
}M_block;

typedef struct mem_pool {
    int block_size;
    int block_num;
    void *memory;
    M_block *free_list;
}Mempool;

static Mempool *global_mpool = NULL;
#define BLOCK_SIZE  1024
#define BLOCK_NUM   10

void mpool_destory(void){
    if(global_mpool == NULL) return ;
    free(global_mpool->memory);
    global_mpool->memory = NULL;
    free(global_mpool);
    global_mpool = NULL;
}

static void mpool_create(void){
    if(global_mpool != NULL) return ;
    static int ae = 0;
    if(!ae){
        atexit(mpool_destory);
        ae = 1;
    }
    Mempool *m_pool = malloc(sizeof(Mempool));
    m_pool->block_num = BLOCK_NUM;
    m_pool->block_size = BLOCK_SIZE;
    m_pool->free_list = NULL;
    m_pool->memory = malloc(BLOCK_NUM * BLOCK_SIZE);

    char *ptr = m_pool->memory;
    int i = 0;
    for(i = 0; i < BLOCK_NUM; i ++){
        M_block *node = (M_block *)(ptr + i * BLOCK_SIZE);
        node->id = i;
        LIST_INSERT(node,m_pool->free_list);
    }
    global_mpool = m_pool;
}

// 
void *m_malloc(int size){
    mpool_create();
    if(global_mpool == NULL || global_mpool->free_list == NULL) return NULL;
    M_block *node = global_mpool->free_list;
    LIST_REMOVE(node, global_mpool->free_list);
    return (void *)(node + 1);
}

void m_free(void *ptr){
    M_block *node = (M_block *)ptr - 1;
    if (global_mpool == NULL || node == NULL) return ;
    LIST_INSERT(node, global_mpool->free_list);
}
