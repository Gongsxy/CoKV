

#include "kvstore.h"
#include "server.h"
#include "load_conf.h"

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
    "SYNC","PING","QUIT","INFO"
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
    KVS_CMD_COUNT
};

enum {
    CMD,
    KEY,
    VALUE
};

static Config cfg;

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

// #define ADDSTORAGE      add_save_flag

// ok
int kvs_protocol_filter(char **tokens, char *response, int *key_len, int *value_len, int *push_flag){
    if(tokens == NULL || response == NULL) return -1;
    
    // printf("tokens:%s\n%s\n%s\n",tokens[0], tokens[1], tokens[2]);
    int i = KVS_CMD_START;
    for(i = KVS_CMD_START; i < KVS_CMD_COUNT; i ++){
        if(strcmp(tokens[0],command[i]) == 0){
            break;
        }
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
            if(cfg.add_save == 1) {
                kvs_txt_write(cmd, key, value, key_len, value_len);
            }
            if(ret < 0){
                if(cfg.show_response == 1) length = sprintf(response,"-ERR %s\r\n", "unknown command");
                // printf("ERROR\r\n");
            }else{  
                if(cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
                // printf("EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_GET:{       
            if(cfg.add_save == 1) kvs_txt_get(cmd, key, key_len);
#if FULLSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif 
            char *result = kvs_array_get(globle_array,key);
            if(result == NULL){
                if(cfg.show_response == 1) length = sprintf(response,"$-1\r\n");
                // printf("NOT EXIST\r\n");
            }else{        
                if(cfg.show_response == 1) length = sprintf(response,"$%zu\r\n%s\r\n", strlen(result), result);
                // printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_DEL:{
            ret = kvs_array_del(globle_array,key);
            if(cfg.add_save == 1) kvs_txt_del(cmd, key, key_len);
#if FULLSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
            if(ret < 0){
                if(cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                if(cfg.show_response == 1) length = sprintf(response,":1\r\n");
                // printf("OK\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_MOD:{
            ret = kvs_array_mod(globle_array,key,value);
            if(cfg.add_save == 1) kvs_txt_mod(cmd, key, value, key_len, value_len);

#if FULLSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                if(cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                if(cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
                // printf("OK\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,"-ERR no such key\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_EXIST:
            ret = kvs_array_exist(globle_array,key);

            kvs_txt_exist(cmd, key, key_len);
            if(ret == 0){
                if(cfg.show_response == 1) length = sprintf(response,":1\r\n");
                // printf("EXIST\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;

#endif
// rbtree
#if ENABLE_RBTREE
        case KVS_CMD_RSET:{
            ret = kvs_rbtree_set(globle_rbtree,key,value);
            if(cfg.add_save == 1) kvs_txt_write(cmd, key, value, key_len, value_len);
#if FULLSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif

            if(ret < 0){
                if(cfg.show_response == 1) length = sprintf(response,"-ERR %s\r\n", "unknown command");
                // printf("ERROR\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
                // printf("EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_RGET:{
            char *result = kvs_rbtree_get(globle_rbtree,key);
            if(cfg.add_save == 1){
                kvs_txt_get(cmd, key, key_len);
                free(value);
            }
#if FULLSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif
            if(result == NULL){
                if(cfg.show_response == 1) length = sprintf(response,"$-1\r\n");
                // printf("NOT EXIST\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,"$%zu\r\n%s\r\n", strlen(result), result);
                // printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_RDEL:{
            ret = kvs_rbtree_del(globle_rbtree,key);
            if(cfg.add_save == 1) kvs_txt_del(cmd, key, key_len);
#if FULLSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
            if(ret < 0){
                if(cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                if(cfg.show_response == 1) length = sprintf(response,":1\r\n");
                // printf("OK\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_RMOD:{
            if(cfg.add_save == 1) kvs_txt_mod(cmd, key, value, key_len, value_len);
#if FULLSTORAGE
            kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
            ret = kvs_rbtree_mod(globle_rbtree,key,value);
            if(ret < 0){
                if(cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                if(cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
                // printf("OK\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,"-ERR no such key\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_REXIST:
            ret = kvs_rbtree_exist(globle_rbtree,key);
            kvs_txt_exist(cmd, key, key_len);
            if(ret == 0){
                if(cfg.show_response == 1) length = sprintf(response,":1\r\n");
                // printf("EXIST\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
#endif
// hash
#if ENABLE_HASH
        case KVS_CMD_HSET:{
            int ret = kvs_hash_set(globle_hash,key,value);
            if(cfg.add_save == 1) kvs_txt_write(cmd, key, value, key_len, value_len);
#if FULLSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                if(cfg.show_response == 1) length = sprintf(response,"-ERR %s\r\n", "unknown command");
                // printf("ERROR\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
                // printf("EXIST\r\n");/
            }
            break;
        }
        case KVS_CMD_HGET:{
            char *result = kvs_hash_get(globle_hash,key);
            if(cfg.add_save == 1){
                kvs_txt_get(cmd, key, key_len);
                free(value); 
            }
#if FULLSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif
            if(result == NULL){
                if(cfg.show_response == 1) length = sprintf(response,"$-1\r\n");
                // printf("NOT EXIST\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,"$%zu\r\n%s\r\n", strlen(result), result);
                // printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_HDEL:{
            ret = kvs_hash_del(globle_hash,key);
            if(cfg.add_save == 1) kvs_txt_del(cmd, key, key_len);
#if FULLSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
            if(ret < 0){
                if(cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                if(cfg.show_response == 1) length = sprintf(response,":1\r\n");
                // printf("OK\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_HMOD:{
            ret = kvs_hash_mod(globle_hash,key,value);
            if(cfg.add_save == 1) kvs_txt_mod(cmd, key, value, key_len, value_len);
#if FULLSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                if(cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                if(cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
                // printf("OK\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,"-ERR no such key\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_HEXIST:
            ret = kvs_hash_exist(globle_hash,key);
            kvs_txt_exist(cmd, key, key_len);
            if(ret == 0){
                if(cfg.show_response == 1) length = sprintf(response,":1\r\n");
                // printf("EXIST\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
#endif
// skiplist
#if ENABLE_SKIPTABLE
        case KVS_CMD_SSET:{
            ret = kvs_skiplist_set(globle_skiplist,key,value);
            if(cfg.add_save == 1) kvs_txt_write(cmd, key, value, key_len, value_len);
#if FULLSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                if(cfg.show_response == 1) length = sprintf(response,"-ERR %s\r\n", "unknown command");
                // printf("ERROR\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
                // printf("EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_SGET:{
            char *result = kvs_skiplist_get(globle_skiplist,key);\
            if(cfg.add_save == 1){
                kvs_txt_get(cmd, key, key_len);
                free(value);
            }

#if FULLSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif  
            if(result == NULL){
                if(cfg.show_response == 1) length = sprintf(response,"$-1\r\n");
                // printf("NOT EXIST\r\n");
            }else{          
                if(cfg.show_response == 1) length = sprintf(response,"$%zu\r\n%s\r\n", strlen(result), result);
                // printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_SDEL:{
            ret = kvs_skiplist_del(globle_skiplist,key);
            if(cfg.add_save == 1) kvs_txt_del(cmd, key, key_len);

#if FULLSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
            if(ret < 0){
                if(cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                if(cfg.show_response == 1) length = sprintf(response,":1\r\n");
                // printf("OK\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_SMOD:{
            ret = kvs_skiplist_mod(globle_skiplist,key,value);
            if(cfg.add_save == 1) kvs_txt_mod(cmd, key, value, key_len, value_len);

#if FULLSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                if(cfg.show_response == 1) length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                if(cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
                // printf("OK\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,"-ERR no such key\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_SEXIST:
            ret = kvs_skiplist_exist(globle_skiplist,key);

            if(cfg.add_save == 1) kvs_txt_exist(cmd, key, key_len);

            if(ret == 0){
                if(cfg.show_response == 1) length = sprintf(response,":1\r\n");
                // printf("EXIST\r\n");
            }else{
                if(cfg.show_response == 1) length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }

            break;

#endif
        case KVS_CMD_PING:{
            if(cfg.show_response == 1) length = sprintf(response,"+PONG\r\n");
            break;
        }
        case KVS_CMD_QUIT:{
            if(cfg.show_response == 1) length = sprintf(response,"+OK\r\n");
            break;
        }
        case KVS_CMD_SYNC:{
            

            // // ½áÊø±ê¼Ç
            // offset += snprintf(response + offset, remain - offset, "$4\r\nDONE\r\n");
            // length = offset;
            break;
        }
        default:{
            if(cfg.show_response == 1) length = sprintf(response,"-ERR unknown command '%s'\r\n", tokens[0]);
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
                        if(cfg.use_mempool == 1){
                            tokens[0] = m_malloc(cmd_len + 1);
                        }else{
                            tokens[0] = malloc(cmd_len + 1);
                        }
                        memcpy(tokens[0], &tcopy[i], cmd_len);
                        // if (tokens[0]) {
                        //     for(int k = 0; tokens[0][k]; k++) {
                        //         tokens[0][k] = toupper(tokens[0][k]);
                        //     }
                        // }
                        tokens[0][cmd_len] = '\0';
                        // printf("cmd:%s\n",tokens[0]);
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
                    if(cfg.use_mempool == 1){
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
                    if(cfg.use_mempool == 1){
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
        // printf("enter");
        
        if(single_num > 0 && finish_single == single_num){
            // printf("enter\n");
            // for(int k = 0; tokens[0][k]; k++){
            //     tokens[0][k] = toupper(tokens[0][k]);
            // }

            int len = kvs_protocol_filter(tokens, response + resp_len, key_len, value_len, push_flag);
            resp_len += len;

            // mempool
            if(cfg.use_mempool == 1){
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


// ok
void sync_mes_init(void) {
    if (sync_mes != NULL) return;  
    
    sync_mes_cap = 1024 * 1024 * 5;
    if(cfg.use_mempool == 1){
        sync_mes = m_malloc(sync_mes_cap);
    }else {
        sync_mes = calloc(1, sync_mes_cap);
    }
}


// ok
int kvs_protocol(char *msg, int length, char *response){

    int key_len = 0;
    int value_len = 0;
    int push_flag = 0;
    int packet_len = 0;

    static int mes_len = 0;

    if (mes == NULL) {
        if(cfg.use_mempool == 1){
            mes = m_malloc(1024 * 1024 * 7);
            if(mes) memset(mes, 0, 1024 * 1024 * 7);
        }else {
            mes = calloc(1, 1024 * 1024 * 7);
        }
        // mes = calloc(1, 1024 * 1024 * 5);
        mes_len = 0;
    }
    
    if(strncmp(msg, "+", 1) == 0) {

        int i = 0, j = 0, in = 0;
        while(msg[i]){
            if(msg[i] == '*') {
                msg[j] = msg[i];
                in ++;
                j++;
            }
            i++; 
        }
        msg[j] = '\0'; 
        if(in == 0) return 0;
    }

    if(cfg.full_save_flag == 1){
        if(strcmp(msg, "save\n") == 0){

            char *tmp = NULL;
            if(cfg.use_mempool == 1){
                tmp = m_malloc(1024 * 1024 * 5);
                if(tmp) memset(tmp, 0, 1024 * 1024 * 5);
            }else {
                tmp = calloc(1, 1024 * 1024 * 5);
            }
            
            FILE *fp_full = fopen("config/full.txt", "a+");

            kvs_array_save(globle_array, tmp);
            fputs(tmp, fp_full);
            fflush(fp_full);
            memset(tmp, 0, 1024 * 1024 * 5);

            kvs_skiplist_save(globle_skiplist, tmp);
            fputs(tmp, fp_full);
            fflush(fp_full);
            memset(tmp, 0, 1024 * 1024 * 5);

            int rbtree_length = kvs_rbtree_save(globle_rbtree, tmp);
            fputs(tmp, fp_full);
            fflush(fp_full);
            memset(tmp, 0, 1024 * 1024 * 5);

            int hash_length = kvs_hash_save(globle_hash, tmp);
            fputs(tmp, fp_full);
            fflush(fp_full);
            memset(tmp, 0, 1024 * 1024 * 5);


            fclose(fp_full);

            kvs_protocol_push(msg, strlen(msg));
            if(cfg.use_mempool == 1){
                m_reset();
            }
            return 0;
        }

    }


    if(strncmp(msg, "SYNC\n", 5) == 0){
        mes_len = 0;
        sync_mes_init();
        FILE *fp = NULL;
        if(cfg.add_save == 1){
            fp = fopen("add.txt", "r");
        }else if(cfg.full_save == 1){
            fp = fopen("full.txt", "r");
        }

        if (!fp) {
            printf( "$4\r\nERROR\r\n");
            return 0;
        }
        
        char line[1024];
        char cmd[1024] = {0};
        char key[1024] = {0};
        char value[1024] = {0};
        int has_key = 0, has_cmd = 0, has_value = 0;
        int key_len = 0, value_len = 0;
        memset(sync_mes, 0, sizeof(sync_mes));
        while (fgets(line, sizeof(line), fp)) {

            // line[strcspn(line, "\r\n")] = '\0';
            
            if (strncmp(line, "cmd: ", 5) == 0) {
                sscanf(line, "cmd: %s\r\n", cmd);
                has_cmd = 1;
            }else if (strncmp(line, "key_len: ", 9) == 0) {
                sscanf(line, "key_len: %d\r\n", &key_len);
            }else if (strncmp(line, "key: ", 5) == 0) {
                strncpy(key, line + 5, key_len);
                key[key_len] = '\0';
                // printf("key:%s", key);
                has_key = 1;
            }else if (strncmp(line, "value_len: ", 11) == 0) {
                sscanf(line, "value_len: %d\r\n", &value_len);
            }else if (strncmp(line, "value: ", 7) == 0) {
                strncpy(value, line + 7, value_len);
                value[value_len] = '\0';
                // printf("value:%s", value);s
                has_value = 1;
            }
            
            int i = 0;
            while(i < 8){
                if(strcmp(cmd, key_value_command[i]) == 0){
                    i = 10;
                    break;
                }
            }
            if(i = 10){
                if (has_value == 1) {
                    char resp[4096];
                    int n = snprintf(resp, 4096,"*3\r\n$3\r\n%s\r\n$%d\r\n%s\r\n$%d\r\n%s\r\n",cmd,key_len, key, value_len, value);
                    resp[n] = '\0';
                    // printf("push_msg:%s\n", resp);
                    // kvs_protocol_push(resp);
                    if (sync_mes_len + n < sync_mes_cap) {
                        memcpy(sync_mes + sync_mes_len, resp, n);
                        sync_mes_len += n;
                    }
                    
                    has_cmd = 0;
                    has_key = 0;
                    has_value = 0;
                    key_len = 0;
                    value_len = 0;
                    cmd[0] = key[0] = value[0] = '\0';
                }
            }else{
                if (has_key == 1) {
                    char resp[4096];
                    int n = snprintf(resp, sizeof(resp),"*2\r\n$3\r\n%s\r\n$%ld\r\n%s\r\n",cmd, strlen(key), key);
                    
                    resp[n] = '\0';
                    strcat(sync_mes, resp);
                    
                    has_cmd = 0;
                    has_key = 0;
                    has_value = 0;
                    key_len = 0;
                    cmd[0] = key[0] = '\0';
                }
            }
        }
        
        fclose(fp);
        kvs_protocol_push(sync_mes, sync_mes_len);
        memset(sync_mes, 0, 1024 * 1024 * 9);
        sync_mes_len = 0;
        // memset(mes, 0, 1024 * 1024 * 9);
        mes_len = 0;
        printf("DONE\r\n");
        return 0;
    }


    memset(response, 0, BUFFER_LENGTH);
    memcpy(mes + mes_len,msg,length);

    mes_len += length;

    char *tokens[3] = {NULL, NULL, NULL};
    int count = 0;

    int total_response_len = 0;

    while (mes_len > 0) {

        int packet_len = kvs_is_complete(mes, mes_len);
        
        // if (packet_len == 0) break;  
        if (packet_len == 0) break; 
        char single_cmd[4096] = {0};
        memcpy(single_cmd, mes, packet_len);
        single_cmd[packet_len] = '\0';

        char single_response[BUFFER_LENGTH] = {0};
        char *tokens[3] = {NULL, NULL, NULL};

        // printf("single_cmd:%s\n", single_cmd);
        int count = kvs_split_token(single_cmd, tokens, single_response, packet_len, &key_len, &value_len, &push_flag);
        if(cfg.push_flag){
            kvs_protocol_push(single_cmd, count);
        }


        int single_len = strlen(single_response);
        if (total_response_len + single_len < BUFFER_LENGTH) {
            memcpy(response + total_response_len, single_response, single_len);
            total_response_len += single_len;
        }


        memmove(mes, mes + packet_len, mes_len - packet_len);
        mes_len -= packet_len;
    }
    if(mes_len == 0) memset(mes, 0, 1024 * 1024 * 7);

    return total_response_len; 
    
    // return count;
}


// mempool
int init_kvs(void){
#if ENABLE_ARRAY
    // memset(globle_array,0,sizeof(kvs_array_t));
    globle_array = malloc(sizeof(kvs_array_t));
    kvs_array_create(globle_array);
#endif

#if ENABLE_RBTREE
    // memset(globle_rbtree,0,sizeof(kvs_rbtree_t));
    globle_rbtree = malloc(sizeof(kvs_rbtree_t));
    kvs_rbtree_create(globle_rbtree);
#endif

#if ENABLE_HASH
    // memset(globle_hash,0,sizeof(kvs_hash_t));
    globle_hash = malloc(sizeof(kvs_hash_t));
    kvs_hash_create(globle_hash);
#endif

#if ENABLE_SKIPTABLE
    // memset(globle_skiplist, 0, sizeof(kvs_skiplist_t));
    globle_skiplist = malloc(sizeof(kvs_skiplist_t));
    kvs_skiplist_create(globle_skiplist);
#endif
    return 0;
}


// mempool
void dest_kvs(void){
#if ENABLE_ARRAY
free(globle_array);
kvs_array_destory(globle_array);

#endif

#if ENABLE_RBTREE
free(globle_rbtree);
kvs_rbtree_destory(globle_rbtree);

#endif

#if ENABLE_HASH
free(globle_hash);
kvs_hash_destory(globle_hash);

#endif

#if ENABLE_SKIPTABLE
free(globle_skiplist);
kvs_skiplist_destroy(globle_skiplist);

#endif
}


// ok
int origin_size = 0;
static char *old_mes = NULL;
int save_old_mes(char *cmd, char *key, char *value){
    // FILE *fp = fopen("sync.log", "w");
    if(key == NULL || value == NULL) return -1;
    char mes[4096] = {0};
    int length = 0;
    if(key != NULL && value != NULL && strlen(key) > 0 && strlen(value) > 0){
        length = sprintf(mes, "key:%s---value:%s\r\n", key, value);
    }


    static int total_old_length = 0;
    total_old_length += length;
    if(old_mes == NULL){
        origin_size = 5 * length;
        old_mes = malloc(origin_size);
        old_mes[0] = '\0';
    }else if(total_old_length >= origin_size){
        origin_size *= 2;
        old_mes = realloc(old_mes, origin_size);
    }

    strcat(old_mes, mes);
    return 0;
}

// ok
int write_old_mes(void){
    if(old_mes == NULL) return 0;
    FILE *fp = fopen("./config/sync.log", "w");
    if(fp == NULL)
    {
        perror("fopen sync.log failed");
        return -1;
    }
    fputs(old_mes, fp);
    fflush(fp);
    fclose(fp);

    old_mes = NULL;
    
    return 0;
}

// ok
void kvs_txt_load(char *cmd, char *key, char *value){
    // printf("Lcmd:%s,key:%s,value:%s\n",cmd, key, value);
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
#if ENABLE_RBTREE
    if(strcmp(cmd,"RSET") == 0){
        kvs_rbtree_set(globle_rbtree, key, value);
    }else if(strcmp(cmd,"RDEL") == 0){
        kvs_rbtree_del(globle_rbtree, key);
    }else if(strcmp(cmd,"RMOD") == 0){
        kvs_rbtree_mod(globle_rbtree, key, value);
    }else if(strcmp(cmd,"RGET") == 0){
        kvs_rbtree_get(globle_rbtree, key);
    }else if(strcmp(cmd,"REXIST") == 0){
        kvs_rbtree_exist(globle_rbtree, key);
    }
#endif
#if ENABLE_HASH
    if(strcmp(cmd,"HSET") == 0){
        kvs_hash_set(globle_hash, key, value);
    }else if(strcmp(cmd,"HDEL") == 0){
        kvs_hash_del(globle_hash, key);
    }else if(strcmp(cmd,"HMOD") == 0){
        kvs_hash_mod(globle_hash, key, value);
    }else if(strcmp(cmd,"HGET") == 0){
        kvs_hash_get(globle_hash, key);
    }else if(strcmp(cmd,"HEXIST") == 0){
        kvs_hash_exist(globle_hash, key);
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
    // save_old_mes(cmd, key, value);
    // send_old_mes(cmd, key, value);
}

// ok
// read requesies in a.txt
void kvs_add_read(char *cmd, char *key, char *value){
    FILE *fp = fopen("config/add.txt", "r");
    if(fp == NULL){
        perror("open a.txt");
        return;
    }

    char buf[TXT_LENGTH] = {0}; 
    int status = CMD;
    int key_len = 0;
    int value_len = 0;

    while(fgets(buf, sizeof(buf), fp)){
        if(buf[0] == '\n' || buf[0] == '\0') continue;

        switch(status){
            case CMD:
                if(strncmp(buf,"key: ",5) == 0){
                    strcat(key, buf + 5);
                    cmd[strcspn(cmd,"\r\n")] = 0;
                    status = KEY;
                }else if(strncmp(buf,"key_len: ",9) == 0){
                    sscanf(buf, "key_len: %d", &key_len);
                }else{
                    strcat(cmd, buf + 5);
                }
                break;
            case KEY:
                if(strncmp(buf,"value: ",7) == 0){
                    strcat(value, buf + 7);
                    status = VALUE;
                }else if(strncmp(buf,"cmd: ",5) == 0){
                    int len = strlen(key);
                    if(len > 0 && strlen(key) >= len){
                        key[len - 1] = '\0';
                    }
                    cmd[strcspn(cmd,"\r\n")]=0;
                    kvs_txt_load(cmd, key, value);
                    if(cfg.add_save == 1){
                        save_old_mes(cmd, key, value);
                        
                    }
                    memset(cmd,0,TXT_LENGTH);
                    memset(key,0,TXT_LENGTH);
                    memset(value,0,TXT_LENGTH);
                    strcat(cmd, buf + 5);
                    key_len=0;
                    value_len=0;
                    status = CMD;

                }else if(strncmp(buf,"value_len: ",11) == 0){
                    sscanf(buf, "value_len: %d", &value_len);
                }else{
                    strcat(key, buf);
                }
                break;
            case VALUE:

                if(strncmp(buf,"cmd: ",5) == 0){
                    int len = strlen(value);
                    if(len > 0 && value[len] == '\n'){
                        value[len] = '\0';
                    }
                    cmd[strcspn(cmd,"\r\n")]=0;
                    if(key_len > 0 && strlen(key) >= key_len){
                        key[key_len] = '\0';
                    }

                    if(value_len > 0 && strlen(value) >= value_len){
                        value[value_len] = '\0';
                    }
                    kvs_txt_load(cmd, key, value);
                    if(cfg.add_save == 1){
                        save_old_mes(cmd, key, value);
                    }
                    memset(cmd,0,TXT_LENGTH);
                    memset(key,0,TXT_LENGTH);
                    memset(value,0,TXT_LENGTH);
                    strcat(cmd, buf + 5);
                    key_len = 0;
                    value_len = 0;
                    status = CMD;
                }else{
                    strcat(value, buf);
                }
                break;
        }
            
        memset(buf, 0, strlen(buf));
    }
    if(strlen(key) > 0){
        int len = key_len;
        if(key_len > 0 && strlen(key) >= key_len){
            key[key_len] = '\0';
        }
    }
    if(strlen(value) > 0){
        int len = value_len;
        if(value_len > 0 && strlen(value) >= value_len){
            value[value_len] = '\0';
        }
    }
    if(strlen(cmd) > 0){
        kvs_txt_load(cmd,key,value);
        if(cfg.add_save == 1){
            save_old_mes(cmd, key, value);
            write_old_mes();
        }
        memset(cmd,0,TXT_LENGTH);
        memset(key,0,TXT_LENGTH);
        memset(value,0,TXT_LENGTH);
        value_len=0;
        status = KEY;
    }

///////////////-
    fclose(fp);
}

// ok
void kvs_full_read(char *cmd, char *key, char *value){

    FILE *fp = fopen("config/full.txt", "r");
    if(fp == NULL){
        perror("open a.txt");
        return;
    }

    char buf[TXT_LENGTH] = {0}; 
    int status = CMD;
    int key_len = 0;
    int value_len = 0;

    while(fgets(buf, sizeof(buf), fp)){
        if(buf[0] == '\n' || buf[0] == '\0') continue;

        switch(status){
            case CMD:
                if(strncmp(buf,"key: ",5) == 0){
                    strcat(key, buf + 5);
                    cmd[strcspn(cmd,"\r\n")] = 0;
                    status = KEY;
                }else if(strncmp(buf,"key_len: ",9) == 0){
                    sscanf(buf, "key_len: %d", &key_len);
                }else{
                    strcat(cmd, buf + 5);
                }
                break;
            case KEY:
                if(strncmp(buf,"value: ",7) == 0){
                    strcat(value, buf + 7);
                    status = VALUE;
                }else if(strncmp(buf,"cmd: ",5) == 0){
                    int len = strlen(key);
                    if(len > 0 && strlen(key) >= len){
                        key[len - 1] = '\0';
                    }
                    cmd[strcspn(cmd,"\r\n")]=0;
                    kvs_txt_load(cmd, key, value);
                    if(cfg.load_old_mes == 1){
                        save_old_mes(cmd, key, value);
                    }
                    memset(cmd,0,TXT_LENGTH);
                    memset(key,0,TXT_LENGTH);
                    memset(value,0,TXT_LENGTH);
                    strcat(cmd, buf + 5);
                    key_len=0;
                    value_len=0;
                    status = CMD;

                }else if(strncmp(buf,"value_len: ",11) == 0){
                    sscanf(buf, "value_len: %d", &value_len);
                }else{
                    strcat(key, buf);
                }
                break;
            case VALUE:

                if(strncmp(buf,"cmd: ",5) == 0){
                    int len = strlen(value);
                    if(len > 0 && value[len] == '\n'){
                        value[len] = '\0';
                    }
                    cmd[strcspn(cmd,"\r\n")]=0;
                    if(key_len > 0 && strlen(key) >= key_len){
                        key[key_len] = '\0';
                    }

                    if(value_len > 0 && strlen(value) >= value_len){
                        value[value_len] = '\0';
                    }
                    kvs_txt_load(cmd, key, value);

                    if(cfg.load_old_mes == 1){
                        save_old_mes(cmd, key, value);
                    }

                    memset(cmd,0,TXT_LENGTH);
                    memset(key,0,TXT_LENGTH);
                    memset(value,0,TXT_LENGTH);
                    strcat(cmd, buf + 5);
                    key_len = 0;
                    value_len = 0;
                    status = CMD;
                }else{
                    strcat(value, buf);
                }
                break;
        }
            
        memset(buf, 0, strlen(buf));
    }
    if(strlen(key) > 0){
        int len = key_len;
        if(key_len > 0 && strlen(key) >= key_len){
            key[key_len] = '\0';
        }
    }
    if(strlen(value) > 0){
        int len = value_len;
        if(value_len > 0 && strlen(value) >= value_len){
            value[value_len] = '\0';
        }
    }
    if(strlen(cmd) > 0){
        kvs_txt_load(cmd,key,value);
        if(cfg.load_old_mes == 1){
            save_old_mes(cmd, key, value);
            write_old_mes();
        }
        free(old_mes);
        memset(cmd,0,TXT_LENGTH);
        memset(key,0,TXT_LENGTH);
        memset(value,0,TXT_LENGTH);
        value_len=0;
        status = KEY;
    }

    fclose(fp);
}

static int add_save_flag = 0;

int main(int argc, char const *argv[])
{
    // if(argc != 3) return -1;

     
    load_config("config/server.conf", &cfg);
    
    init_kvs();
    char cmd[TXT_LENGTH] = {0};
    char key[TXT_LENGTH] = {0};
    char value[TXT_LENGTH] = {0};

    if(cfg.mode == 1){
        if(cfg.add_save == 1) kvs_add_read(cmd, key, value);
        if(cfg.full_save == 1) {
            kvs_full_read(cmd, key, value);
        }
        
        reactor_start(cfg.port,kvs_protocol);
    }else if(cfg.mode == 2){
        
        if(cfg.add_save == 1) kvs_add_read(cmd, key, value);
        if(cfg.full_save == 1) kvs_full_read(cmd, key, value);

        proactor_start(cfg.port,kvs_protocol);
    }
    dest_kvs();
    kvs_push_destroy();
    mpool_destroy();
    return 0;
}
