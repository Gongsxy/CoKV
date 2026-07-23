

#include "kvstore.h"

#if 1
extern kvs_array_t globle_array;
#endif

#if ENABLE_RBTREE
extern kvs_rbtree_t globle_rbtree;
#endif

#if ENABLE_HASH
extern kvs_hash_t globle_hash;
#endif

#if ENABLE_SKIPTABLE
extern kvs_skiplist_t globle_skiplist;
#endif


const char *command[] = {
    "SET","GET","DEL","MOD","EXIST",
    "RSET","RGET","RDEL","RMOD","REXIST",
    "HSET","HGET","HDEL","HMOD","HEXIST",
    "SSET","SGET","SDEL","SMOD","SEXIST"
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

    KVS_CMD_COUNT,
};

enum {
    CMD,
    KEY,
    VALUE
};

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

int kvs_split_token(char *msg, char *tokens[], int *key_len, int *value_len){

    if(msg == NULL || tokens == NULL) return -1;
    char *tcopy = msg;
    int idx = 0;
    char *cmd = strchr(tcopy,' ');
    // printf("cmd: %s\n",cmd);
    int cmd_len = cmd - tcopy;

    tokens[0] = kvs_malloc(cmd_len + 1);
    memcpy(tokens[0], tcopy, cmd_len);
    tokens[0][cmd_len] = '\0';
    idx++;
    tcopy = cmd + 1;
    *key_len = atoi(tcopy);
    while(*tcopy != ' ') tcopy ++;
    tcopy++;
    tokens[1] = kvs_malloc(*key_len + 1);
    memcpy(tokens[1], tcopy, *key_len);
    tokens[1][*key_len - 1] = '\0';
    idx++;

    // value
    int i = 0;
    int match = 0;
    for(i = 0 ; i < 8 ; i ++){
        if(strcmp(tokens[0],key_value_command[i]) == 0){
            match = 1;
        }
    }
    if(match){
        tcopy += *key_len;
        *value_len = atoi(tcopy);
        tcopy ++;
        while(*tcopy != ' ') tcopy ++;
        tcopy++;
        tokens[2] = kvs_malloc(*value_len + 1);

        memcpy(tokens[2], tcopy, *value_len);

        tokens[2][*value_len -1] = '\0';
        idx++;
    }
    i ++;

    return idx;
}

