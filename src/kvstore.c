
#include "kvstore.h"
#include "server.h"
#include "load_conf.h"
#include "uring_slot.h"
#include "rdma.h"
#include "../libbpf-bootstrap/examples/c/resp_capture.h"
#include "mem_pool.h"
#include <zlib.h>
#include <sodium.h>

#include <signal.h>
#include <malloc.h>

const char *malloc_conf =
    "background_thread:true,dirty_decay_ms:1000,muzzy_decay_ms:0,"
    "retain:false,abort_conf:true";

#if 1
kvs_array_t *globle_array = NULL;
#endif

#if ENABLE_RBTREE
kvs_rbtree_t *globle_rbtree = NULL;
#endif

#if ENABLE_HASH
kvs_hash_t *globle_hash = NULL;
#endif

#if ENABLE_SKIPTABLE
kvs_skiplist_t *globle_skiplist = NULL;
#endif

char save_message[4096] = {0};

const char *command[] = {
    "SET","GET","DEL","MOD","EXIST",
    "RSET","RGET","RDEL","RMOD","REXIST",
    "HSET","HGET","HDEL","HMOD","HEXIST",
    "SSET","SGET","SDEL","SMOD","SEXIST",
    "SYNC","PING","QUIT","INFO","SAVE",
    "CLEAR"
};

const char *key_value_command[] = {
    "SET","MOD","RSET","RMOD","HSET","HMOD","SSET","SMOD"
};

enum {
    KVS_CMD_START = 0,
    KVS_CMD_SET = KVS_CMD_START,
    KVS_CMD_GET,
    KVS_CMD_DEL,
    KVS_CMD_MOD,
    KVS_CMD_EXIST,
// rbtree
    KVS_CMD_RSET,
    KVS_CMD_RGET,
    KVS_CMD_RDEL,
    KVS_CMD_RMOD,
    KVS_CMD_REXIST,
// hash
    KVS_CMD_HSET,
    KVS_CMD_HGET,
    KVS_CMD_HDEL,
    KVS_CMD_HMOD,
    KVS_CMD_HEXIST,
// skiptable
    KVS_CMD_SSET,
    KVS_CMD_SGET,
    KVS_CMD_SDEL,
    KVS_CMD_SMOD,
    KVS_CMD_SEXIST,

    KVS_CMD_SYNC,
    KVS_CMD_PING,
    KVS_CMD_QUIT,
    KVS_CMD_INFO,
    KVS_CMD_SAVE,
    KVS_CMD_CLEAR,
    KVS_CMD_COUNT
};

enum {
    CMD,
    KEY,
    VALUE
};

Config server_cfg;
struct io_uring add_ring;
struct io_uring full_ring;
struct io_uring_params add_params;
struct io_uring_params full_params;
uring_info u_info[1024];
uring_info u_full_info;

static int g_add_ring_ok  = 0;
static int g_full_ring_ok = 0;

static struct mp_pool *pool = NULL;
static struct mp_pool *cmd_pool = NULL;

volatile sig_atomic_t g_stop = 0;


static __g_rdma gr = {0};


static int globle_add_fd = -1;
static int globle_full_fd = -1;
static int load_saved_msg = 1;


#define STATUS_IN	1
#define STATUS_OUT	0

#define INIT STATUS_OUT;

#define MAC_LEN crypto_auth_hmacsha256_BYTES
static unsigned char g_hmac_key[crypto_auth_hmacsha256_KEYBYTES];

#define MAX_BUF_SIZE    1024 * 1024 * 10

static char *mes = NULL;
static char *sync_mes = NULL;
static int   sync_mes_cap = 0;
static int   sync_mes_len = 0;


int get_add_fd(void){
    if(globle_add_fd < 0){
        globle_add_fd = open("config/add.txt", O_WRONLY | O_APPEND);
        if(globle_add_fd < 0){
            perror("open failed");
            return -1;
        }
    }
    
    return globle_add_fd;
}

int get_full_fd(void){
    if(globle_full_fd < 0){
        globle_full_fd = open("config/full.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(globle_full_fd < 0){
            perror("open failed");
            return -1;
        }
    }
    return globle_full_fd;
}


static int skip_add_log = 0;
int kvs_protocol_filter(char **tokens, char **response, size_t *resp_length, size_t *key_len, size_t *value_len){
    if(tokens == NULL || response == NULL) return -1;
    skip_add_log = 0;

    int i = KVS_CMD_START;
    for(i = KVS_CMD_START; i < KVS_CMD_COUNT; i ++){
        if(strcmp(tokens[0],command[i]) == 0) break;
    }

    if(i >= KVS_CMD_SET && i <= KVS_CMD_EXIST){
        if(strncmp(server_cfg.storage_structure, "rbtree", 6) == 0)        i += 5;
        else if(strncmp(server_cfg.storage_structure, "hash", 4) == 0)     i += 10;
        else if(strncmp(server_cfg.storage_structure, "skiplist", 8) == 0) i += 15;
    }
    char *cmd = tokens[0];
    char *key = tokens[1];
    char *value = tokens[2];
    int length = 0;
    int ret = 0;
    
    switch(i){

#if ENABLE_ARRAY
        case KVS_CMD_SET:{
            ret = kvs_array_set(globle_array,key,value);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR %s\r\n", "unknown command");
                // printf("ERROR\r\n");
            }else{  
                if(server_cfg.show_response == 1) length = sprintf(*response,"+OK\r\n");
                // printf("EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_GET:{       
            char *result = kvs_array_get(globle_array,key);
            if(result == NULL){
                if(server_cfg.show_response == 1) length = sprintf(*response,"$-1\r\n");
                // printf("NOT EXIST\r\n");
            }else{   
                if(server_cfg.show_response == 1){  
                    if(*value_len > *resp_length){
                        void *p = realloc(*response, *resp_length + *value_len + 2);
                        if(p == NULL){
                            return -1;
                        }
                        *response = p; *resp_length += *value_len + 2; 
                    }
                    length  = sprintf(*response, "$%ld\r\n", *value_len);
                    memcpy(*response + length, result, *value_len);
                    length += *value_len;
                    (*response)[length++] = '\r';
                    (*response)[length++] = '\n';   
                }
            }
            break;
        }
        case KVS_CMD_DEL:{
            ret = kvs_array_del(globle_array,key);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,":1\r\n");
                // printf("OK\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_MOD:{
            ret = kvs_array_mod(globle_array,key,value);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"+OK\r\n");
                // printf("OK\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR no such key\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_EXIST:
            ret = kvs_array_exist(globle_array,key);
            if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,":1\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,":0\r\n");
            }
            break;

