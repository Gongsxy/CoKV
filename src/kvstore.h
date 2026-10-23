#define _GNU_SOURCE

#ifndef __KVSTORE_H__
#define __KVSTORE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <liburing.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <ctype.h>
#include <sys/mman.h>			
#include <sys/stat.h>


#include <pthread.h>

#include "load_conf.h"
#include "rdma.h"
#include "../NtyCo/core/nty_coroutine.h"


#define KVS_MAX_VALUE   (512u * 1024u * 1024u) 
#define KVS_MAX_FRAME   (KVS_MAX_VALUE + 64u * 1024u)
#define KVS_RET_DROP    -3
#define KVS_MAX_FORWARD_QUEUE  (2u * KVS_MAX_FRAME + 64u * 1024u)


extern int g_repl_collect;


void kvs_conn_ctx_free(int fd);
int kvs_protocol(int fd, char *msg, int length, char **response, size_t *response_size);


int ntyco_start(Config *server_cfg, __g_rdma *g_m_rdma);


#endif