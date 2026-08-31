
#ifndef URING_SLOT_H
#define URING_SLOT_H

#include <pthread.h>
#include <string.h>

typedef struct uring_info{
	int fd;
	int idx;
	char buffer[1024];
	char *save_buf_ptr;
	int used;
	int free_next;
} uring_info;

uring_info *u_info_alloc(void);
void u_info_pool_init(void);
void u_info_free(uring_info *info);
#endif