

#include "kvstore.h"

// kvs_array_t *globle_array;

int kvs_array_create(kvs_array_t *inst){
    if(!inst) return -1;
    memset(inst, 0, sizeof(kvs_array_t));
    if(inst->table){
        return 1;
    }
    inst->table = kvs_malloc(sizeof(kvs_array_items_t) * KVS_ARRAY_SIZE);
    if(!inst->table) return -2;

    inst->total = 0;
    return 0;
};

int kvs_array_destory(kvs_array_t *inst){
    if(!inst) return -1;
    if(inst->table){
        kvs_free(inst->table);
    }
    return 0;
};

char * kvs_array_get(kvs_array_t *inst, char *key){

    if(inst == NULL || key == NULL) return NULL;
    int i = 0;
    for(i = 0; i < inst->total; i ++){
        if(inst->table[i].key == NULL){
            continue;
        }
        if(strcmp(inst->table[i].key,key) == 0){
            return inst->table[i].value;
        }
    }
    return NULL;
};

int kvs_array_set(kvs_array_t *inst, char *key, char *value){
    if(inst == NULL || key == NULL || value == NULL) return -1;
    
    if(kvs_array_get(inst,key)){
        return 1;
    }
    if(inst->total == KVS_ARRAY_SIZE) return -1;

    char *kcopy = kvs_malloc(strlen(key) + 1);
    memset(kcopy,0,strlen(key) + 1);
    strcpy(kcopy,key);

    char *kvalue = kvs_malloc(strlen(value) + 1);
    memset(kvalue,0,strlen(value) + 1);
    strcpy(kvalue,value);

    int i = 0;
    for(i = 0; i < inst->total; i ++){
        if(inst->table[i].key == NULL){
            inst->table[i].key = kcopy;
            inst->table[i].value = kvalue;
            // inst->total ++;

            inst->table[i].key = kcopy;
            inst->table[i].value = kvalue;
            inst->total++;  
            return 0;
        }
    }

    if(i == inst->total && i < KVS_ARRAY_SIZE){
        
        inst->table[i].key = kcopy;
        inst->table[i].value = kvalue;
        inst->total ++;
        return 0;
    }
    return 0;
};

int kvs_array_del(kvs_array_t *inst, char *key){
    if(inst == NULL || key == NULL) return -1;
    int i = 0;
    for(i = 0; i < inst->total; i ++){
        if(inst->table[i].key == NULL) continue;

        if(strcmp(inst->table[i].key, key) == 0){
            kvs_free(inst->table[i].key);
            inst->table[i].key = NULL;

            kvs_free(inst->table[i].value);
            inst->table[i].value = NULL;
            return 0;
        }
    }
    return i;
}

int kvs_array_mod(kvs_array_t *inst, char *key, char *value){

    if(inst == NULL || key == NULL || value == NULL) return -1;

    int i = 0;
    for(i = 0; i < inst->total; i ++){
        if(inst->table[i].key == NULL){
            continue;
        }
        if(strcmp(inst->table[i].key,key) == 0){
            kvs_free(inst->table[i].value);

            char *kvalue = kvs_malloc(strlen(value) + 1);
            memset(kvalue,0,strlen(value) + 1);
            strcpy(kvalue,value);
            inst->table[i].value = kvalue;

            return 0;
        }
    }
    return i;
};

int kvs_array_exist(kvs_array_t *inst, char *key){
    if(inst == NULL || key == NULL) return -1;

    if(!kvs_array_get(inst,key)){
        return 1;
    }
    return 0;
}

int kvs_array_save(kvs_array_t *inst, char *msg){
    if(inst == NULL) return -1;

    int i = 0;
    for(i = 0; i < inst->total; i ++){
        int key_len = 0;
        int value_len = 0;
        char tmp[128] = {0};
        key_len = strlen(inst->table[i].key);
        value_len = strlen(inst->table[i].value);
        sprintf(tmp, "key_len: %d\nkey: %s\nvalue_len: %d\nvalue: %s\n",key_len, inst->table[i].key, value_len, inst->table[i].value);
        strcat(msg,tmp);
    }
    return 0;
}
