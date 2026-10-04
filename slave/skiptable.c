

#include "kvstore.h"

// kvs_skiplist_t *globle_skiplist;

static int random_level(){
    int level = 1;

    while((rand()%2) && level < MAX_LEVEL){
        level++;
    }

    return level;
}

static skiplist_node* create_node(int level, char *key, char *value){

    skiplist_node *node;

    node = malloc(sizeof(skiplist_node));


    if(node == NULL) return NULL;

    memset(node,0,sizeof(skiplist_node));

    node->key = malloc(strlen(key)+1);
    node->level = level;

    strcpy(node->key,key);

    node->value = malloc(strlen(value)+1);

    strcpy(node->value,value);

    memset(node->forward,0, sizeof(node->forward));

    return node;
}


int kvs_skiplist_create(kvs_skiplist_t *inst){


    if(inst==NULL) return -1;

    static int init_random = 0;
    if(!init_random)
    {
        srand(time(NULL));

        init_random = 1;
    }

    inst->level = 1;

    inst->header = malloc(sizeof(skiplist_node));

    if(inst->header == NULL) return -1;

    memset(inst->header, 0, sizeof(skiplist_node));

    return 0;
}


void kvs_skiplist_destroy(kvs_skiplist_t *inst){

    if(inst==NULL) return ;

    skiplist_node *cur;

    cur = inst->header->forward[0];

    while(cur){

        skiplist_node *tmp = cur->forward[0];

        free(cur->key);

        free(cur->value);

        free(cur);

        cur = tmp;
    }

    free(inst->header);
    inst->header = NULL;
}


static skiplist_node* skiplist_search( kvs_skiplist_t *inst, char *key){
    if(inst==NULL || key==NULL) return NULL;

    skiplist_node *cur = inst->header;

    int i;

    for(i = inst->level-1; i >= 0; i --){
        while(cur->forward[i] && strcmp(cur->forward[i]->key,key) < 0){
            cur=cur->forward[i];
        }
    }

    cur=cur->forward[0];

    if(cur && strcmp(cur->key,key)==0) return cur;

    return NULL;
}


int kvs_skiplist_set(kvs_skiplist_t *inst, char *key, char *value)
{

    if(inst==NULL || key==NULL || value==NULL) return -1;

    skiplist_node *update[MAX_LEVEL];

    skiplist_node *cur = inst->header;

    int i;

    for(i = inst->level-1; i >= 0; i --){

        while(cur->forward[i] && strcmp(cur->forward[i]->key,key) < 0){
            cur=cur->forward[i];
        }

        update[i]=cur;
    }

    cur=cur->forward[0];

    if(cur && strcmp(cur->key,key) == 0)  return 1;
    int level=random_level();

    if(level>inst->level){

        for(i=inst->level;i<level;i++){
            update[i]=inst->header;
        }

        inst->level=level;
    }

    skiplist_node *node = create_node(level,key,value);

    if(node==NULL) return -1;

    for(i=0;i<level;i++){
        node->forward[i] = update[i]->forward[i];
        update[i]->forward[i]=node;
    }

    return 0;
}


char* kvs_skiplist_get(kvs_skiplist_t *inst,char *key){

    skiplist_node *node = skiplist_search(inst,key);

    if(node) return node->value;

    return NULL;
}


int kvs_skiplist_mod(kvs_skiplist_t *inst,char *key,char *value){
    skiplist_node *node = skiplist_search(inst,key);

    if(node==NULL) return 1;

    free(node->value);

    node->value = malloc(strlen(value)+1);

    strcpy(node->value,value);

    return 0;
}



int kvs_skiplist_del(kvs_skiplist_t *inst,char *key){
    skiplist_node *update[MAX_LEVEL];

    skiplist_node *cur = inst->header;

    int i;

    for(i=inst->level-1;i>=0;i--){

        while(cur->forward[i] && strcmp(cur->forward[i]->key,key)<0){
            cur=cur->forward[i];
        }

        update[i]=cur;
    }

    cur=cur->forward[0];

    if(cur==NULL || strcmp(cur->key,key) != 0) return 1;

    for(i=0;i<inst->level;i++){
        if(update[i]->forward[i] != cur) break;

        update[i]->forward[i] = cur->forward[i];

    }

    free(cur->key);
    free(cur->value);
    free(cur);

    while(inst->level > 1 && inst->header->forward[inst->level-1] == NULL){
        inst->level--;
    }

    return 0;
}

int kvs_skiplist_exist(kvs_skiplist_t *inst, char *key){
   
    if(skiplist_search(inst,key))
        return 0;

    return 1;
}

int kvs_skiplist_save(kvs_skiplist_t *inst, char *msg){
    if(inst == NULL || msg == NULL) return -1;

    skiplist_node *cur = inst->header->forward[0];
    while (cur) {
        int key_len = 0;
        int value_len = 0;
        char tmp[128] = {0};

        key_len = strlen(cur->key);
        value_len = strlen(cur->value);

        sprintf(tmp, "cmd: SSET\r\nkey_len: %d\nkey: %s\nvalue_len: %d\nvalue: %s\n",key_len, cur->key, value_len, cur->value);
        strcat(msg, tmp);

        cur = cur->forward[0];
    }

    return 0;

}