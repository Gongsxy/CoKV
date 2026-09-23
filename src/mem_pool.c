
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "mem_pool.h"

#define MAX_BLOCK   64 * 100


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

#if 0
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>



#define MP_ALIGNMENT       		32
#define MP_PAGE_SIZE			4096
#define MP_MAX_ALLOC_FROM_POOL	(MP_PAGE_SIZE-1)

#define mp_align(n, alignment) (((n)+(alignment-1)) & ~(alignment-1))
#define mp_align_ptr(p, alignment) (void *)((((size_t)p)+(alignment-1)) & ~(alignment-1))//向上取整到 alignment 的倍数，实现按2的幂次方对齐





struct mp_large_s {
	struct mp_large_s *next;// 大块链表的下一个节点
	void *alloc;// 指向真正 malloc 出来的用户数据内存
};

struct mp_node_s {

	unsigned char *last;// 当前空闲内存的起点
	unsigned char *end;// 当前块的末尾
	
	struct mp_node_s *next;// 链表下一个块
	size_t failed;/*这块内存分配失败的次数 后续有一块是这么用的：失败超过 4 次，就把 current 往后挪
如果一块内存反复分配失败（说明它快满了）*/
};

struct mp_pool_s {//内存池总管

	size_t max;//小内存 vs 大内存”的分界线

	struct mp_node_s *current;// 当前可用的小块
	struct mp_large_s *large;// 大块链表头

	struct mp_node_s head[0];/*它实际不占sizeof空间，只是为了让管理单元末尾和第一个块起始共用同一块内存*/
};

struct mp_pool_s *mp_create_pool(size_t size);
void mp_destory_pool(struct mp_pool_s *pool);
void *mp_alloc(struct mp_pool_s *pool, size_t size);
void *mp_nalloc(struct mp_pool_s *pool, size_t size);
void *mp_calloc(struct mp_pool_s *pool, size_t size);
void mp_free(struct mp_pool_s *pool, void *p);


struct mp_pool_s *mp_create_pool(size_t size) {//按位对齐创建内存池和小块管理单元，大块初始化置空，第一个小块把head这个零长数组指向的第一个节点的用户空间起始位置和这个块的结束位置设置，并把这个赋给current作为第一个小块，小块大块分界线也设置，小块失败次数置零

	struct mp_pool_s *p;
	int ret = posix_memalign((void **)&p, MP_ALIGNMENT, size + sizeof(struct mp_pool_s) + sizeof(struct mp_node_s));//一次性申请：总管结构 + 第一个节点 + 用户空间
	if (ret) {
		return NULL; // 分配失败
	}
	
	p->max = (size < MP_MAX_ALLOC_FROM_POOL) ? size : MP_MAX_ALLOC_FROM_POOL;// 2. 设置小/大内存分界线,怕出现池子大小比默认判定值还低
	p->current = p->head;//当前活跃块
	p->large = NULL;//大内存链表初始化

	p->head->last = (unsigned char *)p + sizeof(struct mp_pool_s) + sizeof(struct mp_node_s);//设置用户空间的起点
	p->head->end = p->head->last + size;//设置用户空间的终点

	p->head->failed = 0;// 失败计数清零

	return p;

}

void mp_destory_pool(struct mp_pool_s *pool) {//分大小快。小块因为是侵入设计，所以需要先把后块指针拿出来再free，大块则直接判断对应的alloc是不是空，不是就free。因为大块的链表数据用的是小块存储，所以先大后小，最后free池子
	struct mp_node_s *h, *n;
	struct mp_large_s *l;

	for (l = pool->large; l; l = l->next) {//释放所有大块内存的真实数据
		if (l->alloc) {
			free(l->alloc);// 释放用户真正使用的内存
		}
	}
    // free(l);

	h = pool->head->next;//第一块在 pool 里面，不在这里释放

	while (h) {//释放所有后续小块内存（从第二块开始）
		n = h->next;// 先保存下一块
		free(h);// 释放当前整块
		h = n;// 跳到下一块
	}

	free(pool);//释放初始的整块内存（包含 pool 结构 + 第一个 mp_node_s + 第一块用户空间）

}

