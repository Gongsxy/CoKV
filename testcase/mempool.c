

#include <stdio.h>
#include <stdlib.h>

#include "mempool.h"

// #define POOL_NUM    5
static Mempool global_mpool[BLOCK_NUM] = {0};

void mpool_destory(void){
    // if(global_mpool == NULL) return ;
    int i = 0;
    for(i = 0; i < BLOCK_NUM; i ++){
        free(global_mpool[i].memory);
    }
    global_mpool[i].memory = NULL;
    global_mpool[i].free_list = NULL;
}
// 
static void mpool_create(void){
    if(global_mpool[0].memory != NULL) return ;
    static int ae = 0;
    if(!ae){
        atexit(mpool_destory);
        ae = 1;
    }
    
    int i = 0;
    size_t size = 32;
    for(i = 0; i < BLOCK_NUM; i ++){
        global_mpool[i].memory = malloc(PAGE_SIZE);
        global_mpool[i].block_size = sizeof(M_block) + size;
        global_mpool[i].block_num = PAGE_SIZE / global_mpool[i].block_size;
        global_mpool[i].free_list = NULL; 
        size = size * 2;
        int j = 0;
        char *ptr = global_mpool[i].memory;
        for(j = 0; j < global_mpool[i].block_num; j ++){
            M_block *node = (M_block *)(ptr + j * global_mpool[i].block_size);
            LIST_INSERT(node, global_mpool[i].free_list);
            // node = global_mpool[i].free_list;
            node->id = i;
        }
    }
}

// 
void *m_malloc(int size){
    mpool_create();
    int i = 0;
    if(size <= 32){
        i = 0;
    }else if(size <= 64){
        i = 1;
    }else if(size <= 128){
        i = 2;
    }else if(size <= 256){
        i = 3;
    }else if(size <= 512){
        i = 4;
    }else if(size <= 1024){
        i = 5;
    }else return NULL;
    if(global_mpool[i].free_list == NULL) return NULL;
    M_block *node = global_mpool[i].free_list;
    LIST_REMOVE(node, global_mpool[i].free_list);
    return (void *)(node + 1);
}

void m_free(void *ptr){
    if (ptr == NULL) return ;
    M_block *node = (M_block *)ptr - 1;
    int i = node->id;
    LIST_INSERT(node, global_mpool[i].free_list);
}
