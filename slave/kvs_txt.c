
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

    
        printf("%s\n",buf);
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
#if 0
    FILE *tmp = fopen("tmp.txt","w");
    if(tmp == NULL) return -1;

    kvs_txt_full_load(filename, tmp, key);
    // printf("txt cmd: %s\n",cmd);
    fprintf(tmp, "cmd: %s\nkey_len: %d\nkey: %s\nvalue_len: %d\nvalue: %s\n",cmd,*key_len,key,*value_len,value);

    fclose(tmp);

    remove(filename);
    rename("tmp.txt", filename);
#endif
    return 0;
};

int kvs_txt_del(char *cmd, char* key, int *key_len){

    
#if 0

    FILE *tmp = fopen("tmp.txt", "w");
    if(tmp == NULL) return -1;

    kvs_txt_full_load(filename, tmp, key);

    fprintf(tmp, "cmd: %s\nkey_len: %d\nkey: %s\n",cmd,*key_len,key);
    fclose(tmp);

    remove(filename);
    rename("tmp.txt", filename);
#endif
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

#if 0

    FILE *tmp = fopen("tmp.txt", "w");
    if(tmp == NULL) return -1;

    kvs_txt_full_load(filename, tmp, key);

    fprintf(tmp, "cmd: %s\nkey_len: %d\nkey: %s\n",cmd,*key_len,key);

    fclose(tmp);

    remove(filename);
    rename("tmp.txt", filename);

#endif
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
#if 0
    FILE *fp = fopen(filename, "r");
    if(fp == NULL)  return -1;

    FILE *tmp = fopen("tmp.txt", "w");
    if(tmp == NULL) return -1;

    kvs_txt_full_load(filename, tmp, key);

    fprintf(tmp, "cmd: %s\nkey_len: %d\nkey: %s\nvalue_len: %d\nvalue: %s\n",cmd, *key_len, key, *value_len, value);

    fclose(tmp);

    remove(filename);
    rename("tmp.txt", filename);

#endif
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

        fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\n", cmd, *key_len, key);
        fflush(fp);

        fclose(fp);
    }

#if 0

    FILE *tmp = fopen("tmp.txt", "w");
    if(tmp == NULL) return NULL;

    kvs_txt_full_load(filename, tmp, key);

    fprintf(tmp, "cmd: %s\nkey_len: %d\nkey: %s\n", cmd, *key_len, key);

    fclose(tmp);

    remove(filename);
    rename("tmp.txt", filename);

#endif
    return NULL;
}

