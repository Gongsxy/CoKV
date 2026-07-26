

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
    mpool_create();
    if (global_mpool == NULL || node == NULL) return ;
    LIST_INSERT(node, global_mpool->free_list);
}

int main()
{
    // 用来存申请的所有块，最多10个
    M_block *arr[10] = {NULL};
    int i;

    printf("=====test1:apply whole (10) block =====\n");
    // 把10个块全部申请出来
    for(i = 0; i < BLOCK_NUM; i++)
    {
        arr[i] = m_malloc();
        if(arr[i] == NULL)
        {
            printf("failed id:%d\n", i);
        }
        else
        {
            M_block *node = (M_block *)arr[i] - 1;
            printf("success id:%d\n", node->id);
        }
    }

    // 全拿完再次申请,应当返回NULL
    printf("\n=====test2: No free blocks, Overcommit allocation=====\n");
    M_block *tmp = m_malloc();
    if(tmp == NULL)
        printf("No blocks,return NULL, success\n");
    else
        printf("Still can apply blocks, failed\n");

    printf("\n=====test3:return half blocks, apply again=====\n");
    // 归还偶数下标块
    for(i = 0; i < BLOCK_NUM; i += 2)
    {
        m_free(arr[i]);
        arr[i] = NULL;
        printf("return id:%d\n",i);
    }
    // 重新申请5次
    for(i = 0; i < 5; i++)
    {
        M_block *p = m_malloc();
        if(p){
            printf("apply blocks successed,id:%d\n", (p - 1)->id - 1);
        }
    }

    printf("\n=====test5:return all blocks=====\n");
    for(i = 0; i < BLOCK_NUM; i++)
    {
        if(arr[i] != NULL)
        {
            m_free(arr[i]);
            M_block *node = (M_block *)arr[i] - 1;
            printf("%d blocks successfully return\n",node->id + 1);
            arr[i] = NULL;
        }
    }
    printf("all blocks successfully return\n");

    printf("run out all test,program exit normal,atexit free memory\n");
    return 0;
}