#endif
// rbtree
// #if ENABLE_RBTREE
//         case KVS_CMD_RSET:{
//             ret = kvs_rbtree_set(globle_rbtree,key,value);
//             if(ret < 0){
//                 if(cfg.show_response == 1) length = sprintf(*response,"-ERR %s\r\n", "unknown command");
//             }else{
//                 if(cfg.show_response == 1) length = sprintf(*response,"+OK\r\n");
//             }
//             break;
//         }
//         case KVS_CMD_RGET:{
//             char *result = kvs_rbtree_get(globle_rbtree,key);
//             if(result == NULL){
//                 if(cfg.show_response == 1) length = sprintf(*response,"$-1\r\n");
//             }else{
//                 if(cfg.show_response == 1) length = sprintf(*response,"$%zu\r\n%s\r\n", strlen(result), result);
//             }
//             break;
//         }
//         case KVS_CMD_RDEL:{
//             ret = kvs_rbtree_del(globle_rbtree,key);
//             if(ret < 0){
//                 if(cfg.show_response == 1) length = sprintf(*response,"-ERR internal error\r\n");
//             }else if(ret == 0){
//                 if(cfg.show_response == 1) length = sprintf(*response,":1\r\n");
//             }else{
//                 if(cfg.show_response == 1) length = sprintf(*response,":0\r\n");
//             }
//             break;
//         }
//         case KVS_CMD_RMOD:{
//             ret = kvs_rbtree_mod(globle_rbtree,key,value);
//             if(ret < 0){
//                 if(cfg.show_response == 1) length = sprintf(*response,"-ERR internal error\r\n");
//             }else if(ret == 0){
//                 if(cfg.show_response == 1) length = sprintf(*response,"+OK\r\n");
//             }else{
//                 if(cfg.show_response == 1) length = sprintf(*response,"-ERR no such key\r\n");
//             }
//             break;
//         }
//         case KVS_CMD_REXIST:
//             ret = kvs_rbtree_exist(globle_rbtree,key);
//             if(ret == 0){
//                 if(cfg.show_response == 1) length = sprintf(*response,":1\r\n");
//             }else{
//                 if(cfg.show_response == 1) length = sprintf(*response,":0\r\n");
//             }
//             break;
// #endif
// hash
#if ENABLE_HASH
        case KVS_CMD_HSET:{
            if (*value_len > KVS_MAX_VALUE) {
                if(server_cfg.show_response == 1)
                    length = sprintf(*response, "-ERR value too large (max 512 MiB)\r\n");
                break;
            }

            int ret = kvs_hash_set(globle_hash,key,value, *key_len, *value_len);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR %s\r\n", "unknown command");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,"+OK\r\n");
            }
            break;
        }
        case KVS_CMD_HGET:{
            char *result = kvs_hash_get(globle_hash, key, *key_len, value_len);
            if(result == NULL){
                if(server_cfg.show_response == 1) length = sprintf(*response,"$-1\r\n");
            }else{
                if(server_cfg.show_response == 1){
                    if(*value_len > *resp_length){
                        void *p = realloc(*response, *resp_length + *value_len + 2);
                        if(p == NULL){
                            return -1;
                        }
                        *response = p; *resp_length += *value_len + 2; 
                    }
                    length  = sprintf(*response, "$%ld\r\n", *value_len);
                    memcpy(*response + length, result, *value_len);
                    length += *value_len;
                    (*response)[length++] = '\r';
                    (*response)[length++] = '\n';
                }
            }
            break;
        }
        case KVS_CMD_HDEL:{
            ret = kvs_hash_del(globle_hash,key, *key_len);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR internal error\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,":1\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,":0\r\n");
            }
            break;
        }
        case KVS_CMD_HMOD:{
            ret = kvs_hash_mod(globle_hash,key,value, *key_len, *value_len);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR internal error\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"+OK\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR no such key\r\n");
            }
            break;
        }
        case KVS_CMD_HEXIST:{
            ret = kvs_hash_exist(globle_hash,key, *key_len);
            if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,":1\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,":0\r\n");
            }
            break;
        }
#endif
// skiplist
#if ENABLE_SKIPTABLE
        case KVS_CMD_SSET:{
            ret = kvs_skiplist_set(globle_skiplist,key,value);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR %s\r\n", "unknown command");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,"+OK\r\n");
            }
            break;
        }
        case KVS_CMD_SGET:{
            char *result = kvs_skiplist_get(globle_skiplist,key);
            if(result == NULL){
                if(server_cfg.show_response == 1) length = sprintf(*response,"$-1\r\n");
            }else{          
                if(server_cfg.show_response == 1) length = sprintf(*response,"$%zu\r\n%s\r\n", strlen(result), result);
            }
            break;
        }
        case KVS_CMD_SDEL:{
            ret = kvs_skiplist_del(globle_skiplist,key);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR internal error\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,":1\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,":0\r\n");
            }
            break;
        }
        case KVS_CMD_SMOD:{
            ret = kvs_skiplist_mod(globle_skiplist,key,value);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR internal error\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"+OK\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR no such key\r\n");
            }
            break;
        }
        case KVS_CMD_SEXIST:{
            ret = kvs_skiplist_exist(globle_skiplist,key);
            if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,":1\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,":0\r\n");
            }
        }
            break;

