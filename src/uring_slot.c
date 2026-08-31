
#include "uring_slot.h"

#define MAX_URING_INFO 1024

static uring_info u_info[MAX_URING_INFO];
static int u_info_free_head = 0;
static int u_info_free_count = MAX_URING_INFO;
static pthread_mutex_t u_info_lock = PTHREAD_MUTEX_INITIALIZER;

void u_info_pool_init(void) {
    for (int i = 0; i < MAX_URING_INFO; i++) {
        u_info[i].idx = i;
        u_info[i].used = 0;
        u_info[i].save_buf_ptr = NULL;
        u_info[i].free_next = (i + 1 < MAX_URING_INFO) ? i + 1 : -1;
    }
    u_info_free_head = 0;
    u_info_free_count = MAX_URING_INFO;
}

uring_info *u_info_alloc(void) {
    pthread_mutex_lock(&u_info_lock);
    if (u_info_free_head == -1) {
        pthread_mutex_unlock(&u_info_lock);
        return NULL;
    }
    int idx = u_info_free_head;
    u_info_free_head = u_info[idx].free_next;
    u_info_free_count--;
    memset(&u_info[idx], 0, sizeof(u_info[idx]));
    u_info[idx].idx = idx;
    u_info[idx].used = 1;
    pthread_mutex_unlock(&u_info_lock);
    return &u_info[idx];
}

void u_info_free(uring_info *info) {
    if (!info) return;
    pthread_mutex_lock(&u_info_lock);
    info->used = 0;
    info->save_buf_ptr = NULL;
    info->free_next = u_info_free_head;
    u_info_free_head = info->idx;
    u_info_free_count++;
    pthread_mutex_unlock(&u_info_lock);
}