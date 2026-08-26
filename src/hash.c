


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "kvstore.h"


typedef struct hashtable_s kvs_hash_t;


//Connection 
// 'C' + 'o' + 'n'
static int _hash(char *key, int key_len, int size) {
    if (!key || key_len < 0 || size <= 0) return -1;

    unsigned int sum = 0;
    for (int i = 0; i < key_len; i++) {
        sum += (unsigned char)key[i];  
    }
    return (int)(sum % size);
}

static hashnode_t *_create_node(char *key, char *value, int key_len, int value_len) {

	hashnode_t *node = (hashnode_t*)malloc(sizeof(hashnode_t));
	if (!node) return NULL;
	
	// node->key = kvs_malloc(strlen(key) + 1);
	node->key = calloc(1, key_len);
	if(!node->key) {
		free(node);
		return NULL;
	}

	memcpy(node->key, key, key_len);
	node->key_len = key_len;
	
	// node->value = kvs_malloc(strlen(value) + 1);
	node->value = calloc(1, value_len);
	if(!node->value) {
		free(node->key);
        free(node);
		return NULL;
	}

	memcpy(node->value, value, value_len);
    node->value_len = value_len;

	node->next = NULL;

	return node;
}

static void _free_node(hashnode_t *node) {
    if (!node) return;
    free(node->key);
    free(node->value);
    free(node);
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
			
			_free_node(tmp);
			
		}
	}
	free(hash->nodes);
    hash->nodes = NULL;
    hash->count = 0;
	
}



// mp
int kvs_hash_set(kvs_hash_t *hash, char *key, char *value, int key_len, int value_len) {

	if (!hash || !key || key_len < 0 || !value || value_len < 0) return -1;

	int idx = _hash(key, key_len, hash->max_slots);
	if (idx < 0) return -1;

	hashnode_t *node = hash->nodes[idx];

	while (node != NULL) {
		if (node->key_len == key_len && memcmp(node->key, key, key_len) == 0) {
            return 1;  
        }
        node = node->next;
	}

	hashnode_t *new_node = _create_node(key, value, key_len, value_len);
	if (!new_node) return -1;

	new_node->next = hash->nodes[idx];
	hash->nodes[idx] = new_node;
	
	hash->count ++;
	return 0;
}


char * kvs_hash_get(kvs_hash_t *hash, char *key, int key_len, int *value_len) {

	if (!hash || !key || key_len < 0) return NULL;

	int idx = _hash(key, key_len, hash->max_slots);
	if (idx < 0) return NULL;

	hashnode_t *node = hash->nodes[idx];

	while (node != NULL) {
		if (node->key_len == key_len && memcmp(node->key, key, key_len) == 0) {
            if (value_len) *value_len = node->value_len;
            return node->value;
        }
        node = node->next;
	}

	return NULL;
}


int kvs_hash_count(kvs_hash_t *hash) {
    return hash ? hash->count : 0;
}

int kvs_hash_del(kvs_hash_t *hash, char *key, int key_len) {
	if (!hash || !key || key_len < 0) return -2;

	int idx = _hash(key, key_len, hash->max_slots);
    if (idx < 0) return -2;

	hashnode_t *head = hash->nodes[idx];
	if (!head) return -1;

	
	// head node
	if (head->key_len == key_len && memcmp(head->key, key, key_len) == 0) {
        hash->nodes[idx] = head->next;
        _free_node(head);
        hash->count--;
        return 0;
    }

	hashnode_t *cur = head;
	while (cur->next != NULL) {
		if (cur->next->key_len == key_len &&
            memcmp(cur->next->key, key, key_len) == 0) {
            break;
        }
        cur = cur->next;
	}

	if (cur->next == NULL) {
		return -1;
	}

	hashnode_t *tmp = cur->next;
	cur->next = tmp->next;
	_free_node(tmp);
	hash->count --;
	
	return 0;
}

int kvs_hash_mod(kvs_hash_t *hash, char *key, char *value, int key_len, int value_len){
	if (!hash || !key || key_len < 0 || !value || value_len < 0) return -2;

    int idx = _hash(key, key_len, hash->max_slots);
    if (idx < 0) return -2;

	hashnode_t *node = hash->nodes[idx];

	while (node != NULL) {
		if (node->key_len == key_len && memcmp(node->key, key, key_len) == 0) {
            break;
        }
        node = node->next;
	}

	if (!node) return 1;

	kvs_free(node->value);

	char *new_value = (char *)malloc(value_len);
    if (!new_value) return -2;
    memcpy(new_value, value, value_len);
	
	free(node->value);
    node->value = new_value;
    node->value_len = value_len;
	return 0;
}

int kvs_hash_exist(kvs_hash_t *hash, char *key, int key_len) {
	return kvs_hash_get(hash, key, key_len, NULL) ? 0 : 1;
}

static int _serialize_size(kvs_hash_t *hash) {
    if (!hash) return 0;
    int total = sizeof(int); // count
    for (int i = 0; i < hash->max_slots; i++) {
        hashnode_t *node = hash->nodes[i];
        while (node) {
            total += sizeof(int) + node->key_len;
            total += sizeof(int) + node->value_len;
            node = node->next;
        }
    }
    return total;
}

int kvs_hash_save(kvs_hash_t *hash, char *msg){
    if (!hash || !msg ) return -1;

	char *p = msg;

    int i = 0;
    for(i = 0; i < hash->max_slots; i++){

        hashnode_t *node = hash->nodes[i];

        while(node != NULL){

			p += sprintf(p, "key_len: %d\r\nkey: ", node->key_len);
			memcpy(p, node->key, node->key_len);
            p += node->key_len;

			p += sprintf(p, "\r\nvalue_len: %d\r\nvalue: ", node->value_len);
			memcpy(p, node->value, node->value_len);
            p += node->value_len;

			p += sprintf(p, "\r\n");
            node = node->next;
        }
    }

    return p - msg;
}

