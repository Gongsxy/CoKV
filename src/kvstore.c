
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

const char *response[] = {

};

#define STATUS_IN	1
#define STATUS_OUT	0

#define INIT STATUS_OUT;

#define MAC_LEN crypto_auth_hmacsha256_BYTES
static unsigned char g_hmac_key[crypto_auth_hmacsha256_KEYBYTES];


static char *mes = NULL;
static char *sync_mes = NULL;
static int   sync_mes_cap = 0;
static int   sync_mes_len = 0;

void *uring_add_cqe_thread(void *argv){
    while(1){
        struct io_uring_cqe *cqe;
        int u_ret = io_uring_wait_cqe(&add_ring, &cqe);
        if(u_ret == 0){
            struct uring_info *info = io_uring_cqe_get_data(cqe);
            if(info){
                if(info->buffer){ free(info->buffer); info->buffer = NULL; }
                info->len = 0;
                u_info_free(info); 
            }

            io_uring_cqe_seen(&add_ring, cqe);
            
        }
        if (u_ret < 0) { fprintf(stderr, "[uring] %s\n", strerror(-u_ret)); continue; }
    }
}



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


int is_number(char *msg){//1 true 0 false 
	if(msg == NULL) return 0;
	int i = 0;
	int not_digit = 0;
	for(i = 0; msg[i] != '\0'; i++){
		if(!isdigit(msg[i])){
			return 0;
		}else{
			not_digit = 1;
			break;
		}
	}
	if(not_digit) return 0;
	return 1;
}