#endif
        case KVS_CMD_PING:{
            if(server_cfg.show_response == 1) length = sprintf(*response,"+PONG\r\n");
            break;
        }
        case KVS_CMD_QUIT:{
            if(server_cfg.show_response == 1) length = sprintf(*response,"+OK\r\n");
            break;
        }
        case KVS_CMD_SYNC:{

            break;
        }
        case KVS_CMD_INFO:{
            
            int n   = (globle_hash != NULL) ? kvs_hash_count(globle_hash) : -1;
            int cap = (globle_hash != NULL) ? globle_hash->max_slots  : 0;
            unsigned long idx_bytes = (unsigned long)cap * sizeof(hashnode_t *);

            length = sprintf(*response,
                "+storage:%s count:%d max_slots:%d avg_chain:%.2f index_bytes:%lu\r\n",
                server_cfg.storage_structure, n, cap,
                (cap > 0) ? (double)n / cap : 0.0, idx_bytes);
            break;
        }
        case KVS_CMD_SAVE:{
            
            skip_add_log = 1;
            if(server_cfg.full_save == 1){
                int full_fd = get_full_fd();
                if (full_fd < 0) {
                    length = sprintf(*response, "-ERR open full file failed\r\n");
                    break;
                }
                char *tmp = NULL;
                size_t tmp_size = 1024 * 1024 * 1;
                tmp = calloc(1, tmp_size + MAC_LEN);
                
                if (!tmp) {
                    length = sprintf(*response, "-ERR out of memory\r\n");
                    close(full_fd);
                    globle_full_fd = -1;
                    break;
                }

                uint32_t count = (uint32_t)kvs_hash_count(globle_hash);

                size_t hash_length = kvs_hash_save(globle_hash, &tmp, &tmp_size, count);

                if (hash_length == 0) {
                    close(full_fd); globle_full_fd = -1;
                    free(tmp);
                    length = sprintf(*response, "+OK\r\n");
                    break;
                }

                 
                void *p = realloc(tmp, (size_t)hash_length + MAC_LEN);
                if (p == NULL) { 
                    close(full_fd);
                    globle_full_fd = -1; 
                     
                    free(tmp);
                    length = sprintf(*response, "-ERR out of memory\r\n");
                    break;
                }
                tmp = p;
                


                if (crypto_auth_hmacsha256((unsigned char *)(tmp + hash_length),
                           (const unsigned char *)tmp, hash_length,
                           g_hmac_key) != 0) {
                    close(full_fd); globle_full_fd = -1; 
                    free(tmp); 
                    length = sprintf(*response,"-ERR hmac failed\r\n"); 
                    break;
                }

                struct uring_info *full_info = calloc(1, sizeof(*full_info));
                if(full_info == NULL){
                    close(full_fd); globle_full_fd = -1;
                    free(tmp);
                    length = sprintf(*response, "-ERR out of memory\r\n");
                    break;
                }

                struct io_uring_sqe *sqe_full = io_uring_get_sqe(&full_ring);
                if (!sqe_full) {

                    close(full_fd); globle_full_fd = -1;
                    free(full_info);
                    free(tmp);
                    length = sprintf(*response, "-ERR save busy\r\n");
                    break;
                }


                memset(full_info, 0, sizeof(*full_info));
                full_info->save_buf_ptr = tmp;
                full_info->fd = full_fd;

                io_uring_prep_write(sqe_full, full_fd, full_info->save_buf_ptr, hash_length + MAC_LEN, 0);
                io_uring_sqe_set_data(sqe_full, full_info);

                int sub_ret = io_uring_submit(&full_ring);
                if (sub_ret < 0) {
                    close(full_fd); globle_full_fd = -1;
                    length = sprintf(*response, "-ERR submit failed\r\n");
                    abort();
                }

                struct io_uring_cqe *cqe;
                int wait_ret = io_uring_wait_cqe(&full_ring, &cqe);
                if (wait_ret == 0) {
                    struct uring_info *info = io_uring_cqe_get_data(cqe);

                    if (cqe->res < 0) {
                        length = sprintf(*response, "-ERR save failed\r\n");
                    } else {
                        fsync(full_fd);
                        length = sprintf(*response, "+OK\r\n");
                    }

                    if (info) {
                        free(info->save_buf_ptr);
                        free(info);
                    }
                    io_uring_cqe_seen(&full_ring, cqe);
                    close(full_fd); globle_full_fd = -1;
                } else {
                    close(full_fd); globle_full_fd = -1;
                    abort();
                    length = sprintf(*response, "-ERR save failed\r\n");
                }

                break;
            }
            length = sprintf(*response, "-ERR snapshot unavailable\r\n");
            break;
        }
        case KVS_CMD_CLEAR:{
            skip_add_log = 1;

            const char *paths[2] = { "config/add.txt", "config/full.txt" };
            int ok = 1;

            for(int k = 0; k < 2; k++){
                int cfd = open(paths[k], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if(cfd < 0){
                    fprintf(stderr, "[clear] %s: %s\n", paths[k], strerror(errno));
                    ok = 0;
                }else{
                    fsync(cfd);
                    close(cfd); 
                }
            }

            length = ok ? sprintf(*response, "+OK\r\n") : sprintf(*response, "-ERR clear failed\r\n");
            break;
        }

        default:{
            if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR unknown command '%s'\r\n", tokens[0]);
            break;
        }
    }

    return length;
};



