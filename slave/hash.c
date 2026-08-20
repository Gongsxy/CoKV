


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "kvstore.h"


typedef struct hashtable_s kvs_hash_t;


//Connection 
// 'C' + 'o' + 'n'
static int _hash(char *key, int size) {

	if (!key) return -1;

	int sum = 0;
	int i = 0;

	while (key[i] != 0) {
		sum += key[i];
		i ++;
	}

	return sum % size;

}

hashnode_t *_create_node(char *key, char *value) {

	hashnode_t *node = (hashnode_t*)malloc(sizeof(hashnode_t));
	if (!node) return NULL;
	
	#if ENABLE_HASH
	// node->key = kvs_malloc(strlen(key) + 1);
	node->key = calloc(1, strlen(key) + 1);
	if(!node->key) return NULL;

	memset(node->key, 0, strlen(key) + 1);
	strcpy(node->key, key);

	// node->value = kvs_malloc(strlen(value) + 1);
	node->value = calloc(1, strlen(value) + 1);
	if(!node->value) return NULL;

	memset(node->value, 0, strlen(value) + 1);
	strcpy(node->value, value);
#else
	strncpy(node->key, key, MAX_KEY_LEN);
	strncpy(node->value, value, MAX_VALUE_LEN);
#endif

	node->next = NULL;

	return node;
}


//
int kvs_hash_create(kvs_hash_t *hash) {

	if (!hash) return -1;

	memset(hash, 0, sizeof(kvs_hash_t));

	// hash->nodes = (hashnode_t**)malloc(sizeof(hashnode_t*) * MAX_TABLE_SIZE);
	hash->nodes = (hashnode_t**)calloc(1, sizeof(hashnode_t*) * MAX_TABLE_SIZE);
	if (!hash->nodes) return -1;

	hash->max_slots = MAX_TABLE_SIZE;
	hash->count = 0; 

	return 0;
}

// 
void kvs_hash_destory(kvs_hash_t *hash) {

	if (!hash) return;

	int i = 0;
	for (i = 0;i < hash->max_slots;i ++) {
		hashnode_t *node = hash->nodes[i];

		while (node != NULL) { // error

			hashnode_t *tmp = node;
			node = node->next;
			hash->nodes[i] = node;
			
			kvs_free(tmp);
			
		}
	}

	kvs_free(hash->nodes);
	
}



// mp
int kvs_hash_set(kvs_hash_t *hash, char *key, char *value) {

	if (!hash || !key || !value) return -1;

	int idx = _hash(key, MAX_TABLE_SIZE);

	hashnode_t *node = hash->nodes[idx];
#if 1
	while (node != NULL) {
		if (strcmp(node->key, key) == 0) { // exist
			return 1;
		}
		node = node->next;
	}
#endif

	hashnode_t *new_node = _create_node(key, value);
	new_node->next = hash->nodes[idx];
	hash->nodes[idx] = new_node;
	
	hash->count ++;

	return 0;
}


char * kvs_hash_get(kvs_hash_t *hash, char *key) {

	if (!hash || !key) return NULL;

	int idx = _hash(key, MAX_TABLE_SIZE);

	hashnode_t *node = hash->nodes[idx];

	while (node != NULL) {

		if (strcmp(node->key, key) == 0) {
			return node->value;
		}

		node = node->next;
	}

	return NULL;

}


int kvs_hash_count(kvs_hash_t *hash) {
	return hash->count;
}

int kvs_hash_del(kvs_hash_t *hash, char *key) {
	if (!hash || !key) return -2;

	int idx = _hash(key, MAX_TABLE_SIZE);

	hashnode_t *head = hash->nodes[idx];
	if (head == NULL) return -1; // noexist
	// head node
	if (strcmp(head->key, key) == 0) {
		hashnode_t *tmp = head->next;
		hash->nodes[idx] = tmp;
		
		kvs_free(head);
		hash->count --;
		
		return 0;
	}

	hashnode_t *cur = head;
	while (cur->next != NULL) {
		if (strcmp(cur->next->key, key) == 0) break; // search node
		
		cur = cur->next;
	}

	if (cur->next == NULL) {
		return -1;
	}

	hashnode_t *tmp = cur->next;
	cur->next = tmp->next;
	kvs_free(tmp);
	hash->count --;
	
	return 0;
}

int kvs_hash_mod(kvs_hash_t *hash, char *key, char *value){
	if (!hash || !key || !value) return -2;

	int idx = _hash(key, MAX_TABLE_SIZE);

	hashnode_t *node = hash->nodes[idx];

	while (node != NULL) {

		if (strcmp(node->key, key) == 0) {
			break;
		}

		node = node->next;
	}

	if(node == NULL) return 1;

	kvs_free(node->value);

	char *kvalue = kvs_malloc(strlen(value) + 1);
	if(kvalue == NULL) return -2;
	memset(kvalue, 0, strlen(value) + 1);
	strncpy(kvalue, value, strlen(value));

	node->value = kvalue;

	return 0;
}

int kvs_hash_exist(kvs_hash_t *hash, char *key) {

	char *value = kvs_hash_get(hash, key);
	if (!value) return 1;
	else return 0;
	
}

int kvs_hash_save(kvs_hash_t *hash, char *msg){
    if(hash == NULL || msg == NULL)
        return -1;

    int i = 0;

    for(i = 0; i < hash->max_slots; i++){

        hashnode_t *node = hash->nodes[i];

        while(node != NULL){

            int key_len = strlen(node->key);
            int value_len = strlen(node->value);

            char tmp[256] = {0};

            sprintf(tmp,"cmd: HSET\r\nkey_len: %d\r\nkey: %s\r\nvalue_len: %d\r\nvalue: %s\r\n",key_len,node->key,value_len,node->value
            );

            strcat(msg, tmp);

            node = node->next;
        }
    }

    return 0;
}

