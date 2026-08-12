
#include "kvstore.h"

int kvs_protocol_push(char *msg){

    memset(global_push_msg, 0, INFO_LENGTH);
    int length = sprintf(global_push_msg,"%s", msg);
    return length;
}

int kvs_push_get(char *msg){
    if(msg == NULL) return -1;
    strcpy(msg, global_push_msg);
    return strlen(msg);
}

void kvs_push_clear(){
    memset(global_push_msg, 0, INFO_LENGTH);
}