int kvs_split_token(char *msg, char *tokens[], int length, size_t *key_len, size_t *value_len, int *resp_total){
    if(msg == NULL || length == 0 || key_len == 0) return -1;

    char *p = msg;

    int cmd_num = 0;
    int cmd_len = 0;
    if(*p == '*'){
        p += 1;
        cmd_num = *p - '0';
        p += 1;
    }else return -2;
        
    if(cmd_num < 1 || cmd_num > 3) return -1;

    p += 2;
    if(*p == '$'){
        p += 1;
        cmd_len = *p - '0';
        p += 1;
    }else return -2;

    p += 2;

        // if(server_cfg.use_mempool == 1){
        //     tokens[0] = mp_alloc(cmd_pool, cmd_len + 1);
        // }else 
    
    tokens[0] = malloc(cmd_len + 1);
    if(tokens[0] == NULL) return -1;

    memcpy(tokens[0], p, cmd_len);
    tokens[0][cmd_len] = '\0';
    for(int k = 0; k < cmd_len; k++){
        tokens[0][k] = (char)toupper((unsigned char)tokens[0][k]);
    }

    if(cmd_num >= 2){

        p += cmd_len + 2;

        if(*p == '$'){
            p += 1;
            while(*p >= '0' && *p <= '9'){
                *key_len = *key_len * 10 + (*p - '0');
                p ++;
            } 
        }
        p += 2; 

            // if(server_cfg.use_mempool == 1){
            //     tokens[1] = mp_alloc(cmd_pool, *key_len + 1);
            // }else 
        // tokens[1] = malloc(*key_len + 1);
        // if(tokens[1] == NULL) { free(tokens[0]); tokens[0] = NULL; return -1; }

        // memcpy(tokens[1], p, *key_len);
        // tokens[1][*key_len] = '\0';

        tokens[1] = p;
    }

    if(cmd_num == 2) return 0;

    if(cmd_num == 3){ 

        p += *key_len + 2;

        if(*p == '$'){
            p += 1;
            while(*p >= '0' && *p <= '9'){
                *value_len = *value_len * 10 + (*p - '0');
                p ++; 
            } 
        }
            p += 2;

            // if(server_cfg.use_mempool == 1){
            //     tokens[2] = mp_alloc(cmd_pool, *value_len + 1);
            // }else 
        // tokens[2] = malloc(*value_len + 1);
        // if(tokens[2] == NULL) { 
        //     free(tokens[0]); free(tokens[1]); 
        //     tokens[0] = NULL; tokens[1] = NULL;
        //     return -1; 
        // }

        // memcpy(tokens[2], p, *value_len);
        // tokens[2][*value_len] = '\0';
        tokens[2] = p;
    }
    
    return 0;
}

int kvs_is_complete(char *msg, int length, size_t *off){   

    
    if(msg == NULL || length < 0 || off == NULL) return -1;
    int idx = 0;
    while(idx < length){
        int cmd_num = 0; int cmd_len = 0; int key_len = 0; int value_len = 0;

        if(length - idx < 4) return -2;

        if(msg[idx] == '*'){
            idx += 1;
            if (idx >= length) return -2;
            cmd_num = msg[idx] - '0'; 
            idx += 1;     
        }
        
        if (length - idx < 2) return -2;

        idx += 2;

        if(cmd_num < 1 || cmd_num > 3) return -1;
        
        if (length - idx < 4) return -2;

        if(msg[idx] == '$'){
            if(idx + 2 > length) return -2;
            idx += 1;
            if (idx >= length) return -2;
            cmd_len = msg[idx] - '0';
            idx += 1;
            if(length - idx < 2) return -2;
            idx += 2;
        }else return -2;


        if(length - idx < cmd_len + 2) return -2;
        idx += cmd_len + 2;

        if(cmd_num == 1){
            *off = idx;
            return *off;
        }

        if(cmd_num >= 2){
            if (length - idx < 4) return -2;

            if(msg[idx] == '$'){
                idx += 1;
                while(idx < length && msg[idx] >= '0' && msg[idx] <= '9'){
                    key_len = key_len * 10 + (msg[idx] - '0');
                    idx += 1;
                } 
                if (length - idx < 2) return -2;
                idx += 2;

                if(length - idx < key_len + 2) return -2;
                idx += key_len + 2;
            } 
        }

        if(cmd_num == 2) {
            *off = idx;
            return *off;
        }

        if(cmd_num == 3){ 
            if (idx >= length) return -2;
            if(msg[idx] == '$'){
                idx += 1;
                while(idx < length && msg[idx] >= '0' && msg[idx] <= '9'){
                    value_len = value_len * 10 + (msg[idx] - '0');
                    idx += 1; 
                } 
                if (length - idx < 2) return -2;
                idx += 2;

                if(length - idx < value_len + 2) return -2;
                idx += value_len + 2;
            }
        }

        *off = idx;
        return *off;
    }
    return 0;
}


typedef struct {
    int fd;
    char  *mes;
    size_t mes_len;
    size_t mes_size; 
    char  *resp;  
    size_t resp_cap;       
} kvs_conn_ctx_t;
kvs_conn_ctx_t kvs_ctx[1024] = {0};

void kvs_conn_ctx_free(int fd){
    if (fd < 0 || fd >= 1024) return;

    free(kvs_ctx[fd].mes); kvs_ctx[fd].mes = NULL;
    free(kvs_ctx[fd].resp); kvs_ctx[fd].resp = NULL;

    kvs_ctx[fd].fd = -1; 
    kvs_ctx[fd].mes_len = 0; 
    kvs_ctx[fd].mes_size = 0;
    kvs_ctx[fd].resp_cap = 0;
}


static void reap_add_completions(void)
{
    struct io_uring_cqe *cqe;

    while (io_uring_peek_cqe(&add_ring, &cqe) == 0) {
        uring_info *done = io_uring_cqe_get_data(cqe);

        if (done != NULL) {
            if (done->len > 0 && cqe->res != done->len) {
                fprintf(stderr,
                        "[aof] write result=%d expected=%d\n",
                        cqe->res, done->len);
            }

            u_info_free(done);
        }

        io_uring_cqe_seen(&add_ring, cqe);
    }
}


