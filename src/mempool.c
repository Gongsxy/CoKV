

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "mempool.h"

// #define POOL_NUM    5
static Mempool global_mpool = {0};

// 

void mpool_destroy(void){
    free(global_mpool.memory);

    global_mpool.memory = NULL;

    global_mpool.size = 0;

    global_mpool.offset = 0;
}


void m_reset(){
    global_mpool.offset = 0;
}

static bool mpool_ensure(size_t need){
    if (global_mpool.memory == NULL) {
        size_t init_size = 1024 * 1024 * 100;
        global_mpool.memory = malloc(init_size);
        if (global_mpool.memory == NULL) {
            perror("malloc");
            return false;
        }
        global_mpool.size = init_size;
        global_mpool.offset = 0;
    }
    return true;
}

void *m_malloc(size_t size){
    if (size == 0) return NULL;

    if (!mpool_ensure(global_mpool.offset + size)) {
        return NULL;
    }

    /* À©ÈÝ */
    // if (global_mpool.offset + size > global_mpool.size) {
    //     size_t new_size = global_mpool.size * 2;
    //     while (new_size < global_mpool.offset + size) {
    //         new_size *= 2;
    //     }

    //     char *tmp = realloc(global_mpool.memory, new_size);
    //     if (tmp == NULL) return NULL;

    //     global_mpool.memory = tmp;
    //     global_mpool.size  = new_size;
    // }

    if (global_mpool.offset + size > global_mpool.size) {
        fprintf(stderr, "[mempool] OUT OF MEMORY! offset=%zu, need=%zu, total=%zu\n",global_mpool.offset, size, global_mpool.size);
        return NULL;
    }

    void *ptr = (char *)global_mpool.memory + global_mpool.offset;
    global_mpool.offset += size;
    return ptr;
}

// void mpool_create(size_t size)
// {
//     global_mpool.memory = malloc(size);
//     if(global_mpool.memory == NULL){
//         perror("malloc");
//         exit(-1);
//     }
//     global_mpool.size = size;

//     global_mpool.offset = 0;
// }

// 
// void *m_malloc(size_t size)
// {
//     if(global_mpool.offset + size > global_mpool.size){
//         size_t new_size = global_mpool.size * 2;

//         while(new_size < global_mpool.offset + size){
//             new_size *= 2;
//         }


//         char *tmp = realloc(global_mpool.memory,new_size);

//         if(tmp == NULL) return NULL;

//         global_mpool.memory = tmp;

//         global_mpool.size = new_size;
//     }


//     void *ptr = global_mpool.memory + global_mpool.offset;


//     global_mpool.offset += size;


//     return ptr;
// }


