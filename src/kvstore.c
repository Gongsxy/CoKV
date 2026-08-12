

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
#if ADDSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
#if FULLSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
            
            if(ret < 0){
                length = sprintf(response,"-ERR %s\r\n", "unknown command");
                // printf("ERROR\r\n");
            }else{  
                length = sprintf(response,"+OK\r\n");
                // printf("EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_GET:{
#if ADDSTORAGE
            kvs_txt_get(cmd, key, key_len);
            m_free(value);
#endif       
#if FULLSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif 
            char *result = kvs_array_get(globle_array,key);
            if(result == NULL){
                length = sprintf(response,"$-1\r\n");
                // printf("NOT EXIST\r\n");
            }else{        
                length = sprintf(response,"$%zu\r\n%s\r\n", strlen(result), result);
                // printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_DEL:{
            ret = kvs_array_del(globle_array,key);
#if ADDSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
#if FULLSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
            if(ret < 0){
                length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                length = sprintf(response,":1\r\n");
                // printf("OK\r\n");
            }else{
                length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_MOD:{
            ret = kvs_array_mod(globle_array,key,value);
#if ADDSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
#if FULLSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                length = sprintf(response,"+OK\r\n");
                // printf("OK\r\n");
            }else{
                length = sprintf(response,"-ERR no such key\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_EXIST:
            ret = kvs_array_exist(globle_array,key);

            kvs_txt_exist(cmd, key, key_len);
            if(ret == 0){
                length = sprintf(response,":1\r\n");
                // printf("EXIST\r\n");
            }else{
                length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;

#endif
// rbtree
#if ENABLE_RBTREE
        case KVS_CMD_RSET:{
            ret = kvs_rbtree_set(globle_rbtree,key,value);
#if FULLSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
#if ADDSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                length = sprintf(response,"-ERR %s\r\n", "unknown command");
                // printf("ERROR\r\n");
            }else{
                length = sprintf(response,"+OK\r\n");
                // printf("EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_RGET:{
            char *result = kvs_rbtree_get(globle_rbtree,key);
#if FULLSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif
#if ADDSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif 
            if(result == NULL){
                length = sprintf(response,"$-1\r\n");
                // printf("NOT EXIST\r\n");
            }else{
                length = sprintf(response,"$%zu\r\n%s\r\n", strlen(result), result);
                // printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_RDEL:{
            ret = kvs_rbtree_del(globle_rbtree,key);
#if FULLSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
#if ADDSTORAGE
                kvs_txt_del(cmd, key, key_len);
#endif
            if(ret < 0){
                length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                length = sprintf(response,":1\r\n");
                // printf("OK\r\n");
            }else{
                length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_RMOD:{
#if FULLSTORAGE
            kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
#if ADDSTORAGE
            kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
            ret = kvs_rbtree_mod(globle_rbtree,key,value);
            if(ret < 0){
                length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                length = sprintf(response,"+OK\r\n");
                // printf("OK\r\n");
            }else{
                length = sprintf(response,"-ERR no such key\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_REXIST:
            ret = kvs_rbtree_exist(globle_rbtree,key);
            kvs_txt_exist(cmd, key, key_len);
            if(ret == 0){
                length = sprintf(response,":1\r\n");
                // printf("EXIST\r\n");
            }else{
                length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
#endif
// hash
#if ENABLE_HASH
        case KVS_CMD_HSET:{
            int ret = kvs_hash_set(globle_hash,key,value);
#if FULLSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
#if ADDSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                length = sprintf(response,"-ERR %s\r\n", "unknown command");
                // printf("ERROR\r\n");
            }else{
                length = sprintf(response,"+OK\r\n");
                // printf("EXIST\r\n");/
            }
            break;
        }
        case KVS_CMD_HGET:{
            char *result = kvs_hash_get(globle_hash,key);
#if FULLSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif
#if ADDSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif
            if(result == NULL){
                length = sprintf(response,"$-1\r\n");
                // printf("NOT EXIST\r\n");
            }else{
                length = sprintf(response,"$%zu\r\n%s\r\n", strlen(result), result);
                // printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_HDEL:{
            ret = kvs_hash_del(globle_hash,key);
#if FULLSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
#if ADDSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
            if(ret < 0){
                length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                length = sprintf(response,":1\r\n");
                // printf("OK\r\n");
            }else{
                length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_HMOD:{
            ret = kvs_hash_mod(globle_hash,key,value);
#if FULLSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
#if ADDSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                length = sprintf(response,"+OK\r\n");
                // printf("OK\r\n");
            }else{
                length = sprintf(response,"-ERR no such key\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_HEXIST:
            ret = kvs_hash_exist(globle_hash,key);
            kvs_txt_exist(cmd, key, key_len);
            if(ret == 0){
                length = sprintf(response,":1\r\n");
                // printf("EXIST\r\n");
            }else{
                length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
#endif
// skiplist
#if ENABLE_SKIPTABLE

        case KVS_CMD_SSET:{
            ret = kvs_skiplist_set(globle_skiplist,key,value);
#if ADDSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
#if FULLSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                length = sprintf(response,"-ERR %s\r\n", "unknown command");
                // printf("ERROR\r\n");
            }else{
                length = sprintf(response,"+OK\r\n");
                // printf("EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_SGET:{
            char *result = kvs_skiplist_get(globle_skiplist,key);
#if ADDSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif    
#if FULLSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif  
            if(result == NULL){
                length = sprintf(response,"$-1\r\n");
                // printf("NOT EXIST\r\n");
            }else{          
                length = sprintf(response,"$%zu\r\n%s\r\n", strlen(result), result);
                // printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_SDEL:{
            ret = kvs_skiplist_del(globle_skiplist,key);
#if ADDSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
#if FULLSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
            if(ret < 0){
                length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                length = sprintf(response,":1\r\n");
                // printf("OK\r\n");
            }else{
                length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_SMOD:{
            ret = kvs_skiplist_mod(globle_skiplist,key,value);
#if ADDSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
#if FULLSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                length = sprintf(response,"-ERR internal error\r\n");
                // printf("ERROR\r\n");
            }else if(ret == 0){
                length = sprintf(response,"+OK\r\n");
                // printf("OK\r\n");
            }else{
                length = sprintf(response,"-ERR no such key\r\n");
                // printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_SEXIST:
            ret = kvs_skiplist_exist(globle_skiplist,key);

            kvs_txt_exist(cmd, key, key_len);

            if(ret == 0){
                length = sprintf(response,":1\r\n");
                // printf("EXIST\r\n");
            }else{
                length = sprintf(response,":0\r\n");
                // printf("NOT EXIST\r\n");
            }

            break;

#endif
        case KVS_CMD_PING:{
            length = sprintf(response,"+PONG\r\n");
            break;
        }
        case KVS_CMD_QUIT:{
            length = sprintf(response,"+OK\r\n");
            break;
        }
        case KVS_CMD_SYNC:{
            FILE *fp = fopen("sync.log", "r");
            if (fp == NULL) {
                // 文件不存在，直接回 DONE
                length = sprintf(response, "$4\r\nDONE\r\n");
                break;
            }

            int offset = 0;
            int remain = BUFFER_LENGTH - 16;  // 预留 DONE 的空间
            char line[512] = {0};

            // 一行一行读，直接拷进 response
            while (fgets(line, sizeof(line), fp)) {
                int n = strlen(line);
                if (offset + n >= remain) {
                    // buffer 满了，截断，剩下的下次 SYNC 再拿
                    break;
                }
                memcpy(response + offset, line, n);
                offset += n;
            }
            fclose(fp);

            // 结束标记
            offset += snprintf(response + offset, remain - offset, "$4\r\nDONE\r\n");
            length = offset;
            break;
        }
        default:{
            length = sprintf(response,"-ERR unknown command '%s'\r\n", tokens[0]);
            break;
        }
    }
    
    *push_flag = 0;
    return length;
};

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
        //  && tcopy[i] != '\0'
        // tcopy[i] < length
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
                        tokens[0] = m_malloc(cmd_len + 1);
                        memcpy(tokens[0], &tcopy[i], cmd_len);
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
                        return 0; // 数据不完整
                    }
                    tokens[1] = m_malloc(*key_len + 1);
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
// printf("status=%d i=%d char=%c\n",
//        status,i,tcopy[i]);
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
                    tokens[2] = m_malloc(*value_len + 1);
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
            for(int k = 0; tokens[0][k]; k++){
                tokens[0][k] = toupper(tokens[0][k]);
            }

            int len = kvs_protocol_filter(tokens, response + resp_len, key_len, value_len, push_flag);
            resp_len += len;
            if(tokens[0]) m_free(tokens[0]);
            if(tokens[1]) m_free(tokens[1]);
            if(tokens[2]) m_free(tokens[2]);
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

    return resp_len;
}



int kvs_is_complete(char *msg, int length){

    int i = 0;
    char total_length[128] = {0};
    while(msg[i] != '\r'){
        total_length[i] = msg[i];
        i++;
    }
    i += 2;
    int real_length = atoi(total_length);

    int j = 0;

    if (i + real_length > length) {
        return 0;  // 数据不够，等下次 recv
    }
    return 1;
}


int packet_len = 0;
int kvs_protocol(char *msg, int length, char *response){
    int key_len = 0;
    int value_len = 0;
    int push_flag = 0;

    // if (length > 0 && msg[0] != '*') {
    //     // 找 \r\n 边界
    //     int linelen = 0;
    //     while (linelen < length - 1 && !(msg[linelen] == '\r' && msg[linelen+1] == '\n')) {
    //         linelen++;
    //     }
    //     if (linelen >= length - 1) return 0;  // 数据不完整，等下次
        
    //     // 拷贝一行（去掉 \r\n）
    //     char line[BUFFER_LENGTH] = {0};
    //     memcpy(line, msg, linelen);
    //     line[linelen] = '\0';
        
    //     // 按空格分割
    //     char *tokens[3] = {NULL, NULL, NULL};
    //     tokens[0] = strtok(line, " ");
    //     tokens[1] = strtok(NULL, " ");
    //     tokens[2] = strtok(NULL, " ");
        
    //     // 命令转大写
    //     if (tokens[0]) {
    //         for(int k = 0; tokens[0][k]; k++) {
    //             tokens[0][k] = toupper(tokens[0][k]);
    //         }
    //     }
        
    //     memset(response, 0, BUFFER_LENGTH);
    //     int count = kvs_protocol_filter(tokens, response, &key_len, &value_len, &push_flag);
    //     return count;  // 内联命令直接返回，不走下面的 RESP 逻辑
    // }

    static char *mes = NULL;
    static int mes_len = 0;
    if (mes == NULL) {
        mes = malloc(3072000);
        mes_len = 0;
    }
    
    if(cfg.full_save_flag == 1){
        if(strcmp(msg, "save\n") == 0){
            char mes_array[4096] = {0};
            kvs_array_save(globle_array, mes_array);
            char mes_skip[4096] = {0};
            kvs_skiplist_save(globle_skiplist, mes_skip);
            char mes_rbtree[4096] = {0};
            kvs_rbtree_save(globle_rbtree, mes_rbtree);
            char mes_hash[4096] = {0};
            kvs_hash_save(globle_hash, mes_hash);
            FILE *fp_full = fopen("full.txt", "a+");
            fputs(mes_array, fp_full);
            fputs(mes_skip, fp_full);
            fputs(mes_rbtree, fp_full);
            fputs(mes_hash, fp_full);
            fflush(fp_full);
            fclose(fp_full);
            return 0;
        }

    }
    
    memset(response, 0, BUFFER_LENGTH);
    memcpy(mes + mes_len,msg,length);
    mes_len += length;

    char *tokens[3] = {NULL, NULL, NULL};
    int count = 0;

    
    packet_len = kvs_is_complete(mes, mes_len);
    // packet_len = kvs_is_complete(mes);

    if(packet_len){
        count = kvs_split_token(mes, tokens, response, mes_len, &key_len, &value_len, &push_flag);

        if(push_flag){
            kvs_protocol_push(mes);
        }
        memmove(mes, mes + packet_len, mes_len - packet_len);
        mes_len -= packet_len;
    }
    
    return count;
}

int init_kvs(void){
#if ENABLE_ARRAY
    // memset(globle_array,0,sizeof(kvs_array_t));
    globle_array = m_malloc(sizeof(kvs_array_t));
    kvs_array_create(globle_array);
#endif

#if ENABLE_RBTREE
    // memset(globle_rbtree,0,sizeof(kvs_rbtree_t));
    globle_rbtree = m_malloc(sizeof(kvs_rbtree_t));
    kvs_rbtree_create(globle_rbtree);
#endif

#if ENABLE_HASH
    // memset(globle_hash,0,sizeof(kvs_hash_t));
    globle_hash = m_malloc(sizeof(kvs_hash_t));
    kvs_hash_create(globle_hash);
#endif

#if ENABLE_SKIPTABLE
    // memset(globle_skiplist, 0, sizeof(kvs_skiplist_t));
    globle_skiplist = m_malloc(sizeof(kvs_skiplist_t));
    kvs_skiplist_create(globle_skiplist);
#endif
    return 0;
}

void dest_kvs(void){
#if ENABLE_ARRAY
kvs_array_destory(globle_array);
m_free(globle_array);
#endif

#if ENABLE_RBTREE
kvs_rbtree_destory(globle_rbtree);
m_free(globle_rbtree);
#endif

#if ENABLE_HASH
kvs_hash_destory(globle_hash);
m_free(globle_hash);
#endif

#if ENABLE_SKIPTABLE
kvs_skiplist_destroy(globle_skiplist);
m_free(globle_skiplist);
#endif
}



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

int write_old_mes(void){
    if(old_mes == NULL) return 0;
    FILE *fp = fopen("sync.log", "w");
    fputs(old_mes, fp);
    fflush(fp);
    fclose(fp);

    old_mes = NULL;
    
    return 0;
}

void kvs_txt_load(char *cmd, char *key, char *value){
    printf("Lcmd:%s,key:%s,value:%s\n",cmd, key, value);
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

// read requesies in a.txt
void kvs_add_read(char *cmd, char *key, char *value){
    FILE *fp = fopen("add.txt", "r");
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
        memset(cmd,0,TXT_LENGTH);
        memset(key,0,TXT_LENGTH);
        memset(value,0,TXT_LENGTH);
        value_len=0;
        status = KEY;
    }

///////////////-
    fclose(fp);
}

void kvs_full_read(char *cmd, char *key, char *value){

    FILE *fp = fopen("full.txt", "r");
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

///////////////-
    fclose(fp);
}

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
            // if(cfg.load_old_mes == 1){
            //     printf("enter\n");
            //     save_old_mes(cmd, key, value);
            //     write_old_mes();
            // }
        }
        reactor_start(cfg.port,kvs_protocol);
    }else if(cfg.mode == 2){
        
        if(cfg.add_save == 1) kvs_add_read(cmd, key, value);
        if(cfg.full_save == 1) kvs_full_read(cmd, key, value);

        proactor_start(cfg.port,kvs_protocol);
    }
    dest_kvs();
    return 0;
}
