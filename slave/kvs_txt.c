
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

    char buf[TXT_LENGTH] = {0}; 
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

#define KEY_VALUE_OFF   4
int full_save_to_resp(char *msg, uint32_t length, char **response, size_t *res_size){
    if (!msg || !response || !*response || !res_size) return -1;

    uint32_t i = 0;
    size_t offset = 0;
    int set_count = 0;

    uint32_t k_len; uint32_t v_len;
    while(i < length){

        if (length - i < KEY_VALUE_OFF) return -1;


        memcpy(&k_len, msg + i, KEY_VALUE_OFF);
        i += KEY_VALUE_OFF;

        if (k_len > length - i) return -1;

        char *key = msg + i;
        i += k_len;

        if (length - i < KEY_VALUE_OFF) return -1;

        memcpy(&v_len, msg + i, KEY_VALUE_OFF);
        i += KEY_VALUE_OFF;

        if (v_len > length - i)
        return -1;

        char *value = msg + i;
        i += v_len;

        int ret = snprintf(NULL, 0, "*3\r\n$3\r\nSET\r\n$%u\r\n", k_len);
        if (offset + (size_t)ret + k_len + 2 > *res_size){

            void *ptr = realloc(*response, (size_t)ret + offset + k_len + 2);
            if(ptr){
                *response = ptr;
                *res_size = (size_t)ret + offset + k_len + 2;
            }else return -1;

        }

        ret = sprintf(*response + offset, "*3\r\n$3\r\nSET\r\n$%u\r\n", k_len);
        if (ret < 0 || (size_t)ret >= *res_size - offset) return -1;

        offset += ret;

        
        memcpy(*response + offset, key, k_len);
        offset += k_len;

        memcpy(*response + offset, "\r\n", 2);
        offset += 2;

        ret = snprintf(NULL, 0, "$%u\r\n", v_len);
        if ((size_t)ret + offset + v_len + 2 > *res_size){
            void *ptr = realloc(*response, (size_t)ret + offset + v_len + 2);
            if(ptr){
                *response = ptr;
                *res_size = (size_t)ret + offset + v_len + 2;
            }else return -1;
        }

        ret = snprintf(*response + offset, *res_size - offset, "$%u\r\n", v_len);
        if (ret < 0 || (size_t)ret >= *res_size - offset) return -1;

        offset += ret;

        memcpy(*response + offset, value, v_len);
        offset += v_len;

        memcpy(*response + offset, "\r\n", 2);
        offset += 2;

        set_count++;
    }

    return (int)offset;
}