int kvs_protocol(int fd, char *msg, int length, char **response, size_t *response_size){ 

    if (fd < 0 || fd >= 1024 || msg == NULL || length < 0) return -1;

    kvs_ctx[fd].fd = fd;
    size_t key_len = 0; size_t value_len = 0; size_t packet_len = 0;

    if (kvs_ctx[fd].mes == NULL) {
        // if(server_cfg.use_mempool == 1){
        //     mes = mp_alloc(pool, 1024 * 1024 * 5);
        //     if(mes) memset(mes, 0, 1024 * 1024 * 5);
        // }else 
        kvs_ctx[fd].mes_size = 1024 * 64;
        kvs_ctx[fd].mes = malloc(kvs_ctx[fd].mes_size);
        if (kvs_ctx[fd].mes == NULL) { kvs_ctx[fd].mes_size = 0; return -1; }
        kvs_ctx[fd].mes_len = 0;
    }

    size_t need = (size_t)length + kvs_ctx[fd].mes_len;

    if (load_saved_msg == 1 && need > KVS_MAX_FRAME) {
        fprintf(stderr, "[proto] fd=%d frame %zu > %u, drop conn\n",
                fd, need, (unsigned)KVS_MAX_FRAME);
        kvs_ctx[fd].mes_len = 0;
        return KVS_RET_DROP;
    }

    if(need > kvs_ctx[fd].mes_size){
        size_t new_size = kvs_ctx[fd].mes_size;
        while(need > new_size) {
            if (new_size > SIZE_MAX / 2) {
                new_size = need;
                break;
            }

            new_size += new_size / 2;
        }
        void *ptr = realloc(kvs_ctx[fd].mes, new_size);
        if(ptr == NULL) {
            const char *err = "-ERR frame too large\r\n";
            int elen = (int)strlen(err);
            if(response != NULL && *response != NULL) memcpy(*response, err, elen);
            kvs_ctx[fd].mes_len = 0;
            return *response ? elen : -1;
        }
        kvs_ctx[fd].mes = ptr; 
        kvs_ctx[fd].mes_size = new_size;
    }
    memcpy(kvs_ctx[fd].mes + kvs_ctx[fd].mes_len, msg, length);
    kvs_ctx[fd].mes_len += (size_t)length;

    char *tokens[3] = {NULL, NULL, NULL};

    size_t total_response_len = 0;

    while (kvs_ctx[fd].mes_len > 0) {
        size_t off = 0;
        key_len = 0; value_len = 0;
        int ret = kvs_is_complete(kvs_ctx[fd].mes, kvs_ctx[fd].mes_len, &off);
        if (ret == 0) break; 
        if (ret < 0){
            if (ret == -2) break;
            else {
                kvs_ctx[fd].mes_len = 0;
                const char *err = "-ERR protocol error\r\n";
                int elen = (int)strlen(err);
                if (response != NULL && *response) memcpy(*response, err, elen);
                return elen;
            }
        }
        if ((size_t)ret > kvs_ctx[fd].mes_len){
            const char *err = "-ERR over max range\r\n";
            int elen = (int)strlen(err);
            if(response != NULL && *response != NULL) memcpy(*response, err, elen);
            kvs_ctx[fd].mes_len = 0;
            return *response ? elen : -1;
        }

        packet_len = (size_t)ret;

        int resp_used = 0;
        int count = kvs_split_token(kvs_ctx[fd].mes, tokens, packet_len, &key_len, &value_len, &resp_used);

        if(count < 0){
            memmove(kvs_ctx[fd].mes, kvs_ctx[fd].mes + packet_len, kvs_ctx[fd].mes_len - packet_len);
            kvs_ctx[fd].mes_len -= packet_len;

            free(tokens[0]);
            tokens[0] = tokens[1] = tokens[2] = NULL;

            const char *err = "-ERR split token error\r\n";
            int elen = (int)strlen(err);
            if(response != NULL && *response != NULL){
                size_t need_r = total_response_len + (size_t)elen;
                if(need_r > *response_size){
                    size_t newcap = (*response_size > 0) ? *response_size : 4096;
                    while (newcap < need_r) newcap += newcap / 2 + 64;

                    void *ptr = realloc(*response, newcap);
                    if(ptr == NULL) return -2;
                    *response = ptr;
                    *response_size = newcap;
                }
                memcpy(*response + total_response_len, err, (size_t)elen);
                total_response_len = need_r;
            }
            break;
        } 
        
        int direct = (total_response_len == 0 && response != NULL && *response != NULL && *response_size >= 1024);

        char  *resp_buf;
        size_t resp_cap;

        if (direct) {
            resp_buf = *response;
            resp_cap = *response_size;
            resp_buf[0] = '\0';
        } else {
            
            if (kvs_ctx[fd].resp == NULL) {
                kvs_ctx[fd].resp_cap = 1024;
                kvs_ctx[fd].resp = malloc(kvs_ctx[fd].resp_cap);
                if (kvs_ctx[fd].resp == NULL) {
                    free(tokens[0]);
                    tokens[0] = tokens[1] = tokens[2] = NULL;
                    return -2;
                }
            }
            kvs_ctx[fd].resp[0] = '\0';
            resp_buf = kvs_ctx[fd].resp;
            resp_cap = kvs_ctx[fd].resp_cap;
        }
        resp_used = kvs_protocol_filter(tokens, &resp_buf, &resp_cap, &key_len, &value_len);


        if (direct) {
            *response = resp_buf;
            *response_size = resp_cap;
        } else {
            kvs_ctx[fd].resp = resp_buf;
            kvs_ctx[fd].resp_cap = resp_cap;
        }
    

#define AOF_BIG_THRESHOLD   (1024 * 1024)

        if(server_cfg.add_save == 1) {
            reap_add_completions();

            if(load_saved_msg == 1 && skip_add_log == 0){

                int add_fd = get_add_fd();

                

                if ((size_t)packet_len + 4 > AOF_BIG_THRESHOLD){
                    uint32_t crc_ret = (uint32_t)crc32(0L, (const Bytef *)kvs_ctx[fd].mes,
                                                       (uInt)packet_len);

                    struct iovec iov[2];
                    iov[0].iov_base = &crc_ret;
                    iov[0].iov_len = 4;
                    iov[1].iov_base = kvs_ctx[fd].mes;
                    iov[1].iov_len = packet_len;

                    struct io_uring_sqe *sqe = io_uring_get_sqe(&add_ring);
                    if (!sqe) {
                        io_uring_submit(&add_ring);
                        sqe = io_uring_get_sqe(&add_ring);                       
                    }

                    if(!sqe){
                        fprintf(stderr, "[aof] big: no sqe, drop\n");
                        abort();
                    }

                    io_uring_prep_writev(sqe, add_fd, iov, 2, 0);
                    io_uring_sqe_set_data(sqe, NULL);

                    if (io_uring_submit(&add_ring) < 0) {
                        fprintf(stderr, "[aof] big: submit fail\n");
                        abort();
                    } else {
                        int retry = 0;
                        for (;;) {
                            struct io_uring_cqe *cqe = NULL;
                            if (io_uring_wait_cqe(&add_ring, &cqe) < 0){
                                if (++retry > 100) {
                                    fprintf(stderr, "[aof] big: wait_cqe error, close AOF\n");
                                    abort(); 
                                }
                                continue;
                            }
                            retry = 0;

                            void *d = io_uring_cqe_get_data(cqe);
                            if (d == NULL) {
                                if (cqe->res != (int)(packet_len + 4))
                                    fprintf(stderr, "[aof] big write res=%d expect=%zu\n",
                                                cqe->res, packet_len + 4);
                                io_uring_cqe_seen(&add_ring, cqe);
                                break;
                            }
                            u_info_free((uring_info *)d);
                            io_uring_cqe_seen(&add_ring, cqe);
                        }   
                    }
                }else{
                    uring_info *slot = u_info_alloc();  

                    if (!slot) {
                        // fprintf(stderr, "[aof] pool empty, drop this record\n");   
                    } else {
                        slot->buffer = malloc((size_t)packet_len + 4);
                        if (!slot->buffer) {
                            u_info_free(slot);
                        } else {
                            uint32_t crc_ret = (uint32_t)crc32(
                                    0L, (const Bytef *)kvs_ctx[fd].mes, (uInt)packet_len);
                            slot->fd  = add_fd;
                            slot->len = (int)packet_len + 4;
                            memcpy(slot->buffer, &crc_ret, 4);
                            memcpy(slot->buffer + 4, kvs_ctx[fd].mes, packet_len);
                            
                            struct io_uring_sqe *sqe = io_uring_get_sqe(&add_ring);    
                            if (!sqe) {
                                free(slot->buffer); slot->buffer = NULL;
                                u_info_free(slot);
                                fprintf(stderr, "[aof] no sqe, drop this record\n");
                            }else{
                                io_uring_prep_write(sqe, add_fd, slot->buffer, packet_len + 4, 0);
                                io_uring_sqe_set_data(sqe, slot);
                                if (io_uring_submit(&add_ring) < 0) {              
                                    fprintf(stderr, "[aof] submit failed, drop\n");
                                    abort();
                                }
                            
                            }
                        }
                    }
                }
                
            }
        }

        memmove(kvs_ctx[fd].mes, kvs_ctx[fd].mes + packet_len, kvs_ctx[fd].mes_len - packet_len);
        kvs_ctx[fd].mes_len -= packet_len;

        if (resp_used > 0) {
            if(!direct && response != NULL && *response != NULL){
                size_t need_r = total_response_len + (size_t)resp_used;
                if (need_r > *response_size) {

                size_t newcap = (*response_size > 0) ? *response_size : 4096;
                    while (newcap < need_r) newcap += newcap / 2 + 64;
                    void *ptr = realloc(*response, newcap);
                    if (ptr == NULL) {
                        free(tokens[0]);
                        return -2;
                    }
                    *response = ptr; *response_size = newcap;
                }
                memcpy(*response + total_response_len, kvs_ctx[fd].resp, (size_t)resp_used);
            }
            total_response_len += (size_t)resp_used;
        }
            // fprintf(stderr, "[proto] drop reply: used=%d resp=%d limit=%d\n",
            //         total_response_len, resp_used, BUFFER_LENGTH);

        free(tokens[0]);
        tokens[0] = tokens[1] = tokens[2] = NULL;
    }

    if (kvs_ctx[fd].mes_len == 0 && kvs_ctx[fd].mes_size > 64 * 1024) {
        void *p = realloc(kvs_ctx[fd].mes, 64 * 1024);
        if (p) { kvs_ctx[fd].mes = p; kvs_ctx[fd].mes_size = 64 * 1024; }
    }

    if (kvs_ctx[fd].resp != NULL && kvs_ctx[fd].resp_cap > 64 * 1024) {
        void *p = realloc(kvs_ctx[fd].resp, 1024);
        if (p) { kvs_ctx[fd].resp = p; kvs_ctx[fd].resp_cap = 1024; }
    }

    return (int)total_response_len;
}


