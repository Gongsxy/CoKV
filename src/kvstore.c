
#include "kvstore.h"
#include "server.h"
#include "load_conf.h"
#include "uring_slot.h"
#include "rdma.h"
#include "../libbpf-bootstrap/examples/c/resp_capture.h"

#include <signal.h>
#include <malloc.h>

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
    "SYNC","PING","QUIT","INFO","SAVE"
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
    KVS_CMD_SHUTDOWN,
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

#if JEMALLOC
void *kvs_malloc(size_t size){
    return malloc(size);
}

void kvs_free(void *ptr){
    return free(ptr);
}
#else
void *kvs_malloc(size_t size){
    return malloc(size);
};

void kvs_free(void *ptr){
    return free(ptr);
};
#endif


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
                memset(info->buffer, 0, sizeof(u_info->buffer));
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
        globle_full_fd = open("config/full.txt", O_WRONLY | O_APPEND);
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


int kvs_protocol_filter(char **tokens, char *response, int *key_len, int *value_len, int *push_flag){
    if(tokens == NULL || response == NULL) return -1;

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
            }else{
                if(server_cfg.show_response == 1) length = sprintf(response,"$%zu\r\n%s\r\n", strlen(result), result);
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
        case KVS_CMD_SAVE:{
            if(server_cfg.full_save == 1){
                int full_fd = get_full_fd();
                if (full_fd < 0) {
                    length = sprintf(response, "-ERR open full file failed\r\n");
                    break;
                }

                char *tmp = calloc(1, 1024 * 1024 * 5);
                if (!tmp) {
                    length = sprintf(response, "-ERR out of memory\r\n");
                    close(full_fd);
                    break;
                }

                int hash_length = kvs_hash_save(globle_hash, tmp);

                if (hash_length <= 0) {
                    close(full_fd);
                    free(tmp);
                    length = sprintf(response, "+OK\r\n");
                    break;
                }

                struct io_uring_sqe *sqe_full = io_uring_get_sqe(&full_ring);
                if (!sqe_full) {
                    close(full_fd);
                    free(tmp);
                    length = sprintf(response, "-ERR save busy\r\n");
                    break;
                }

                struct uring_info *full_info = calloc(1, sizeof(*full_info));
                full_info->save_buf_ptr = tmp;
                full_info->fd = full_fd;

                io_uring_prep_write(sqe_full, full_fd, full_info->save_buf_ptr, hash_length, 0);
                io_uring_sqe_set_data(sqe_full, full_info);

                int sub_ret = io_uring_submit(&full_ring);
                if (sub_ret < 0) {
                    free(full_info->save_buf_ptr);
                    free(full_info);
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
                        free(info->save_buf_ptr);
                        free(info);
                    }
                    io_uring_cqe_seen(&full_ring, cqe);
                    close(full_fd);
                } else {
                    close(full_fd);
                    free(tmp);
                    free(full_info);
                    length = sprintf(response, "-ERR save failed\r\n");
                }

                break;
            }
     
        }
        default:{
            if(server_cfg.show_response == 1) length = sprintf(response,"-ERR unknown command '%s'\r\n", tokens[0]);
            break;
        }
    }

    
    *push_flag = 0;
    return length;
};


