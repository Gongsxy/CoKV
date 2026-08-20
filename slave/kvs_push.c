
#include "kvstore.h"

static char *global_push_msg = NULL;
static int global_push_msg_len = 0;
int kvs_protocol_push(char *msg, int length){
    kvs_push_init();

    strncat(global_push_msg,msg, length);
    int len = strlen(global_push_msg);
    return len;
}

int kvs_push_get(char *msg){

    if(msg == NULL || global_push_msg == NULL) return -1;
    // printf("globle:%s\n", global_push_msg);
    memcpy(msg, global_push_msg, 1024 * 1024 * 8);
    return strlen(msg);


}

void kvs_push_clear(){
    if(global_push_msg == NULL) return;
    memset(global_push_msg, 0, 1024 * 1024 * 8);
}

int kvs_push_init(){
    if(global_push_msg == NULL){
        global_push_msg = malloc(1024 * 1024 * 8);
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