int init_kvs(void){
    if(strcmp(server_cfg.storage_structure, "array") == 0){
        if(server_cfg.use_mempool == 1){
            globle_array = mp_alloc(pool, sizeof(kvs_array_t));
        }else{
            globle_array = malloc(sizeof(kvs_array_t));
        }
        kvs_array_create(globle_array);
        
    }else if(strcmp(server_cfg.storage_structure, "hash") == 0){
        if(server_cfg.use_mempool == 1){
            globle_hash = mp_alloc(pool, sizeof(kvs_hash_t));
        }else{
            globle_hash = malloc(sizeof(kvs_hash_t));
        }
        kvs_hash_create(globle_hash);
    }else if(strcmp(server_cfg.storage_structure, "skiplist") == 0){
        if(server_cfg.use_mempool == 1){
            globle_skiplist = mp_alloc(pool, sizeof(kvs_skiplist_t));
        }else{
            globle_skiplist = malloc(sizeof(kvs_skiplist_t));
        }
        kvs_skiplist_create(globle_skiplist);
    }
    return 0;
}


void dest_kvs(void){
    if(strcmp(server_cfg.storage_structure, "array") == 0){
        kvs_array_destory(globle_array);
    }else if(strcmp(server_cfg.storage_structure, "hash") == 0){
        kvs_hash_destory(globle_hash);
    }else if(strcmp(server_cfg.storage_structure, "skiplist") == 0){
        kvs_skiplist_destroy(globle_skiplist);
    }

    close(globle_add_fd);
    close(globle_full_fd);
}


int origin_size = 0;
static char *old_mes = NULL;



