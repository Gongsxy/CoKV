

#ifndef __KVSTORE_H__
#define __KVSTORE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <liburing.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <ctype.h>

#include <jemalloc/jemalloc.h>

// #include "server.h"

#define RED				1
#define BLACK 			2

#define ENABLE_RBTREE   	1	
#define ENABLE_ARRAY		1
#define ENABLE_HASH			1
#define ENABLE_SKIPTABLE	1
#define ENABLE_SAVE			1

#define ENABLE_EPOLL		0
#define ENABLE_URING		1
#define ENABLE_NTYCO		0

#define STORAGE_ADD			1

#define ENABLE_SEND			1

#define ENABLE_KEY_CHAR	1

#define JEMALLOC			1

#define SHARED				1

#define FD_MAX				128

#define INFO_LENGTH			1024
#define BUF_LENGTH		1048576
						

int recv_total_length;

struct fd_info {
    char ip[32];
    int fd;
	char info[INFO_LENGTH];
	char send_info[4096];
	int s_len;
};
extern struct fd_info *fd_list;
char global_push_msg[INFO_LENGTH];


int check_fd_to_ip(int fd, char *ip);

typedef struct son_conn{
	int fd[FD_MAX];
} connection;
connection son;

int kvs_protocol_push(char *msg);
int kvs_push_get(char *msg);
void kvs_push_clear();


#if ENABLE_RBTREE
typedef char * KEY_TYPE;

typedef struct _rbtree_node {
	unsigned char color;
	struct _rbtree_node *right;
	struct _rbtree_node *left;
	struct _rbtree_node *parent;
	KEY_TYPE key;
	void *value;
} rbtree_node;

typedef struct _rbtree {
	rbtree_node *root;
	rbtree_node *nil;
} rbtree;

typedef rbtree kvs_rbtree_t;

int kvs_rbtree_create(kvs_rbtree_t *inst);
void kvs_rbtree_destory(kvs_rbtree_t *inst);
int kvs_rbtree_set(kvs_rbtree_t *inst, char *key, char *value);
char * kvs_rbtree_get(kvs_rbtree_t *inst, char *key);
int kvs_rbtree_mod(kvs_rbtree_t *inst, char *key, char *value);
int kvs_rbtree_del(kvs_rbtree_t *inst, char *key);
int kvs_rbtree_exist(kvs_rbtree_t *inst, char *key);

#endif


#define KVS_ARRAY_SIZE      1048576
#define KVS_TOKENS_MAX      1048576

#if  ENABLE_ARRAY
typedef int(*msg_handle)(char *msg, int length, char *response);

typedef struct kvs_array_items_s{
    char *key;
    char *value;
}kvs_array_items_t;

typedef struct ksv_array_s{
    kvs_array_items_t *table;
    int idx;
    int total;
}kvs_array_t;
#endif


int reactor_start(unsigned short port, msg_handle hand);
int proactor_start(unsigned short port, msg_handle handle);


void *kvs_malloc(size_t size);
void kvs_free(void *ptr);

int kvs_array_create(kvs_array_t *inst);
int kvs_array_destory(kvs_array_t *inst);
char * kvs_array_get(kvs_array_t *inst, char *key);
int kvs_array_set(kvs_array_t *inst, char *key, char *value);
int kvs_array_del(kvs_array_t *inst, char *key);
int kvs_array_mod(kvs_array_t *inst, char *key, char *value);
int kvs_array_exist(kvs_array_t *inst, char *key);
int kvs_array_save(kvs_array_t *inst,char *msg);

extern kvs_array_t *globle_array;

#if ENABLE_HASH
#define MAX_KEY_LEN	128
#define MAX_VALUE_LEN	512
#define MAX_TABLE_SIZE	1024


extern kvs_rbtree_t *globle_rbtree;

typedef struct hashnode_s {

#if ENABLE_HASH
	char *key;
	char *value;
#else
	char key[MAX_KEY_LEN];
	char value[MAX_VALUE_LEN];
#endif
	struct hashnode_s *next;
	
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
int kvs_hash_set(kvs_hash_t *hash, char *key, char *value);
char * kvs_hash_get(kvs_hash_t *hash, char *key);
int kvs_hash_count(kvs_hash_t *hash);
int kvs_hash_del(kvs_hash_t *hash, char *key);
int kvs_hash_exist(kvs_hash_t *hash, char *key);
int kvs_hash_mod(kvs_hash_t *hash, char *key, char *value);

#endif

int reactor_b_start(const char *ip, unsigned short port, msg_handle hand);

#if ENABLE_SKIPTABLE

#if ENABLE_SKIPTABLE

#define MAX_LEVEL 8

typedef struct skiplist_node {

    char *key;
    char *value;
    int level;

    struct skiplist_node *forward[MAX_LEVEL];

}skiplist_node;


typedef struct skiplist {

    int level;

    skiplist_node *header;

}skiplist;

typedef skiplist kvs_skiplist_t;

extern kvs_skiplist_t *globle_skiplist;

int kvs_skiplist_create(kvs_skiplist_t *inst);
void kvs_skiplist_destroy(kvs_skiplist_t *inst);
int kvs_skiplist_set(kvs_skiplist_t *inst, char *key, char *value);
char* kvs_skiplist_get(kvs_skiplist_t *inst, char *key);
int kvs_skiplist_mod(kvs_skiplist_t *inst, char *key, char *value);
int kvs_skiplist_del(kvs_skiplist_t *inst, char *key);
int kvs_skiplist_exist(kvs_skiplist_t *inst, char *key);
#endif

#if ENABLE_SAVE
#define TXT_LENGTH	1024

#define FULLSTORAGE     0
#define ADDSTORAGE      1

// int kvs_txt_write(const char *filename, char* key, char* value);
// int kvs_txt_del(const char *filename, char* key);
// int kvs_txt_exist(const char *filename, char* key);
// int kvs_txt_mod(const char *filename, char* key, char* value);

// int kvs_txt_write(char *cmd, char* key, char* value, int *key_len, int *value_len);
int kvs_txt_write(char *cmd, char* key, char* value, int *key_len, int *value_len);
int kvs_txt_del(char *cmd, char* key, int *key_len);
int kvs_txt_exist(char *cmd, char* key, int *key_len);
int kvs_txt_mod(char *cmd, char* key, char* value, int *key_len, int *value_len);
char * kvs_txt_get(char *cmd, char* key, int *key_len);

void kvs_txt_read(char *cmd, char *key, char *value);
void kvs_txt_load(char *cmd, char *key, char *value);
void kvs_txt_full_load(char *msg);

// void kvs_txt_load(char *cmd, char *key, char *value);

#endif


#define ENABLE_MEMPOOL 1
#if ENABLE_MEMPOOL

void *m_malloc(int size);
void m_free(void *ptr);

#endif

#endif
#endif