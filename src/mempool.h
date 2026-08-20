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


// typedef struct mem_chunk {

//     void *memory;

//     struct mem_chunk *next;

// }M_chunk;

// typedef struct mem_block {
//     int id;
//     struct mem_block *next;
//     struct mem_block *prev;
// }M_block;

typedef struct mem_pool {
    size_t size;       
    size_t offset;
    char *memory;
}Mempool;

#define PAGE_SIZE  2048
#define BLOCK_NUM   6