void kvs_txt_load(char *cmd, char *key, char *value, size_t key_len, size_t value_len){
#if ENABLE_ARRAY
    if(strcmp(cmd,"SET") == 0){
        kvs_array_set(globle_array, key, value);
    }else if(strcmp(cmd,"DEL") == 0){
        kvs_array_del(globle_array, key);
    }else if(strcmp(cmd,"MOD") == 0){
        kvs_array_mod(globle_array, key, value);
    }else if(strcmp(cmd,"GET") == 0){
        kvs_array_get(globle_array, key);
    }else if(strcmp(cmd,"EXIST") == 0){
        kvs_array_exist(globle_array, key);
    }
#endif

#if ENABLE_HASH
    if(strcmp(cmd,"HSET") == 0){
        kvs_hash_set(globle_hash, key, value, key_len, value_len);
    }else if(strcmp(cmd,"HDEL") == 0){
        kvs_hash_del(globle_hash, key, key_len);
    }else if(strcmp(cmd,"HMOD") == 0){
        kvs_hash_mod(globle_hash, key, value, key_len, value_len);
    }else if(strcmp(cmd,"HGET") == 0){
        kvs_hash_get(globle_hash, key, key_len, &value_len);
    }else if(strcmp(cmd,"HEXIST") == 0){
        kvs_hash_exist(globle_hash, key, key_len);
    }
#endif
#if ENABLE_SKIPTABLE
    if(strcmp(cmd,"SSET") == 0){
        kvs_skiplist_set(globle_skiplist, key, value);
    }else if(strcmp(cmd,"SDEL") == 0){
        kvs_skiplist_del(globle_skiplist, key);
    }else if(strcmp(cmd,"SMOD") == 0){
        kvs_skiplist_mod(globle_skiplist, key, value);
    }else if(strcmp(cmd,"SGET") == 0){
        kvs_skiplist_get(globle_skiplist, key);
    }else if(strcmp(cmd,"SEXIST") == 0){
        kvs_skiplist_exist(globle_skiplist, key);
    }
#endif

}


#define FULL_HDR_SIZE  16
#define FULL_OFF_CRC     0
#define FULL_OFF_VER     8
#define FULL_OFF_COUNT   12
#define FULL_OFF_PACKLEN 4

void kvs_add_read(char *cmd, char *key, char *value){
    
    int fd = open("config/add.txt", O_RDONLY);
    if(fd == -1){
        perror("open a.txt");
        return;
    }

    struct stat st;
    if(fstat(fd, &st) < 0){
        perror("fstat error");
        close(fd);
        return ;
    }
    off_t size = st.st_size;
    if(size < 0){
        perror("fstat size error");
        close(fd);
        return ;
    }
    size_t len = st.st_size;
    if(len == 0){
        close(fd);
        return;   
    }
    char *addr = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    if(addr == MAP_FAILED){
        close(fd);
        perror("mmap error");
        return ;
    }

    
    // char *tmp = NULL;
    // if(server_cfg.use_mempool == 1){
    //     tmp = mp_alloc(pool, 1024 * 1024 * 5);
    // }else{
    //     tmp = calloc(1, 1024 * 1024 * 5);
    // }

    load_saved_msg = 0;

    char *p = addr;
    char *end = addr + len;
    int ok = 0; int bad = 0;

    char *replay_response = NULL;
    size_t replay_response_size = 0;

    while (p + 4 <= end) {
        uint32_t crc_exp;
        memcpy(&crc_exp, p, 4);

        size_t off = 0;
        int plen = kvs_is_complete(p + 4, (int)(end - p - 4), &off);//&off
        if (plen <= 0) {
            fprintf(stderr, "[load] tail incomplete at offset %zu (%zd bytes left)\n",
                    (size_t)(p - addr), (ssize_t)(end - p));
            break;
        }

        uint32_t crc_real = crc32(0L, p + 4, plen);
        if(crc_exp != crc_real){
            fprintf(stderr, "[load] record crc mismatch at offset %zu (file=%08x calc=%08x)\n",
                    (size_t)(p - addr), crc_exp, crc_real);
            bad++;
            break; 
        }

        kvs_protocol(fd, p + 4, plen, &replay_response, &replay_response_size);
        p += 4 + plen;
        ok++;
    }

    // fprintf(stderr, "[load] aof replay: ok=%d bad=%d, consumed %zu / %zu bytes\n",
    //         ok, bad, (size_t)(p - addr), len);
    kvs_conn_ctx_free(fd);
    free(replay_response);
    load_saved_msg = 1;
    munmap(addr, len);
    close(fd);
    // if(server_cfg.use_mempool == 1) mp_large_free(pool, tmp);
    // else free(tmp);
}



