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

						
extern int recv_total_length;

struct fd_info {
    char ip[32];
    int fd;
	char info[4096];
	char send_info[4096];
	int s_len;
};
extern struct fd_info *fd_list;


void kvs_conn_ctx_free(int fd);
int kvs_protocol(int fd, char *msg, int length, char **response, size_t *response_size);


#define KVS_ARRAY_SIZE      1048576
#define KVS_TOKENS_MAX      1048576


typedef int(*msg_handle)(char *msg, int length, char *response);


// typedef struct kvs_array_items_s{
//     char *key;
//     char *value;
// }kvs_array_items_t;

// typedef struct ksv_array_s{
//     kvs_array_items_t *table;
//     int idx;
//     int total;
// }kvs_array_t;
// extern kvs_array_t *globle_array;

int proactor_start(unsigned short port, msg_handle handle);
int ntyco_start(Config *server_cfg, __g_rdma *g_m_rdma);




typedef struct hashnode_s {

	char *key;
	char *value;
	int key_len;      
    int value_len;
	struct hashnode_s *next;
	uint8_t cls;
} hashnode_t;


typedef struct hashtable_s {

	hashnode_t **nodes; //* change **, 

	int max_slots;
	int count;

} hashtable_t;

typedef struct hashtable_s kvs_hash_t;
extern kvs_hash_t *globle_hash;


int kvs_hash_create(kvs_hash_t *hash);
void kvs_hash_destory(kvs_hash_t *hash);
int kvs_hash_set(kvs_hash_t *hash, char *key, char *value, size_t key_len, size_t value_len);
char * kvs_hash_get(kvs_hash_t *hash, char *key, size_t key_len, size_t *value_len);
int kvs_hash_count(kvs_hash_t *hash);
int kvs_hash_del(kvs_hash_t *hash, char *key, size_t key_len);
int kvs_hash_exist(kvs_hash_t *hash, char *key, size_t key_len);
int kvs_hash_mod(kvs_hash_t *hash, char *key, char *value, size_t key_len, size_t value_len);


int kvs_txt_write(char *cmd, char* key, char* value, int *key_len, int *value_len);
int kvs_txt_del(char *cmd, char* key, int *key_len);
int kvs_txt_exist(char *cmd, char* key, int *key_len);
int kvs_txt_mod(char *cmd, char* key, char* value, int *key_len, int *value_len);
char * kvs_txt_get(char *cmd, char* key, int *key_len);


void kvs_txt_load(char *cmd, char *key, char *value, size_t key_len, size_t value_len);
void kvs_txt_full_load(char *msg);
int kvs_add_save(char *msg);



size_t kvs_hash_save(kvs_hash_t *hash, char **msg, size_t *length, uint32_t count);
int kvs_hash_save_slave(kvs_hash_t *hash, char **msg, size_t *length);


#define KVS_MAX_VALUE   (512u * 1024u * 1024u) 
#define KVS_MAX_FRAME   (KVS_MAX_VALUE + 64u * 1024u)
#define KVS_RET_DROP    -3
#define KVS_MAX_FORWARD_QUEUE  (2u * KVS_MAX_FRAME + 64u * 1024u)


#endif