// ok
static int sync_flag = 0;
int kvs_split_token(char *msg, char *tokens[], char *response, int length, int *key_len, int *value_len, int *push_flag){
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
                            tokens[0] = m_malloc(cmd_len + 2);
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
                        if(strcmp(cmd_tmp, "SAVE") == 0 || strcmp(cmd_tmp, "PING") == 0 || strcmp(cmd_tmp, "QUIT") == 0 || strcmp(cmd_tmp, "INFO") == 0){
                            sprintf(tokens[0], "%s", cmd_tmp);
                        }else if(strncmp(server_cfg.storage_structure, "array", 5) == 0){
                            sprintf(tokens[0], "%s", cmd_tmp);
                        }else if(strncmp(server_cfg.storage_structure, "rbtree", 6) == 0){
                            sprintf(tokens[0], "R%s", cmd_tmp);
                            i++;
                        }else if(strncmp(server_cfg.storage_structure, "hash", 4) == 0){
                            sprintf(tokens[0], "H%s", cmd_tmp);
                        }else if(strncmp(server_cfg.storage_structure, "skiptable", 9) == 0){
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
                        tokens[1] = m_malloc(*key_len + 1);
                    }else{
                        tokens[1] = malloc(*key_len + 1);
                    }
                    memcpy(tokens[1], &tcopy[i], *key_len);
                    tokens[1][*key_len] = '\0';
                    // printf("key:%s\n",tokens[1]);
                    if(tokens[1] == "SYNC"){
                        sync_flag = 1;
                    }
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
                        tokens[2] = m_malloc(*value_len + 1);
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
            
            int len = kvs_protocol_filter(tokens, response + resp_len, key_len, value_len, push_flag);
            resp_len += len;

            // mempool
            if(server_cfg.use_mempool == 1){
                m_reset();
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
            mes = m_malloc(1024 * 1024 * 5);
            if(mes) memset(mes, 0, 1024 * 1024 * 5);
        }else {
            mes = calloc(1, 1024 * 1024 * 5);
        }
        // mes = calloc(1, 1024 * 1024 * 2);
        mes_len = 0;
    }
   
    // memset(mes, 0, 1024 * 1024 * 5);
    memcpy(mes + mes_len,msg,length);

    mes_len += length;

    char *tokens[3] = {NULL, NULL, NULL};
    int count = 0;

    int total_response_len = 0;

    while (mes_len > 0) {

        int packet_len = kvs_is_complete(mes, mes_len);
        if (packet_len == 0) break; 

        char single_cmd[1024 * 64] = {0};
        memcpy(single_cmd, mes, packet_len);
        
        char single_response[1024 * 64] = {0};

        single_cmd[packet_len] = '\0';
        single_response[0] = '\0';

        char *tokens[3] = {NULL, NULL, NULL};

        int count = kvs_split_token(single_cmd, tokens, single_response, packet_len, &key_len, &value_len, &push_flag);


        if(server_cfg.add_save == 1) {
            if(load_saved_msg == 1){
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
                    memcpy(slot->buffer, single_cmd, packet_len);
                    io_uring_prep_write(sqe, fd, slot->buffer, packet_len, 0);
                    io_uring_sqe_set_data(sqe, slot);
                    io_uring_submit(&add_ring);
                }
            }
        }


        int single_len = strlen(single_response);
        if (total_response_len + single_len < BUFFER_LENGTH) {
            memcpy(response + total_response_len, single_response, single_len);
            total_response_len += single_len;
        }


        memmove(mes, mes + packet_len, mes_len - packet_len);
        mes_len -= packet_len;
    }

    return total_response_len; 

}


int init_kvs(void){
    if(strcmp(server_cfg.storage_structure, "array") == 0){
        // memset(globle_array,0,sizeof(kvs_array_t));
        globle_array = malloc(sizeof(kvs_array_t));
        kvs_array_create(globle_array);
    }else if(strcmp(server_cfg.storage_structure, "hash") == 0){
        // memset(globle_hash,0,sizeof(kvs_hash_t));
        globle_hash = malloc(sizeof(kvs_hash_t));
        kvs_hash_create(globle_hash);
    }else if(strcmp(server_cfg.storage_structure, "skiplist") == 0){
        // memset(globle_skiplist, 0, sizeof(kvs_skiplist_t));
        globle_skiplist = malloc(sizeof(kvs_skiplist_t));
        kvs_skiplist_create(globle_skiplist);
    }
    return 0;
}


