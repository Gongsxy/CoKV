
#include "kvstore.h"
#include "load_conf.h"

enum {
    CMD,
    KEY,
    VALUE
};

static Config cfg;
static char* filename = "config/add.txt";

void kvs_txt_full_load(char *msg){

    FILE *fp = fopen("config/full.txt", "a+");
    if(fp == NULL) return ;


    char *cmd = NULL;
    char *key = NULL;
    char *value = NULL;

    char buf[1024] = {0}; 
    int status = CMD;
    int key_len = 0;
    int value_len = 0;

    memcpy(buf, msg, strlen(msg));

    fputs(buf, fp);
    
}

int kvs_txt_write(char *cmd, char* key, char* value, int *key_len, int *value_len){
    if(key == NULL || value == NULL) return -1;

    load_config("config/server.conf", &cfg);

    if(cfg.add_save == 1){
        FILE *fp = fopen(filename, "a+");
        if(fp == NULL) return -1;

        fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\nvalue_len: %d\nvalue: %s\n",cmd,*key_len,key,*value_len,value);
        fflush(fp);
        fclose(fp);
    }

    return 0;
};

int kvs_txt_del(char *cmd, char* key, int *key_len){

    load_config("config/server.conf", &cfg);
    int add_flag = cfg.add_save;
    char* filename = "config/add.txt";

    if(cfg.add_save == 1){
        FILE *fp = fopen(filename, "a+");
        if(fp == NULL) return -1;

        fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\n",cmd,*key_len,key);
        fflush(fp);
        fclose(fp);
    }
    return 0;
}

int kvs_txt_exist(char *cmd, char* key, int *key_len){
    if(key == NULL) return -1;

    load_config("config/server.conf", &cfg);
    int add_flag = cfg.add_save;
    char* filename = "config/add.txt";

    if(cfg.add_save == 1){

        FILE *fp = fopen(filename, "a+");
        if(fp == NULL) return -1;

        fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\n",cmd,*key_len,key);
        fflush(fp);
        fclose(fp);
    }

    return 0;
}

int kvs_txt_mod(char *cmd, char* key, char* value, int *key_len, int *value_len){

    if(key == NULL || value == NULL) return -1;
    load_config("config/server.conf", &cfg);
    int add_flag = cfg.add_save;
    char* filename = "config/add.txt";

    if(cfg.add_save == 1){

        FILE *fp = fopen(filename, "a+");
        if(fp == NULL) {
            return -1;
        }

        fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\nvalue_len: %d\nvalue: %s\n",cmd, *key_len, key, *value_len, value);
        fflush(fp);
        
        fclose(fp);

    }

    return 0;
}

char * kvs_txt_get(char *cmd, char* key, int *key_len){

    if(key == NULL) return NULL;

    load_config("config/server.conf", &cfg);
    int add_flag = cfg.add_save;
    char* filename = "config/add.txt";

    if(cfg.add_save == 1){
        FILE *fp = fopen(filename, "a+");
        if(fp == NULL) return NULL;
        printf("cmd: %s\nkey_len: %d\nkey: %s\n", cmd, *key_len, key);
        fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\n", cmd, *key_len, key);
        fflush(fp);

        fclose(fp);
    }


    return NULL;
}

int kvs_add_save(char *msg){
    if(msg == NULL) return -1;
    
    load_config("config/server.conf", &cfg);
    int add_flag = cfg.add_save;
    char* filename = "config/add.txt";

    if(cfg.add_save == 1){
        FILE *fp = fopen(filename, "a+");
        if(fp == NULL) return -1;
        
        fprintf(fp, "%s", msg);
        fflush(fp);

        fclose(fp);
    }
}

int full_save_to_resp(char *msg, uint32_t length, char *response){
    if(msg == NULL) return -1;

    static int is_full_save = 0;
    if(strncmp(msg, "key_len: ", 9) == 0) {
        is_full_save = 1;
    }else return -1;

    uint32_t i = 0;
    if(is_full_save == 1){
        uint32_t offset = 0;
        int ret = 0;
        char key[1024] = {0}; char value[1024] = {0};
        int k_len; int v_len;
        while(i < length){
            if(strncmp(msg + i, "key_len: ", 9) == 0){
                int len = 0;
                sscanf(msg + i, "key_len: %d%n", &k_len, &len);
                ret = sprintf(response + offset, "*3\r\n$3\r\nSET\r\n$%d\r\n", k_len);
                offset += ret;
                i += len + 2;
            }
            if(strncmp(msg + i, "key: ", 5) == 0){
                int len = 0;
                memset(key, 0, 1024);
                memcpy(key, msg + i + 5, (size_t)k_len);
                ret = sprintf(response + offset, "%s\r\n", key);
                offset += ret;
                i += 5 + k_len + 2;
            }
            if(strncmp(msg + i, "value_len: ", 11) == 0){
                int len = 0;
                sscanf(msg + i, "value_len: %d%n", &v_len, &len);
                ret = sprintf(response + offset, "$%d\r\n", v_len);
                offset += ret;
                i += len + 2;
            }
            if(strncmp(msg + i, "value: ", 7) == 0){
                int len = 0;
                memset(value, 0, 1024);
                memcpy(value, msg + i + 7, (size_t)v_len);
                ret = sprintf(response + offset, "%s\r\n", value);
                offset += ret;
                i += 7 + v_len + 2;
            }
        }
        // printf("res:%s\n", response);
    }
    return (int)i;
}