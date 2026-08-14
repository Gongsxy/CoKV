
#include "kvstore.h"

static char *global_push_msg = NULL;

int kvs_protocol_push(char *msg){
    kvs_push_init();
    strcat(global_push_msg,msg);
    int length = strlen(global_push_msg);
    // printf("push:%s\n", global_push_msg);
    return length;
}

int kvs_push_get(char *msg){
    // printf("glo:%s\n",global_push_msg);
    if(msg == NULL || global_push_msg == NULL) return -1;
    strcpy(msg, global_push_msg);
    return strlen(msg);
}

void kvs_push_clear(){
    if(global_push_msg == NULL) return;
    memset(global_push_msg, 0, 3072000);
}

int kvs_push_init(){
    if(global_push_msg == NULL){
        global_push_msg = malloc(3072000);
        if(global_push_msg == NULL){
            perror("malloc push buffer failed");
            return -1;
        }
    }
    return 0;
}

void kvs_push_destroy(){
    if(global_push_msg != NULL){
        free(global_push_msg);
        global_push_msg = NULL;
    }
    
}