static int skip_add_log = 0;
int kvs_protocol_filter(char **tokens, char *response, int *key_len, int *value_len){
    if(tokens == NULL || response == NULL) return -1;

    skip_add_log = 0;

    int i = KVS_CMD_START;
    for(i = KVS_CMD_START; i < KVS_CMD_COUNT; i ++){
        if(strcmp(tokens[0],command[i]) == 0){
            break;
        }
    }
    if(strncmp(tokens[0], "H", 1) == 0){
        memmove(tokens[0], tokens[0] + 1, strlen(tokens[0]));
    }
    char *cmd = tokens[0];
    char *key = tokens[1];
    char *value = tokens[2];
    int length = 0;
    int ret = 0;
    // const char* filename = "a.txt";
    switch(i){

#if ENABLE_ARRAY
        case KVS_CMD_SET:{
            ret = kvs_array_set(globle_array,key,value);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(response,"-ERR %s\r\n", "unknown command");
                // printf("ERROR\r\n");
            }else{  
                if(server_cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
                // printf("EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_GET:{       
            char *result = kvs_array_get(globle_array,key);
            if(result == NULL){
                if(server_cfg.show_response == 1) length = sprintf(response,"$-1\r\n");
                // printf("NOT EXIST\r\n");
            }else{        
                if(server_cfg.show_response == 1) length = sprintf(response,"$%zu\r\n%s\r\n", strlen(result), result);
                // printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_DEL:{
            ret = kvs_array_del(globle_array,key);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(response,":1\r\n");
                // printf("OK\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_MOD:{
            ret = kvs_array_mod(globle_array,key,value);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
                // printf("OK\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(response,"-ERR no such key\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_EXIST:
            ret = kvs_array_exist(globle_array,key);
            if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(response,":1\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(response,":0\r\n");
            }
            break;

#endif
// rbtree
// #if ENABLE_RBTREE
//         case KVS_CMD_RSET:{
//             ret = kvs_rbtree_set(globle_rbtree,key,value);
//             if(ret < 0){
//                 if(cfg.show_response == 1) length = sprintf(response,"-ERR %s\r\n", "unknown command");
//             }else{
//                 if(cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
//             }
//             break;
//         }
//         case KVS_CMD_RGET:{
//             char *result = kvs_rbtree_get(globle_rbtree,key);
//             if(result == NULL){
//                 if(cfg.show_response == 1) length = sprintf(response,"$-1\r\n");
//             }else{
//                 if(cfg.show_response == 1) length = sprintf(response,"$%zu\r\n%s\r\n", strlen(result), result);
//             }
//             break;
//         }
//         case KVS_CMD_RDEL:{
//             ret = kvs_rbtree_del(globle_rbtree,key);
//             if(ret < 0){
//                 if(cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
//             }else if(ret == 0){
//                 if(cfg.show_response == 1) length = sprintf(response,":1\r\n");
//             }else{
//                 if(cfg.show_response == 1) length = sprintf(response,":0\r\n");
//             }
//             break;
//         }
//         case KVS_CMD_RMOD:{
//             ret = kvs_rbtree_mod(globle_rbtree,key,value);
//             if(ret < 0){
//                 if(cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
//             }else if(ret == 0){
//                 if(cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
//             }else{
//                 if(cfg.show_response == 1) length = sprintf(response,"-ERR no such key\r\n");
//             }
//             break;
//         }
//         case KVS_CMD_REXIST:
//             ret = kvs_rbtree_exist(globle_rbtree,key);
//             if(ret == 0){
//                 if(cfg.show_response == 1) length = sprintf(response,":1\r\n");
//             }else{
//                 if(cfg.show_response == 1) length = sprintf(response,":0\r\n");
//             }
//             break;
// #endif
// hash
#if ENABLE_HASH
        case KVS_CMD_HSET:{
            if(*value_len > KVS_MAX_VALUE){
                length = sprintf(response, "-ERR value too large\r\n");
                break;
            }

            int ret = kvs_hash_set(globle_hash,key,value, *key_len, *value_len);
            if(ret < 0){
                if(server_cfg.show_response == 1 || *response != '0') length = sprintf(response,"-ERR %s\r\n", "unknown command");
            }else{
                if(server_cfg.show_response == 1 || *response != '0') length = sprintf(response,"+OK\r\n");
            }
            break;
        }
        case KVS_CMD_HGET:{
            char *result = kvs_hash_get(globle_hash,key, *key_len, value_len);
            if(result == NULL){
                if(server_cfg.show_response == 1) length = sprintf(response,"$-1\r\n");
            }else if(*value_len > KVS_MAX_VALUE){
                length = sprintf(response, "-ERR value too large\r\n");
            }else{
                if(server_cfg.show_response == 1){
                    length  = sprintf(response, "$%d\r\n", *value_len);
                    memcpy(response + length, result, *value_len);
                    length += *value_len;
                    response[length++] = '\r';
                    response[length++] = '\n';
                }
            }
            break;
        }
        case KVS_CMD_HDEL:{
            ret = kvs_hash_del(globle_hash,key, *key_len);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(response,":1\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(response,":0\r\n");
            }
            break;
        }
        case KVS_CMD_HMOD:{
            ret = kvs_hash_mod(globle_hash,key,value, *key_len, *value_len);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(response,"-ERR no such key\r\n");
            }
            break;
        }
        case KVS_CMD_HEXIST:
            ret = kvs_hash_exist(globle_hash,key, *key_len);
            if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(response,":1\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(response,":0\r\n");
            }
            break;
#endif
// skiplist
#if ENABLE_SKIPTABLE
        case KVS_CMD_SSET:{
            ret = kvs_skiplist_set(globle_skiplist,key,value);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(response,"-ERR %s\r\n", "unknown command");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
            }
            break;
        }
        case KVS_CMD_SGET:{
            char *result = kvs_skiplist_get(globle_skiplist,key);
            if(result == NULL){
                if(server_cfg.show_response == 1) length = sprintf(response,"$-1\r\n");
            }else{          
                if(server_cfg.show_response == 1) length = sprintf(response,"$%zu\r\n%s\r\n", strlen(result), result);
            }
            break;
        }
        case KVS_CMD_SDEL:{
            ret = kvs_skiplist_del(globle_skiplist,key);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(response,":1\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(response,":0\r\n");
            }
            break;
        }
        case KVS_CMD_SMOD:{
            ret = kvs_skiplist_mod(globle_skiplist,key,value);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(response,"-ERR no such key\r\n");
            }
            break;
        }
        case KVS_CMD_SEXIST:
            ret = kvs_skiplist_exist(globle_skiplist,key);
            if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(response,":1\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(response,":0\r\n");
            }

            break;

#endif
        case KVS_CMD_PING:{
            if(server_cfg.show_response == 1) length = sprintf(response,"+PONG\r\n");
            break;
        }
        case KVS_CMD_QUIT:{
            if(server_cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
            break;
        }
        case KVS_CMD_SYNC:{

            break;
        }
        case KVS_CMD_INFO:{
            // int n = (globle_hash != NULL) ? kvs_hash_count(globle_hash) : -1;
            // length = sprintf(response, "+storage:%s count:%d hash:%p\r\n",
            //                  server_cfg.storage_structure, n, (void *)globle_hash);
            int n = (globle_hash != NULL) ? kvs_hash_count(globle_hash) : -1;
            int cap = 0, maxchain = 0, i;
            unsigned long idx_bytes = 0;

            if (globle_hash != NULL) {
                cap = globle_hash->max_slots;
                idx_bytes = (unsigned long)cap * sizeof(hashnode_t *);
                for (i = 0; i < cap; i++) {
                    int len = 0;
                    hashnode_t *nd = globle_hash->nodes[i];
                    while (nd) { len++; nd = nd->next; }
                    if (len > maxchain) maxchain = len;
                }
            }

            length = sprintf(response,
                "+storage:%s count:%d max_slots:%d avg_chain:%.2f max_chain:%d index_bytes:%lu\r\n",
                server_cfg.storage_structure, n, cap,
                (cap > 0) ? (double)n / cap : 0.0, maxchain, idx_bytes);
            break;
            break;
        }
        case KVS_CMD_SAVE:{
            
            skip_add_log = 1;

            if(server_cfg.full_save == 1){
                int full_fd = get_full_fd();
                if (full_fd < 0) {
                    length = sprintf(response, "-ERR open full file failed\r\n");
                    break;
                }
                char *tmp = NULL;
                if(server_cfg.use_mempool == 1){
                    tmp = mp_alloc(pool, 1024 * 1024 * 10 + MAC_LEN);
                    
                }else{
                    tmp = calloc(1, 1024 * 1024 * 10 + MAC_LEN);
                }
                if (!tmp) {
                    length = sprintf(response, "-ERR out of memory\r\n");
                    close(full_fd);
                    break;
                }

                uint32_t count = (uint32_t)kvs_hash_count(globle_hash);

                int hash_length = kvs_hash_save(globle_hash, tmp, 1024 * 1024 * 10, count);

                if (hash_length <= 0) {
                    close(full_fd);
                    if(server_cfg.use_mempool == 1){
                        mp_large_free(pool, tmp);
                    }else free(tmp);
                    length = sprintf(response, "+OK\r\n");
                    break;
                }

                if (crypto_auth_hmacsha256((unsigned char *)(tmp + hash_length),
                           (const unsigned char *)tmp, hash_length,
                           g_hmac_key) != 0) {
                    close(full_fd); 
                    if(server_cfg.use_mempool == 1){
                        mp_large_free(pool, tmp);
                    }else free(tmp); 
                    length = sprintf(response,"-ERR hmac failed\r\n"); 
                    break;
                }

                struct io_uring_sqe *sqe_full = io_uring_get_sqe(&full_ring);
                if (!sqe_full) {
                    close(full_fd);
                    if(server_cfg.use_mempool == 1){
                        mp_large_free(pool, tmp);
                    }else free(tmp);
                    length = sprintf(response, "-ERR save busy\r\n");
                    break;
                }

                struct uring_info *full_info = NULL;
                if(server_cfg.use_mempool == 1){
                    full_info = mp_alloc(pool, sizeof(*full_info));
                    if(full_info) memset(full_info, 0, sizeof(*full_info));
                }else{
                    full_info = calloc(1, sizeof(*full_info));
                }
                full_info->save_buf_ptr = tmp;
                full_info->fd = full_fd;

                io_uring_prep_write(sqe_full, full_fd, full_info->save_buf_ptr, hash_length + MAC_LEN, 0);
                io_uring_sqe_set_data(sqe_full, full_info);

                int sub_ret = io_uring_submit(&full_ring);
                if (sub_ret < 0) {
                    if(server_cfg.use_mempool == 1){
                        mp_large_free(pool, full_info->save_buf_ptr);
                        mp_large_free(pool, full_info);
                    }else{
                        if(server_cfg.use_mempool == 1){
                            mp_large_free(pool, tmp);
                            mp_large_free(pool, full_info);
                        }else{
                            free(tmp);
                            free(full_info);
                        }
                    }
                    close(full_fd);

                    length = sprintf(response, "-ERR submit failed\r\n");
                    break;
                }

                struct io_uring_cqe *cqe;
                int wait_ret = io_uring_wait_cqe(&full_ring, &cqe);
                if (wait_ret == 0) {
                    struct uring_info *info = io_uring_cqe_get_data(cqe);

                    if (cqe->res < 0) {
                        length = sprintf(response, "-ERR save failed\r\n");
                    } else {
                        fsync(full_fd);
                        length = sprintf(response, "+OK\r\n");
                    }

                    if (info) {
                        // free(info->save_buf_ptr);
                        if(server_cfg.use_mempool == 1){
                           mp_large_free(pool, info->save_buf_ptr); 
                           mp_large_free(pool, info);
                        }else {
                            free(info->save_buf_ptr);
                            free(info);
                        }
                    }
                    io_uring_cqe_seen(&full_ring, cqe);
                    close(full_fd);
                } else {
                    close(full_fd);
                    if (server_cfg.use_mempool == 1) {
                        mp_large_free(pool, tmp);
                    } else {
                        free(tmp);
                        free(full_info);
                    }
                    length = sprintf(response, "-ERR save failed\r\n");
                }

                break;
            }
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

            length = ok ? sprintf(response, "+OK\r\n") : sprintf(response, "-ERR clear failed\r\n");
            break;
        }

        default:{
            if(server_cfg.show_response == 1) length = sprintf(response,"-ERR unknown command '%s'\r\n", tokens[0]);
            break;
        }
    }

    return length;
};


// ok
static int sync_flag = 0;
int kvs_split_token(char *msg, char *tokens[], char *response, int length, int *key_len, int *value_len, int *resp_total){
    if(msg == NULL || tokens == NULL) return -1;
    char *tcopy = msg;
    int idx = 0;
    
    int origin_status = 1;
    int cmd_len_before = 0;

    int finish_single = 0;
    int single_num = 0;
    int skip = 1;

    int i = 0;
    int status = 0;
    status = CMD;
    char tmp[32] = {0};
    int resp_len = 0;
    while(i < length){

        int move_flag = 1;
        switch(status){
            case CMD:{//OK

                if(origin_status){   

                    if(skip){

                        if(tcopy[i] != '*'){
                            break;
                        }else{//OK

                            i++;
                            while(tcopy[i] != '\r'){
                                char ch_buf[2] = {tcopy[i], '\0'};
                                strcat(tmp,ch_buf);
                                i++;
                            }
                            single_num = atoi(tmp);
                            // printf("single_num:%d\n",single_num);
                            if(tcopy[i] == '\r'){
                                i += 2;
                            }
                            memset(tmp, 0, 32);
                            skip = 0;
                            move_flag = 0;
                            break;
                        }
                    }
                    
                    if(!skip){

                        if(tcopy[i] != '$'){
                            break;
                        }else{
                            cmd_len_before = 1;
                            origin_status = 0;
                            skip = 1;
                            i++;
                            move_flag = 0;
                            break;
                        }
                    }

                }
                // $--> num
                if(cmd_len_before){

                    if(tcopy[i] != '\r'){
                        if(i >= length){
                            return 0;
                        }
                        char ch_buf[2] = {tcopy[i], '\0'};
                        strcat(tmp, ch_buf);// 3
                        break;
                    }else{
                        
                        int cmd_len = atoi(tmp);
                        // printf("cmd_len:%d\n",cmd_len);
                        if (tcopy[i] == '\r' && tcopy[i + 1] == '\n') i++;
                        i++;
                        if(i + cmd_len > length){

                            return 0;
                        }
                        // mempool
                        if(server_cfg.use_mempool == 1){
                            tokens[0] = mp_alloc(cmd_pool, cmd_len + 2);
                        }else{
                            tokens[0] = malloc(cmd_len + 2);
                        }
                        char cmd_tmp[128] = {0};
                        memcpy(cmd_tmp, &tcopy[i], cmd_len);
                        if (cmd_tmp) {
                            for(int k = 0; cmd_tmp[k]; k++) {
                                cmd_tmp[k] = toupper(cmd_tmp[k]);
                            }
                        }
                        if(strcmp(cmd_tmp, "SAVE") == 0 || strcmp(cmd_tmp, "PING") == 0 || strcmp(cmd_tmp, "QUIT") == 0 || strcmp(cmd_tmp, "INFO") == 0 || strcmp(cmd_tmp, "CLEAR") == 0){
                            sprintf(tokens[0], "%s", cmd_tmp);
                        }else if(strncmp(server_cfg.storage_structure, "array", 5) == 0){
                            sprintf(tokens[0], "%s", cmd_tmp);
                        }else if(strncmp(server_cfg.storage_structure, "rbtree", 6) == 0){
                            sprintf(tokens[0], "R%s", cmd_tmp);
                            i++;
                        }else if(strncmp(server_cfg.storage_structure, "hash", 4) == 0){
                            sprintf(tokens[0], "H%s", cmd_tmp);
                        }else if(strncmp(server_cfg.storage_structure, "skiplist", 8) == 0){
                            sprintf(tokens[0], "S%s", cmd_tmp);
                        }

                        i = i + cmd_len;
                        i += 2;
                        status = KEY;
                        finish_single++;
                        // idx++;
                        memset(tmp, 0, 32);

                        origin_status = 1;
                        move_flag = 0;
                        break;
                    }
                }
                break;
            }
            case KEY:{
                
                int len = 0;
                if(tcopy[i] == '$') {
                    len = 1;
                    i++;
                    move_flag = 0;
                }
                if(len){

                    while(tcopy[i] != '\r'){
                        if(i >= length){
                            return 0;
                        }
                        char ch_buf[2] = {tcopy[i], '\0'};
                        strcat(tmp, ch_buf);
                        i++;
                    }
                }

                if(tcopy[i] == '\r'){
                    
                    i += 2;
                    *key_len = atoi(tmp);
                    if(i + *key_len > length){
                        return 0; 
                    }
                    // mempoll
                    if(server_cfg.use_mempool == 1){
                        tokens[1] = mp_alloc(cmd_pool, *key_len + 1);
                    }else{
                        tokens[1] = malloc(*key_len + 1);
                    }
                    memcpy(tokens[1], &tcopy[i], *key_len);
                    tokens[1][*key_len] = '\0';
                    // printf("key:%s\n",tokens[1]);
                    
                    memset(tmp, 0, 32);
                    i = i + *key_len;
                    
                    i += 2;

                    finish_single++;
                    if(finish_single == single_num){
                        status = CMD;
                    }else{
                        status = VALUE;
                    }
                    move_flag = 0;
                    break;
                }

                break;
            }
            case VALUE:{

                int len = 0;
                if(tcopy[i] == '$') {
                    len = 1;
                    i++;
                    move_flag = 0;
                }
                if(len){
                    while(tcopy[i] != '\r'){
                        if(i >= length){
                            return 0;
                        }
                        char ch_buf[2] = {tcopy[i], '\0'};
                        strcat(tmp, ch_buf);
                        i++;
                    }
                    
                }

                if(tcopy[i] == '\r'){

                    i += 2;
                    *value_len = atoi(tmp);
                    if(i + *value_len > length){
                        // stop = 3;
                        // save_i=i;
                        return 0;
                    }

                    // mempool
                    if(server_cfg.use_mempool == 1){
                        tokens[2] = mp_alloc(cmd_pool, *value_len + 1);
                    }else{
                        tokens[2] = malloc(*value_len + 1);
                    }

                    memcpy(tokens[2], &tcopy[i], *value_len );
                    tokens[2][*value_len] = '\0';
                    // printf("value:%s\n",tokens[2]);
                    memset(tmp, 0, 32);
                    i = i + *value_len;
                    i += 2;
                    move_flag = 0;
                    // idx++;
                    status = CMD;
                    finish_single++;
                    break;

                }
                break;
            }
        }
        //
        
        if(single_num > 0 && finish_single == single_num){
            
            int len = kvs_protocol_filter(tokens, response + resp_len, key_len, value_len);
            resp_len += len;

            // mempool
            if(server_cfg.use_mempool == 1){
                mp_reset(cmd_pool);
            }else{
                if(tokens[0]) free(tokens[0]);
                if(tokens[1]) free(tokens[1]);
                if(tokens[2]) free(tokens[2]);
            }
            tokens[0] = NULL;
            tokens[1] = NULL;
            tokens[2] = NULL;

            move_flag = 0;
            finish_single = 0;
            single_num = 0;
            cmd_len_before = 0;

            origin_status = 1;
            skip = 1;
            status = CMD;

        } 
        if(move_flag){
            i++;
        }

    }
    if(resp_total) *resp_total = resp_len;
    return i;
}


// ok
int kvs_is_complete(char *msg, int length){

    if (length < 4 || msg[0] != '*') return 0;
    
    int j = 1;
    char tbuf[128] = {0};
    while (j < length && msg[j] != '\r') {
        tbuf[j-1] = msg[j];
        j++;
    }
    if (j + 1 >= length || msg[j] != '\r' || msg[j+1] != '\n') return 0;
    
    int count = atoi(tbuf);
    j += 2;
    
    for (int k = 0; k < count; k++) {
        if (j >= length || msg[j] != '$') return 0;
        j++;
        
        char lenbuf[128] = {0};
        int li = 0;
        while (j < length && msg[j] != '\r') {
            lenbuf[li++] = msg[j++];
        }
        if (j + 1 >= length || msg[j] != '\r' || msg[j+1] != '\n') return 0;
        int slen = atoi(lenbuf);
        j += 2;
        
        if (j + slen + 2 > length) return 0;
        j += slen + 2;
    }
    return j;
}


int kvs_protocol(char *msg, int length, char *response){ 

    int key_len = 0;
    int value_len = 0;
    int push_flag = 0;
    int packet_len = 0;

    static int mes_len = 0;

    if (mes == NULL) {
        if(server_cfg.use_mempool == 1){
            mes = mp_alloc(pool, 1024 * 1024 * 5);
            if(mes) memset(mes, 0, 1024 * 1024 * 5);
        }else {
            mes = calloc(1, 1024 * 1024 * 5);
        }
        mes_len = 0;
    }
   
    memcpy(mes + mes_len,msg,length);

    mes_len += length;

    char *tokens[3] = {NULL, NULL, NULL};
    int count = 0;

    int total_response_len = 0;

    while (mes_len > 0) {

        int packet_len = kvs_is_complete(mes, mes_len);
        if (packet_len == 0) break; 

        static char single_cmd[128 * 1024] = {0};
        memcpy(single_cmd, mes, packet_len);

        uint32_t crc_ret = (uint32_t)crc32(0L, single_cmd, packet_len);
        

        static char single_response[1024 * 1024 * 1] = {0};

        single_cmd[packet_len] = '\0';
        single_response[0] = '\0';

        char *tokens[3] = {NULL, NULL, NULL};

        int resp_used = 0;
        int count = kvs_split_token(single_cmd, tokens, single_response, packet_len, &key_len, &value_len, &resp_used);


        if(server_cfg.add_save == 1) {
            if(load_saved_msg == 1 && skip_add_log == 0){
                int fd = get_add_fd();
                struct io_uring_sqe *sqe = io_uring_get_sqe(&add_ring);

                if (!sqe) {
                    fprintf(stderr, "add_ring sqe unavailable, skip add persistence\n");
                } else {
                    uring_info *slot = u_info_alloc();
                    if (!slot) {

                        do {
                            io_uring_submit(&add_ring);
                            struct io_uring_cqe *cqe;
                            if (io_uring_wait_cqe(&add_ring, &cqe) == 0) {
                                struct uring_info *done = io_uring_cqe_get_data(cqe);
                                if (done) u_info_free(done);
                                io_uring_cqe_seen(&add_ring, cqe);
                            }
                            slot = u_info_alloc();
                        } while (!slot);
                    }

                    slot->fd = fd;
                    slot->len = packet_len + 4;
                    slot->buffer = malloc(packet_len + 4);
                    if (!slot->buffer) {
                        u_info_free(slot);
                        fprintf(stderr, "[add] oom, skip log\n");
                    } else {
                        memcpy(slot->buffer, &crc_ret, 4);
                        memcpy(slot->buffer + 4, single_cmd, packet_len);
                        io_uring_prep_write(sqe, fd, slot->buffer, packet_len + 4, 0);
                        io_uring_sqe_set_data(sqe, slot);
                        io_uring_submit(&add_ring);
                    }
                }
            }
        }


        if (total_response_len + resp_used < BUFFER_LENGTH) {
            if(response != NULL){
                memcpy(response + total_response_len, single_response, resp_used);
            }
            total_response_len += resp_used;
        }else {
            // fprintf(stderr, "[proto] drop reply: used=%d resp=%d limit=%d\n",
            //         total_response_len, resp_used, BUFFER_LENGTH);
        }


        memmove(mes, mes + packet_len, mes_len - packet_len);
        mes_len -= packet_len;
    }
    return total_response_len; 
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


void kvs_txt_load(char *cmd, char *key, char *value, int key_len, int value_len){
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
        perror("mmap error");
        return ;
    }

    
    char *tmp = NULL;
    if(server_cfg.use_mempool == 1){
        tmp = mp_alloc(pool, 1024 * 1024 * 5);
    }else{
        tmp = calloc(1, 1024 * 1024 * 5);
    }

    load_saved_msg = 0;

    char *p = addr;
    char *end = addr + len;
    int ok = 0; int bad = 0;

    while (p + 4 <= end) {
        uint32_t crc_exp;
        memcpy(&crc_exp, p, 4);

        int plen = kvs_is_complete(p + 4, (int)(end - p - 4));
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

        kvs_protocol(p + 4, plen, NULL);
        p += 4 + plen;
        ok++;
    }

    // fprintf(stderr, "[load] aof replay: ok=%d bad=%d, consumed %zu / %zu bytes\n",
    //         ok, bad, (size_t)(p - addr), len);

    load_saved_msg = 1;
    munmap(addr, len);
    close(fd);
    if(server_cfg.use_mempool == 1) mp_large_free(pool, tmp);
    else free(tmp);
}


#define FULL_HDR_SIZE  16
#define FULL_OFF_CRC     0
#define FULL_OFF_VER     8
#define FULL_OFF_COUNT   12
#define FULL_OFF_PACKLEN 4

void kvs_full_read(char *cmd, char *key, char *value){

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
        goto out;                    /* 拒绝整份，保持当前数据不变 */
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
        if (klen == 0 || klen >= sizeof(s_key) || (size_t)(end - p) < (size_t)4 + klen + 4 + 4) {
            fprintf(stderr, "[load] bad klen=%u at %zu, stop\n", klen, (size_t)(p - addr));
            goto out;
        }
        memcpy(s_key, p + 4, klen);

        memcpy(&vlen, p + 4 + klen, 4);
        if (vlen >= sizeof(s_value) || p + 4 + klen + 4 + vlen > end) {
            fprintf(stderr, "[load] bad vlen=%u at %zu, stop\n", vlen, (size_t)(p - addr));
            goto out;
        }
        memcpy(s_value, p + 4 + klen + 4, vlen);
        
        s_key[klen] = '\0';
        s_value[vlen] = '\0';

        uint32_t crc_single_real = crc32(0L, p, (uInt)(p + 4 + klen + 4 + vlen - p));
        
        if(crc_single_exp != crc_single_real){ printf("single crc check not match\n"); bad++; goto out;}
        
        kvs_txt_load(cname, s_key, s_value, klen, vlen);
        loaded++;

        p += 4 + klen + 4 + vlen;
    }
    // fprintf(stderr, "[load] full snapshot: loaded=%u bad=%u (header count=%u)\n",
    //         loaded, bad, count_hdr);

    if (loaded != count_hdr)
        fprintf(stderr, "[load] count mismatch: loaded=%u header=%u\n", loaded, count_hdr);
out:
    munmap(addr, len);
    close(fd);
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
    if (g_add_ring_ok){
        pthread_create(&add_pid, NULL, uring_add_cqe_thread, NULL);
    }
    rdma_init(&gr);
    if(resp_capture() < 0) return -1;
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