void kvs_full_read(char *cmd, char *key, char *value) {

    int fd = open("config/full.txt", O_RDONLY);
    if(fd == -1){
        perror("open a.txt");
        return;
    }

    char *cname = "SET";
    if (strncmp(server_cfg.storage_structure, "hash", 4) == 0) cname = "HSET";
    else if (strncmp(server_cfg.storage_structure, "skiplist", 8) == 0) cname = "SSET";
    else if (strncmp(server_cfg.storage_structure, "array", 5) == 0) cname = "SET";

    struct stat st;
    if(fstat(fd, &st) < 0){
        perror("fstat error");
        close(fd);
        return ;
    }
    off_t size = st.st_size;
    if(size < 0){
        perror("fstat size error");
        close(fd);
        return ;
    }
    size_t len = st.st_size;
    if(len == 0){
        close(fd);
        return;   
    }

    if (len < FULL_HDR_SIZE + MAC_LEN) {
        fprintf(stderr, "[load] too small: %zu\n", len);
        close(fd); return;
    }

    char *addr = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    if(addr == MAP_FAILED){
        perror("mmap error");
        close(fd);
        return ;
    }

    size_t mac_off = len - MAC_LEN;
    
    unsigned char mac_calc[MAC_LEN];
    if (crypto_auth_hmacsha256(mac_calc,(const unsigned char *)addr, mac_off,
                               g_hmac_key) != 0) {
        fprintf(stderr, "[load] hmac compute failed\n");
        goto out;
    }

    if (sodium_memcmp(mac_calc, addr + mac_off, MAC_LEN) != 0) {
        fprintf(stderr, "[load] MAC mismatch: file forged or key wrong\n");
        munmap(addr, len);
        close(fd);
        return ;                    
    }
    
    uint32_t crc_exp, count_hdr, pack_len;
    memcpy(&crc_exp,   addr + FULL_OFF_CRC, 4);
    memcpy(&count_hdr, addr + FULL_OFF_COUNT, 4);
    memcpy(&pack_len,  addr + FULL_OFF_PACKLEN, 4);

    if (memcmp(addr + FULL_OFF_VER, "V1.0", 4) != 0) {
        fprintf(stderr, "[load] bad version, refuse\n");
        goto out;
    }

    if ((size_t)FULL_HDR_SIZE + (size_t)pack_len + MAC_LEN != len) {
        fprintf(stderr, "[load] length mismatch: file=%zu expect=%zu (pack_len=%u)\n",
                len, (size_t)FULL_HDR_SIZE + pack_len + MAC_LEN, pack_len);
        goto out;
    }

    uint32_t crc_real = (uint32_t)crc32(0L,(const Bytef *)(addr + FULL_OFF_PACKLEN),
                        (uInt)(FULL_HDR_SIZE + pack_len - FULL_OFF_PACKLEN));

    if (crc_exp != crc_real) {
        fprintf(stderr, "[load] header/data crc mismatch: file=%08x calc=%08x\n",
                crc_exp, crc_real);
        goto out;
    }

    char *p = addr + FULL_HDR_SIZE;
    char *end = addr + FULL_HDR_SIZE + pack_len;

    static char s_key[64 * 1024] = {0}; 
    static char s_value[64 * 1024] = {0};

    uint32_t loaded = 0, bad = 0;

    // p += 16;
    while(p + 12 <= end){
        uint32_t crc_single_exp;
        memcpy(&crc_single_exp, p, 4);
        p += 4;

        uint32_t klen; uint32_t vlen;
        memcpy(&klen, p, 4);
        if (klen == 0 || (size_t)(end - p) < (size_t)4 + klen + 4 + 4) {
            fprintf(stderr, "[load] bad klen=%u at %zu, stop\n", klen, (size_t)(p - addr));
            goto out;
        }
        // memcpy(s_key, p + 4, klen);

        memcpy(&vlen, p + 4 + klen, 4);

        size_t rec_len = (size_t)4 + klen + 4 + (size_t)vlen;
        if ((size_t)(end - p) < rec_len) {
            fprintf(stderr, "[load] truncated record klen=%u vlen=%u at %zu, stop\n",
                    klen, vlen, (size_t)(p - addr));
            goto out;
        }


        uint32_t crc_single_real = crc32(0L, p, (uInt)(p + 4 + klen + 4 + vlen - p));
        
        if(crc_single_exp != crc_single_real){ 
            printf("single crc check not match\n"); 
            bad++; 
            goto out;
        }
        
        if (strncmp(server_cfg.storage_structure, "hash", 4) == 0) {
            if (kvs_hash_set(globle_hash, (char *)(p + 4), (char *)(p + 4 + klen + 4), klen, vlen) < 0) {
                fprintf(stderr, "[load] insert failed at %zu\n", (size_t)(p - addr));
                goto out;
            }
        } else {
            memcpy(s_key, p + 4, klen);
            memcpy(s_value, p + 4 + klen + 4, vlen);
            s_key[klen] = '\0';
            s_value[vlen] = '\0';
            kvs_txt_load(cname, s_key, s_value, klen, vlen);
        }
        loaded++;

        p += 4 + klen + 4 + vlen;
    }
    // fprintf(stderr, "[load] full snapshot: loaded=%u bad=%u (header count=%u)\n",
    //         loaded, bad, count_hdr);

out:
    munmap(addr, len);
    close(fd);

    if (loaded != count_hdr) {
        fprintf(stderr, "[load] count mismatch: loaded=%u header=%u\n", loaded, count_hdr);
        abort(); 
    }

    return ;
}


static int load_hmac_key(void){
    int fd = open("config/hmac.key", O_RDONLY | __O_CLOEXEC| __O_NOFOLLOW);
    if (fd < 0) { perror("hmac.key"); return -1; }
    ssize_t n = read(fd, g_hmac_key, sizeof g_hmac_key);
    close(fd);
    return (n == (ssize_t)sizeof g_hmac_key) ? 0 : -1;
}


static int add_save_flag = 0;
static pthread_t add_pid;
static pthread_t full_pid;
int main(int argc, char const *argv[]){

    load_config("config/server.conf", &server_cfg);

    if (sodium_init() < 0) { fprintf(stderr, "sodium_init failed\n"); return 1; }

    if (load_hmac_key() != 0) {
        fprintf(stderr, "[init] HMAC key missing/invalid -> refuse to start\n");
        return 1;
    }

    mallopt(M_MMAP_THRESHOLD, 128 * 1024);
    mallopt(M_TRIM_THRESHOLD, 128 * 1024);


    signal(SIGPIPE, SIG_IGN);
    memset(&add_params, 0, sizeof(add_params));
    memset(&full_params, 0, sizeof(full_params));

    u_info_pool_init();
    int add_ok = io_uring_queue_init_params(1024, &add_ring, &add_params);
    int full_ok = io_uring_queue_init_params(1024, &full_ring, &full_params);
    if (add_ok  < 0) fprintf(stderr, "[init] add_ring init error: %s\n",  strerror(-add_ok));
    if (full_ok < 0) fprintf(stderr, "[init] full_ring init error: %s\n", strerror(-full_ok));

    g_add_ring_ok  = (add_ok  == 0);
    g_full_ring_ok = (full_ok == 0);

    if(server_cfg.use_mempool == 1){
        pool = mp_create(1024 * 1024 * 1);
        cmd_pool = mp_create(64 * 1024);
    }

    init_kvs();
    char cmd[TXT_LENGTH] = {0};
    char key[TXT_LENGTH] = {0};
    char value[TXT_LENGTH] = {0};
    

    if(server_cfg.add_save == 1) kvs_add_read(cmd, key, value);
    if(server_cfg.full_save == 1) {
        kvs_full_read(cmd, key, value);
    }

    rdma_init(&gr);

    // if(server_cfg.use_mempool == 0){
    //     malloc_trim(0);
    // }
    malloc_trim(0);

    ntyco_start(&server_cfg, &gr);
    
    dest_kvs();
    mp_destory(pool);
    mp_destory(cmd_pool);

    return 0;
}