int kvs_protocol_filter(char **tokens,int count, char *response, int *key_len, int *value_len){
    if(tokens == NULL || count == 0 || response == NULL) return -1;

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
            ret = kvs_array_set(&globle_array,key,value);
#if ADDSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
#if FULLSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
            
            if(ret < 0){
                // length = sprintf(response,"ERROR\r\n");
                printf("ERROR\r\n");
            }else if(ret == 0){
                // length = sprintf(response,"OK\r\n");
                printf("OK\r\n");
            }else{
                // length = sprintf(response,"EXIST\r\n");
                printf("EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_GET:{
#if ADDSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif       
#if FULLSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif 
            char *result = kvs_array_get(&globle_array,key);
            if(result == NULL){
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }else{        
                // length = sprintf(response,"%s\r\n",result);
                printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_DEL:{
            ret = kvs_array_del(&globle_array,key);
#if ADDSTORAGE
                kvs_txt_del(cmd, key, key_len);
#endif
#if FULLSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
            if(ret < 0){
                // length = sprintf(response,"ERROR\r\n");
                printf("ERROR\r\n");
            }else if(ret == 0){
                // length = sprintf(response,"OK\r\n");
                printf("OK\r\n");
            }else{
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_MOD:
            ret = kvs_array_mod(&globle_array,key,value);
#if ADDSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
#if FULLSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                // length = sprintf(response,"ERROR\r\n");
                printf("ERROR\r\n");
            }else if(ret == 0){
                // length = sprintf(response,"OK\r\n");
                printf("OK\r\n");
            }else{
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }
            break;
        case KVS_CMD_EXIST:
            ret = kvs_array_exist(&globle_array,key);

            kvs_txt_exist(cmd, key, key_len);
            if(ret == 0){
                // length = sprintf(response,"EXIST\r\n");
                printf("EXIST\r\n");
            }else{
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }
            break;
#endif
// rbtree
#if ENABLE_RBTREE
        case KVS_CMD_RSET:{
            ret = kvs_rbtree_set(&globle_rbtree,key,value);
#if FULLSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
#if ADDSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                // length = sprintf(response,"ERROR\r\n");
                printf("ERROR\r\n");
            }else if(ret == 0){
                // length = sprintf(response,"OK\r\n");
                printf("OK\r\n");
            }else{
                // length = sprintf(response,"EXIST\r\n");
                printf("EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_RGET:{
            char *result = kvs_rbtree_get(&globle_rbtree,key);
#if FULLSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif
#if ADDSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif 
            if(result == NULL){
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }else{
                // length = sprintf(response,"%s\r\n",result);
                printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_RDEL:
            ret = kvs_rbtree_del(&globle_rbtree,key);
#if FULLSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
#if ADDSTORAGE
                kvs_txt_del(cmd, key, key_len);
#endif
            if(ret < 0){
                // length = sprintf(response,"ERROR\r\n");
                printf("ERROR\r\n");
            }else if(ret == 0){
                // length = sprintf(response,"OK\r\n");
                printf("OK\r\n");
            }else{
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }
            break;
        case KVS_CMD_RMOD:
#if FULLSTORAGE
            kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
#if ADDSTORAGE
            kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
            ret = kvs_rbtree_mod(&globle_rbtree,key,value);
            if(ret < 0){
                // length = sprintf(response,"ERROR\r\n");
                printf("ERROR\r\n");
            }else if(ret == 0){
                // length = sprintf(response,"OK\r\n");
                printf("OK\r\n");
            }else{
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }
            break;
        case KVS_CMD_REXIST:
            ret = kvs_rbtree_exist(&globle_rbtree,key);
            kvs_txt_exist(cmd, key, key_len);
            if(ret == 0){
                // length = sprintf(response,"EXIST\r\n");
                printf("EXIST\r\n");
            }else{
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }
            break;
#endif
// hash
#if ENABLE_HASH
        case KVS_CMD_HSET:{
            int ret = kvs_hash_set(&globle_hash,key,value);
#if FULLSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
#if ADDSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                // length = sprintf(response,"ERROR\r\n");
                printf("ERROR\r\n");
            }else if(ret == 0){
                // length = sprintf(response,"OK\r\n");
                printf("OK\r\n");
            }else{
                // length = sprintf(response,"EXIST\r\n");
                printf("EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_HGET:{
            char *result = kvs_hash_get(&globle_hash,key);
#if FULLSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif
#if ADDSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif
            if(result == NULL){
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }else{
                // length = sprintf(response,"%s\r\n",result);
                printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_HDEL:
            ret = kvs_hash_del(&globle_hash,key);
#if FULLSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
#if ADDSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
            if(ret < 0){
                // length = sprintf(response,"ERROR\r\n");
                printf("ERROR\r\n");
            }else if(ret == 0){
                // length = sprintf(response,"OK\r\n");
                printf("OK\r\n");
            }else{
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }
            break;
        case KVS_CMD_HMOD:
            ret = kvs_hash_mod(&globle_hash,key,value);
#if FULLSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
#if ADDSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                // length = sprintf(response,"ERROR\r\n");
                printf("ERROR\r\n");
            }else if(ret == 0){
                // length = sprintf(response,"OK\r\n");
                printf("OK\r\n");
            }else{
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }
            break;
        case KVS_CMD_HEXIST:
            ret = kvs_hash_exist(&globle_hash,key);
            kvs_txt_exist(cmd, key, key_len);
            if(ret == 0){
                // length = sprintf(response,"EXIST\r\n");
                printf("EXIST\r\n");
            }else{
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }
            break;
#endif
// skiplist
#if ENABLE_SKIPTABLE

        case KVS_CMD_SSET:{
            ret = kvs_skiplist_set(&globle_skiplist,key,value);
#if ADDSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
#if FULLSTORAGE
            kvs_txt_write(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                // length = sprintf(response,"ERROR\r\n");
                printf("ERROR\r\n");
            }else if(ret == 0){
                printf("OK\r\n");
                // length = sprintf(response,"OK\r\n");
            }else{
                // length = sprintf(response,"EXIST\r\n");
                printf("EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_SGET:{
            char *result = kvs_skiplist_get(&globle_skiplist,key);
#if ADDSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif    
#if FULLSTORAGE
            kvs_txt_get(cmd, key, key_len);
            free(value);
#endif  
            if(result == NULL){
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }else{          
                // length = sprintf(response,"%s\r\n",result);
                printf("%s\r\n",result);
            }
            break;
        }
        case KVS_CMD_SDEL:{
            ret = kvs_skiplist_del(&globle_skiplist,key);
#if ADDSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
#if FULLSTORAGE
            kvs_txt_del(cmd, key, key_len);
#endif
            if(ret < 0){
                // length = sprintf(response,"ERROR\r\n");
                printf("ERROR\r\n");
            }else if(ret == 0){
                // length = sprintf(response,"OK\r\n");
                printf("OK\r\n");
            }else{
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }
            break;
        }
        case KVS_CMD_SMOD:
            ret = kvs_skiplist_mod(&globle_skiplist,key,value);
#if ADDSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
#if FULLSTORAGE
                kvs_txt_mod(cmd, key, value, key_len, value_len);
#endif
            if(ret < 0){
                // length = sprintf(response,"ERROR\r\n");
                printf("ERROR\r\n");
            }else if(ret == 0){
                // length = sprintf(response,"OK\r\n");
                printf("OK\r\n");
            }else{
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }
            break;
        case KVS_CMD_SEXIST:
            ret = kvs_skiplist_exist(&globle_skiplist,key);

            kvs_txt_exist(cmd, key, key_len);

            if(ret == 0){
                // length = sprintf(response,"EXIST\r\n");
                printf("EXIST\r\n");
            }else{
                // length = sprintf(response,"NOT EXIST\r\n");
                printf("NOT EXIST\r\n");
            }

            break;

#endif
        
    }
    return length;
};

int kvs_protocol(char *msg, int length, char *response){

    int key_len = 0;
    int value_len = 0;
    memset(response, 0, BUFFER_LENGTH);
    memcpy(response,msg,length);

    kvs_protocol_push(msg);

    char *tokens[KVS_TOKENS_MAX] = {0};

    int count = kvs_split_token(msg, tokens, &key_len, &value_len);
 
    int ret = kvs_protocol_filter(tokens,count,response,&key_len,&value_len);


    return ret;
}

int init_kvs(void){
#if ENABLE_ARRAY
    memset(&globle_array,0,sizeof(kvs_array_t));
    kvs_array_create(&globle_array);
#endif

#if ENABLE_RBTREE
    memset(&globle_rbtree,0,sizeof(kvs_rbtree_t));
    kvs_rbtree_create(&globle_rbtree);
#endif

#if ENABLE_HASH
    memset(&globle_hash,0,sizeof(kvs_hash_t));
    kvs_hash_create(&globle_hash);
#endif

#if ENABLE_SKIPTABLE
    memset(&globle_skiplist, 0, sizeof(kvs_skiplist_t));
    kvs_skiplist_create(&globle_skiplist);
#endif
    return 0;
}

void dest_kvs(void){
#if ENABLE_ARRAY
kvs_array_destory(&globle_array);
#endif

#if ENABLE_RBTREE
kvs_rbtree_destory(&globle_rbtree);
#endif

#if ENABLE_HASH
kvs_hash_destory(&globle_hash);
#endif

#if ENABLE_SKIPTABLE
kvs_skiplist_destroy(&globle_skiplist);
#endif
}

// Execute the command in kvs_txt_read
void kvs_txt_load(char *cmd, char *key, char *value){
    printf("Lcmd:%s,key:%s,value:%s\n",cmd, key, value);
#if ENABLE_ARRAY
    if(strcmp(cmd,"SET") == 0){
        kvs_array_set(&globle_array, key, value);
    }else if(strcmp(cmd,"DEL") == 0){
        kvs_array_del(&globle_array, key);
    }else if(strcmp(cmd,"MOD") == 0){
        kvs_array_mod(&globle_array, key, value);
    }else if(strcmp(cmd,"GET") == 0){
        kvs_array_get(&globle_array, key);
    }else if(strcmp(cmd,"EXIST") == 0){
        kvs_array_exist(&globle_array, key);
    }
#endif
#if ENABLE_RBTREE
    if(strcmp(cmd,"RSET") == 0){
        kvs_rbtree_set(&globle_rbtree, key, value);
    }else if(strcmp(cmd,"RDEL") == 0){
        kvs_rbtree_del(&globle_rbtree, key);
    }else if(strcmp(cmd,"RMOD") == 0){
        kvs_rbtree_mod(&globle_rbtree, key, value);
    }else if(strcmp(cmd,"RGET") == 0){
        kvs_rbtree_get(&globle_rbtree, key);
    }else if(strcmp(cmd,"REXIST") == 0){
        kvs_rbtree_exist(&globle_rbtree, key);
    }
#endif
#if ENABLE_HASH
    if(strcmp(cmd,"HSET") == 0){
        kvs_hash_set(&globle_hash, key, value);
    }else if(strcmp(cmd,"HDEL") == 0){
        kvs_hash_del(&globle_hash, key);
    }else if(strcmp(cmd,"HMOD") == 0){
        kvs_hash_mod(&globle_hash, key, value);
    }else if(strcmp(cmd,"HGET") == 0){
        kvs_hash_get(&globle_hash, key);
    }else if(strcmp(cmd,"HEXIST") == 0){
        kvs_hash_exist(&globle_hash, key);
    }
#endif
#if ENABLE_SKIPTABLE
    if(strcmp(cmd,"SSET") == 0){
        kvs_skiplist_set(&globle_skiplist, key, value);
    }else if(strcmp(cmd,"SDEL") == 0){
        kvs_skiplist_del(&globle_skiplist, key);
    }else if(strcmp(cmd,"SMOD") == 0){
        kvs_skiplist_mod(&globle_skiplist, key, value);
    }else if(strcmp(cmd,"SGET") == 0){
        kvs_skiplist_get(&globle_skiplist, key);
    }else if(strcmp(cmd,"SEXIST") == 0){
        kvs_skiplist_exist(&globle_skiplist, key);
    }
#endif
}

// read requesies in a.txt
void kvs_txt_read(char *cmd, char *key, char *value){
    FILE *fp = fopen("a.txt", "r");
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
                    if(len>0 && value[len-1] == '\n'){
                        value[len-1] = '\0';
                    }
                    cmd[strcspn(cmd,"\r\n")]=0;
                    if(key_len > 0 && strlen(key) >= key_len){
                        key[key_len - 1] = '\0';
                    }

                    if(value_len > 0 && strlen(value) >= value_len){
                        value[value_len - 1] = '\0';
                    }
                    kvs_txt_load(cmd, key, value);
                    memset(cmd,0,TXT_LENGTH);
                    memset(key,0,TXT_LENGTH);
                    memset(value,0,TXT_LENGTH);
                    strcat(cmd, buf + 5);
                    key_len=0;
                    value_len=0;
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
            key[key_len - 1] = '\0';
        }
    }
    if(strlen(value) > 0){
        int len = value_len;
        if(value_len > 0 && strlen(value) >= value_len){
            value[value_len - 1] = '\0';
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



int main(int argc, char const *argv[])
{
    if(argc != 3) return -1;

    int port = atoi(argv[1]);
    int mode = atoi(argv[2]);

    init_kvs();

    char cmd[TXT_LENGTH]={0};
    char key[TXT_LENGTH]={0};
    char value[TXT_LENGTH]={0};
    kvs_txt_read(cmd, key, value);

    if(mode == 1){
        reactor_start(port,kvs_protocol);
    }else if(mode == 2){
        proactor_start(port,kvs_protocol);
    }

    dest_kvs();

    return 0;
}