void dest_kvs(void){
    if(strcmp(server_cfg.storage_structure, "array") == 0){
        kvs_array_destory(globle_array);
        free(globle_array);
    }else if(strcmp(server_cfg.storage_structure, "hash") == 0){
        kvs_hash_destory(globle_hash);
        free(globle_hash);
    }else if(strcmp(server_cfg.storage_structure, "skiplist") == 0){
        kvs_skiplist_destroy(globle_skiplist);
        free(globle_skiplist);
    }
    if (mes) { free(mes); mes = NULL; }
    close(globle_add_fd);
    close(globle_full_fd);
    m_reset();
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
    
    char *p = addr;
    char *end = addr + len;
    

    char *tmp = calloc(1, 1024 * 1024 * 5);
    int stop_save = 0;
    load_saved_msg = stop_save;

    const size_t CHUNK = 256 * 1024;
    size_t off   = 0;
    int    calls = 0;

    while (off < len) {
        size_t n = len - off;
        if (n > CHUNK) n = CHUNK;

        kvs_protocol(addr + off, (int)n, tmp);

        off += n;
        calls++;
        if ((calls % 32) == 0) fprintf(stderr, "[load] %zu / %zu bytes\n", off, len);
    }

    load_saved_msg = 1;
    munmap(addr, len);
    close(fd);
    free(tmp);
}


void kvs_full_read(char *cmd, char *key, char *value){

    int fd = open("config/full.txt", O_RDONLY);
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


    char *p = addr;
    char *end = addr + len;
    while(p < end){
        int key_len = 0, value_len = 0;
        if(strncmp(p, "key_len: ", 9) == 0){
            int n = 0;
            sscanf(p, "key_len: %d%n",&key_len, &n);
            p += n;
            while(p < end && (*p == '\r' || *p == '\n')) p++;
        }
        if(strncmp(p, "key: ", 5) == 0){
            p += 5;
            memcpy(key, p, key_len);
            p += key_len;
            while(p < end && (*p == '\r' || *p == '\n')) p++;
        }
        if(strncmp(p, "value_len: ", 11) == 0){
            int n = 0;
            sscanf(p, "value_len: %d%n",&value_len, &n);
            p += n;
            while(p < end && (*p == '\r' || *p == '\n')) p++;
        }
        if(strncmp(p, "value: ", 7) == 0){
            p += 7;
            memcpy(value, p, value_len);
            p += value_len;
            while(p < end && (*p == '\r' || *p == '\n')) p++;
        }
        if(key_len > 0 && value_len > 0){
            if(strncmp(server_cfg.storage_structure, "array", 5) == 0){
                cmd = "SET";
            }else if(strncmp(server_cfg.storage_structure, "rbtree", 6) == 0){
                cmd = "RSET";
            }else if(strncmp(server_cfg.storage_structure, "hash", 4) == 0){
                cmd = "HSET";
            }else if(strncmp(server_cfg.storage_structure, "skiptable", 9) == 0){
                cmd = "SSET";
            }
            kvs_txt_load(cmd, key, value, key_len, value_len);
        }
    }
    close(fd);
    munmap(addr, len);
}

static int add_save_flag = 0;
static pthread_t add_pid;
static pthread_t full_pid;
int main(int argc, char const *argv[]){

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

    load_config("config/server.conf", &server_cfg);
    init_kvs();
    char cmd[TXT_LENGTH] = {0};
    char key[TXT_LENGTH] = {0};
    char value[TXT_LENGTH] = {0};

    if(server_cfg.mode == 1){
        if(server_cfg.add_save == 1) kvs_add_read(cmd, key, value);
        if(server_cfg.full_save == 1) {
            kvs_full_read(cmd, key, value);
        }
        if (g_add_ring_ok){
            pthread_create(&add_pid, NULL, uring_add_cqe_thread, NULL);
        }
        rdma_init(&gr);
        if(resp_capture() < 0) return -1;
        // malloc_trim(0);
        ntyco_start(&server_cfg, &gr);
    }
    dest_kvs();
    kvs_push_destroy();
    mpool_destroy();
    return 0;
}