void mp_reset_pool(struct mp_pool_s *pool) {//分大小快。因为大块链表数据用的小块存储，所以先大后小。大块遍历每个节点的alloc，有就free，并且把池子的大块指针置空。小块则是把last用户空间起始指针设置回起始位置

	struct mp_node_s *h;
	struct mp_large_s *l;

	for (l = pool->large; l; l = l->next) {// 1. 释放所有大块内存的真实数据
		if (l->alloc) {
			free(l->alloc);
		}
	}

	pool->large = NULL;// 2. 清空大块链表

	for (h = pool->head; h; h = h->next) {//遍历小块
		h->last = (unsigned char *)h + sizeof(struct mp_node_s);//指针回拨到起始用户空间
	}//指针p + n 等于 p + n * sizeof(*p) 而sizeof(char)固定为1

}

static void *mp_alloc_block(struct mp_pool_s *pool, size_t size) {//分配小块。这里的小块是定长的，所以要把第一块的小块管理节点+用户空间大小算出来。然后按位对齐连续分配。把这个起始指针强转成管理节点并设置。把当前可用小块拿出来，遍历，并检查失败次数，分配失败次数到定值后就pass这个小块。把新小块挂在链表末尾。return出新块用户空间指针

	unsigned char *m;
	struct mp_node_s *h = pool->head;//h 指向第一块的管理节点
	size_t psize = (size_t)(h->end - (unsigned char *)h);//第一块的"管理节点 + 用户空间"的总大小
	
	int ret = posix_memalign((void **)&m, MP_ALIGNMENT, psize);//对齐分配一块大小为 psize 的新内存 psize是管理模块加上用户空间大小,创建的时候直接把小块内存最大设置好了
	if (ret) return NULL;

	struct mp_node_s *p, *new_node, *current;
	new_node = (struct mp_node_s*)m;//m 是新内存起始地址，强转成管理节点

	new_node->end = m + psize;//设置新块的末尾 = 起始 + 总大小
	new_node->next = NULL;// 初始化下一个块
	new_node->failed = 0;//失败次数置零

	m += sizeof(struct mp_node_s);//后移一个管理小块的大小,指向用户地址
	m = mp_align_ptr(m, MP_ALIGNMENT);// 按位对齐
	new_node->last = m + size;// 结构体末尾内存指针后移到用户区域

	current = pool->current;//从当前可用小块节点开始拿

	for (p = current; p->next; p = p->next) {//遍历链表直到最后一个块 p->next == NULL 时退出
		if (p->failed++ > 4) { //内存不够就pass
			current = p->next;
		}
	}
	p->next = new_node;//新节点挂载链表末尾

	pool->current = current ? current : new_node;//如果 current 非空就设为它（可能是跳过失败块后的某个旧块），为空才设为 new_node

	return m;//返回对齐后的用户地址 m

}

static void *mp_alloc_large(struct mp_pool_s *pool, size_t size) {//分配大块。直接系统申请一块。遍遍历大块，最多搜5个节点，找到alloc == NULL的空闲节点就复用它，把新数据挂上去，没有就分配一个小块当管理节点。设置管理节点，头插法放进池子

	void *p = malloc(size);// 分配内存
	if (p == NULL) return NULL;// 判断失败

	size_t n = 0;
	struct mp_large_s *large;
	for (large = pool->large; large; large = large->next) {// 按要求遍历所有大块节点，最多五次
		if (large->alloc == NULL) {// 有被释放的就复用
			large->alloc = p;
			return p;
		}
		if (n ++ > 3) break;// 连着5个被分配就不搜了
	}

	large = mp_alloc(pool, sizeof(struct mp_large_s));//从内存池的小块空间里，切出一块 16 字节，用来存放 mp_large_s 这个管理节点。
	if (large == NULL) {//判断失败
		free(p);
		return NULL;
	}

	large->alloc = p;//挂载地址指针
	large->next = pool->large;/*头插法
    */pool->large = large;      

	return p;// 返回用户数据的指针
}

