
#include "kvstore.h"


enum {
    CMD,
    KEY,
    VALUE
};

void kvs_txt_full_load(char *msg){

    FILE *fp = fopen("full.txt", "a+");
    if(fp == NULL) return ;


    char *cmd = NULL;
    char *key = NULL;
    char *value = NULL;

    char buf[TXT_LENGTH] = {0}; 
    int status = CMD;
    int key_len = 0;
    int value_len = 0;

    memcpy(buf, msg, strlen(msg));

    // while(fgets(buf, sizeof(buf), msg)){
    //     if(buf[0] == '\n' || buf[0] == '\0') continue;

    //     switch(status){
    //         case CMD:
    //             if(strncmp(buf,"key: ",5) == 0){
    //                 strcat(key, buf + 5);
    //                 cmd[strcspn(cmd,"\r\n")] = 0;
    //                 status = KEY;
    //             }else if(strncmp(buf,"key_len: ",9) == 0){
    //                 sscanf(buf, "key_len: %d", &key_len);
    //             }else{
    //                 strcat(cmd, buf + 5);
    //             }
    //             break;
    //         case KEY:
    //             if(strncmp(buf,"value: ",7) == 0){
    //                 strcat(value, buf + 7);
    //                 status = VALUE;
    //             }else if(strncmp(buf,"cmd: ",5) == 0){
    //                 int len = strlen(key);
    //                 if(len > 0 && strlen(key) >= len){
    //                     key[len - 1] = '\0';
    //                 }
    //                 cmd[strcspn(cmd,"\r\n")]=0;
    //                 kvs_txt_load(cmd, key, value);
    //                 memset(cmd,0,TXT_LENGTH);
    //                 memset(key,0,TXT_LENGTH);
    //                 memset(value,0,TXT_LENGTH);
    //                 strcat(cmd, buf + 5);
    //                 key_len=0;
    //                 value_len=0;
    //                 status = CMD;

    //             }else if(strncmp(buf,"value_len: ",11) == 0){
    //                 sscanf(buf, "value_len: %d", &value_len);
    //             }else{
    //                 strcat(key, buf);
    //             }
    //             break;
    //         case VALUE:

    //             if(strncmp(buf,"cmd: ",5) == 0){
    //                 int len = strlen(value);
    //                 if(len>0 && value[len-1] == '\n'){
    //                     value[len-1] = '\0';
    //                 }
    //                 cmd[strcspn(cmd,"\r\n")]=0;
    //                 if(key_len > 0 && strlen(key) >= key_len){
    //                     key[key_len - 1] = '\0';
    //                 }

    //                 if(value_len > 0 && strlen(value) >= value_len){
    //                     value[value_len - 1] = '\0';
    //                 }
    //                 kvs_txt_load(cmd, key, value);
    //                 memset(cmd,0,TXT_LENGTH);
    //                 memset(key,0,TXT_LENGTH);
    //                 memset(value,0,TXT_LENGTH);
    //                 strcat(cmd, buf + 5);
    //                 key_len=0;
    //                 value_len=0;
    //                 status = CMD;
    //             }else{
    //                 strcat(value, buf);
    //             }
    //             break;
    //     }
            
    //     memset(buf, 0, strlen(buf));
    // }
    // if(strlen(key) > 0){
    //     int len = key_len;
    //     if(key_len > 0 && strlen(key) >= key_len){
    //         key[key_len - 1] = '\0';
    //     }
    // }
    // if(strlen(value) > 0){
    //     int len = value_len;
    //     if(value_len > 0 && strlen(value) >= value_len){
    //         value[value_len - 1] = '\0';
    //     }
    // }
    // if(strlen(cmd) > 0){
    //     kvs_txt_load(cmd,key,value);
    //     memset(cmd,0,TXT_LENGTH);
    //     memset(key,0,TXT_LENGTH);
    //     memset(value,0,TXT_LENGTH);
    //     value_len=0;
    //     status = KEY;
    // }
        printf("%s\n",buf);
        fputs(buf, fp);
    
}

int kvs_txt_write(char *cmd, char* key, char* value, int *key_len, int *value_len){
    if(key == NULL || value == NULL) return -1;
    char* filename = "add.txt";
#if ADDSTORAGE
    FILE *fp = fopen(filename, "a+");
    if(fp == NULL) return -1;

    fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\nvalue_len: %d\nvalue: %s\n",cmd,*key_len,key,*value_len,value);
    fflush(fp);
    fclose(fp);
#endif
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
    char* filename = "add.txt";
    
#if 0

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
    char* filename = "add.txt";
#if ADDSTORAGE

    FILE *fp = fopen(filename, "a+");
    if(fp == NULL) return -1;

    fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\n",cmd,*key_len,key);
    fflush(fp);
    fclose(fp);

#endif
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
    char* filename = "add.txt";
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
    char* filename = "add.txt";
    if(key == NULL) return NULL;

#if ADDSTORAGE
    FILE *fp = fopen(filename, "a+");
    if(fp == NULL) return NULL;

    fprintf(fp, "cmd: %s\nkey_len: %d\nkey: %s\n", cmd, *key_len, key);
    fflush(fp);

    fclose(fp);
#endif
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

