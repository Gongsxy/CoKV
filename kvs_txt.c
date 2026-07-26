
#include "kvstore.h"


enum {
    CMD,
    KEY,
    VALUE
};

void kvs_txt_full_load(char *filename, FILE *tmp, char *key){

    FILE *fp = fopen(filename, "r");
    if(fp == NULL) return ;

    char tget[TXT_LENGTH] = {0};
    int status = CMD;
    while(fgets(tget, sizeof(tget), fp)){
        switch(status){
            case CMD:
                fprintf(tmp,"%s",tget);
                if(strncmp(tget, "key: ", 5) == 0){
                    status = KEY;
                }
                break;
            case KEY:
                fprintf(tmp,"%s",tget);
                if(strncmp(tget, "value: ", 7) == 0){
                    status = VALUE;
                }
                break;
            case VALUE:
                fprintf(tmp,"%s",tget);
                if(strncmp(tget, "cmd: ", 5) == 0){
                    status = CMD;
                }
                break;
        }
    }
    fclose(fp);
}

int kvs_txt_write(char *cmd, char* key, char* value, int *key_len, int *value_len){
    if(key == NULL || value == NULL) return -1;
    char* filename = "a.txt";
#if ADDSTORAGE
    FILE *fp = fopen(filename, "a+");
    if(fp == NULL) return -1;

    fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\nvalue_len: %d\nvalue: %s\n",cmd,*key_len,key,*value_len,value);
    fflush(fp);
    fclose(fp);
#endif
#if FULLSTORAGE
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
    char* filename = "a.txt";
    
#if FULLSTORAGE

    FILE *tmp = fopen("tmp.txt", "w");
    if(tmp == NULL) return -1;

    kvs_txt_full_load(filename, tmp, key);

    fprintf(tmp, "cmd: %s\nkey_len: %d\nkey: %s\n",cmd,*key_len,key);
    fclose(tmp);

    remove(filename);
    rename("tmp.txt", filename);
#endif
#if ADDSTORAGE

    FILE *fp = fopen(filename, "a+");
    if(fp == NULL) return -1;

    fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\n",cmd,*key_len,key);
    fflush(fp);
    fclose(fp);
    
#endif
    return 0;
}

int kvs_txt_exist(char *cmd, char* key, int *key_len){
    if(key == NULL) return -1;
    char* filename = "a.txt";
#if ADDSTORAGE

    FILE *fp = fopen(filename, "a+");
    if(fp == NULL) return -1;

    fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\n",cmd,*key_len,key);
    fflush(fp);
    fclose(fp);

#endif
#if FULLSTORAGE

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
    char* filename = "a.txt";
    if(key == NULL || value == NULL) return -1;
#if  ADDSTORAGE

    FILE *fp = fopen(filename, "a+");
    if(fp == NULL) {
        return -1;
    }

    fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\nvalue_len: %d\nvalue: %s\n",cmd, *key_len, key, *value_len, value);
    fflush(fp);
    
    fclose(fp);

#endif
#if FULLSTORAGE
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
    char* filename = "a.txt";
    if(key == NULL) return NULL;

#if ADDSTORAGE
    FILE *fp = fopen(filename, "a+");
    if(fp == NULL) return NULL;

    fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\n", cmd, *key_len, key);
    fflush(fp);

    fclose(fp);
#endif
#if FULLSTORAGE

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