void *mp_memalign(struct mp_pool_s *pool, size_t size, size_t alignment) {//直接按位对齐申请连续指定大小的内存，分配管理单元并设置，头插法放进池子

	void *p;
	
	int ret = posix_memalign(&p, alignment, size);//对齐分配大块
	if (ret) {
		return NULL;
	}

	struct mp_large_s *large = mp_alloc(pool, sizeof(struct mp_large_s));//分配对应的小块管理单元
	if (large == NULL) {
		free(p);
		return NULL;
	}

	large->alloc = p;//设置内存地址指针
	large->next = pool->large;/*头插法
	*/pool->large = large;

	return p;
}




void *mp_alloc(struct mp_pool_s *pool, size_t size) {//先判断大小。小块则拿到当前可用小块地址，把用户空间按位对齐后判断这块剩余空间够不够，够就把这个区域划出来，不够找下一个。都没有开新的小块。大块直接调用创建函数。

	unsigned char *m;
	struct mp_node_s *p;

	if (size <= pool->max) {// 小块路线

		p = pool->current;// 从当前活跃块开始找

		do {
			
			m = mp_align_ptr(p->last, MP_ALIGNMENT);// 对齐拿到可用小块内存的起始地址指针
			if ((size_t)(p->end - m) >= size) {// 剩余空间够
				p->last = m + size;// 把 last 往后推
				return m;
			}
			p = p->next;// 不够就找下一块
		} while (p);// 所有块都不够

		return mp_alloc_block(pool, size);//开新块
	}

	return mp_alloc_large(pool, size);// 大块路线
	
}


void *mp_nalloc(struct mp_pool_s *pool, size_t size) {//上一个的不对齐版本

	unsigned char *m;
	struct mp_node_s *p;

	if (size <= pool->max) {//小块
		p = pool->current;

		do {
			m = p->last;//拿到第一个小块起始地址，不做按位对齐
			if ((size_t)(p->end - m) >= size) {//有空就复用
				p->last = m+size;
				return m;
			}
			p = p->next;//没空下一个
		} while (p);

		return mp_alloc_block(pool, size);//全都没有创建新的
	}

	return mp_alloc_large(pool, size);//大块
	
}

void *mp_calloc(struct mp_pool_s *pool, size_t size) {//分配 + 清零

	void *p = mp_alloc(pool, size);
	if (p) {
		memset(p, 0, size);
	}

	return p;
	
}

void mp_free(struct mp_pool_s *pool, void *p) {//针对大块。小块是从内存池的大块里“切”出来的，没有独立的malloc头部，不能单独free。大块则是每个独立，所以能直接遍历free置空

	struct mp_large_s *l;
	for (l = pool->large; l; l = l->next) {//遍历大块
		if (p == l->alloc) {//有则释放
			free(l->alloc);
			l->alloc = NULL;//置空

			return ;
		}
	}
	
}


int main(int argc, char *argv[]) {

	int size = 1 << 12;

	struct mp_pool_s *p = mp_create_pool(size);

	int i = 0;
	for (i = 0;i < 10;i ++) {

		void *mp = mp_alloc(p, 512);
//		mp_free(mp);
	}

	//printf("mp_create_pool: %ld\n", p->max);
	printf("mp_align(123, 32): %d, mp_align(17, 32): %d\n", mp_align(24, 32), mp_align(17, 32));
	//printf("mp_align_ptr(p->current, 32): %lx, p->current: %lx, mp_align(p->large, 32): %lx, p->large: %lx\n", mp_align_ptr(p->current, 32), p->current, mp_align_ptr(p->large, 32), p->large);

	int j = 0;
	for (i = 0;i < 5;i ++) {

		char *pp = mp_calloc(p, 32);
		for (j = 0;j < 32;j ++) {
			if (pp[j]) {
				printf("calloc wrong\n");
			}
			printf("calloc success\n");
		}
	}

	//printf("mp_reset_pool\n");

	for (i = 0;i < 5;i ++) {
		void *l = mp_alloc(p, 8192);
		mp_free(p, l);
	}

	mp_reset_pool(p);

	//printf("mp_destory_pool\n");
	for (i = 0;i < 58;i ++) {
		mp_alloc(p, 256);
	}

	mp_destory_pool(p);

	return 0;

}

#endif