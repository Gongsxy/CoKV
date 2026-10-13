


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <zlib.h>

#include "kvstore.h"
#include "load_conf.h"
#include "mem_pool.h"
#include "uring_slot.h"


typedef struct hashtable_s kvs_hash_t;


#define PACK_LEN_OFF				4
#define CRC_LEN						4
#define KEY_VALUE_OFF				4
#define VERSION_OFF					4
#define COUNT_OFF					4
#define FULL_HDR_LEN   (PACK_LEN_OFF + CRC_LEN + VERSION_OFF + COUNT_OFF)


static Config cfg = {0};
static struct mp_pool *pool;
void cfg_init(void) {
    static int done = 0;
    if (done) return;
    done = 1;

	load_config("config/server.conf", &cfg);
    pool = mp_create(4096);
}

//Connection 
// 'C' + 'o' + 'n'
static int _hash(char *key, int key_len, int size) {
    if (!key || key_len <= 0 || size <= 0) return -1;

    unsigned int h = 2166136261u;
    for (int i = 0; i < key_len; i++) {
        h ^= (unsigned char)key[i];
        h *= 16777619u;
    }
    return (int)(h % (unsigned int)size);
}

#if 0
static hashnode_t *_create_node(char *key, char *value, int key_len, int value_len) {

	cfg_init();
	hashnode_t *node;
	node = (hashnode_t*)malloc(sizeof(hashnode_t));
	if (!node) return NULL;
	
	cfg_init();

	node->key = calloc(1, key_len + 1);
	if(!node->key) {
		free(node);
		return NULL;
	}

	memcpy(node->key, key, key_len);
	node->key_len = key_len;
	

	node->value = calloc(1, value_len + 1);
	// node->value = m_malloc(value_len + 1);
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
#endif


static hashnode_t *_create_node(char *key, char *value, int key_len, int value_len)
{

    size_t need = sizeof(hashnode_t) + (size_t)key_len + 1 + (size_t)value_len + 1;

    hashnode_t *node = malloc(need);
    if (!node) return NULL;

    char *p = (char *)(node + 1);

    node->key = p;
    memcpy(p, key, key_len);
    p[key_len] = '\0';
    p += key_len + 1;

    node->value = p;
    memcpy(p, value, value_len);
    p[value_len] = '\0';

    node->key_len   = key_len;
    node->value_len = value_len;
    node->next      = NULL;

    return node;
}



static void _free_node(hashnode_t *node) {
    if (!node) return;

	free(node);
}


//
int kvs_hash_create(kvs_hash_t *hash) {

	if (!hash) return -1;

	memset(hash, 0, sizeof(kvs_hash_t));

	cfg_init();

	hash->nodes = (hashnode_t**)calloc(1, sizeof(hashnode_t*) * MAX_TABLE_SIZE);
	// hash->nodes = (hashnode_t**)m_malloc( sizeof(hashnode_t*) * MAX_TABLE_SIZE);
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
int kvs_hash_set(kvs_hash_t *hash, char *key, char *value, size_t key_len, size_t value_len) {

	if (!hash || !key || !value) return -1;

    int idx = _hash(key, (int)key_len, hash->max_slots);
    if (idx < 0) return -1;

    hashnode_t *prev = NULL;
    hashnode_t *node = hash->nodes[idx];

    while (node != NULL) {
        if (node->key_len == (int)key_len && memcmp(node->key, key, key_len) == 0) {

            hashnode_t *nn = _create_node(key, value, (int)key_len, (int)value_len);
            if (!nn) return -2;

            nn->next = node->next;
            if (prev) prev->next   = nn;
            else      hash->nodes[idx] = nn;

            _free_node(node);
            return 1;
        }
        prev = node;
        node = node->next;
    }

    hashnode_t *new_node = _create_node(key, value, (int)key_len, (int)value_len);
    if (!new_node) return -1;

    new_node->next   = hash->nodes[idx];
    hash->nodes[idx] = new_node;
    hash->count++;

    return 0;
}


char * kvs_hash_get(kvs_hash_t *hash, char *key, size_t key_len, size_t *value_len) {

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

int kvs_hash_del(kvs_hash_t *hash, char *key, size_t key_len) {
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

int kvs_hash_mod(kvs_hash_t *hash, char *key, char *value, size_t key_len, size_t value_len){

    if (!hash || !key || !value) return -2;

    int idx = _hash(key, (int)key_len, hash->max_slots);
    if (idx < 0) return -2;

    hashnode_t *prev = NULL;
    hashnode_t *node = hash->nodes[idx];

    while (node != NULL) {
        if (node->key_len == (int)key_len && memcmp(node->key, key, key_len) == 0) break;
        prev = node;
        node = node->next;
    }

    if (!node) return 1;

    hashnode_t *nn = _create_node(key, value, (int)key_len, (int)value_len);
    if (!nn) return -2;

    nn->next = node->next;
    if (prev) prev->next       = nn;
    else      hash->nodes[idx] = nn;

    _free_node(node);
    return 0;
}

int kvs_hash_exist(kvs_hash_t *hash, char *key, size_t key_len) {
	return kvs_hash_get(hash, key, key_len, NULL) ? 0 : 1;
}

size_t kvs_hash_save(kvs_hash_t *hash, char **msg, size_t *length, uint32_t count){
    if (!hash || !msg ) return 0;

	char *payload = *msg + FULL_HDR_LEN;
	char *p = payload;
	char *end = *msg + *length;

    int i = 0;
    for(i = 0; i < hash->max_slots; i++){

        hashnode_t *node = hash->nodes[i];
        while(node != NULL){
			
			size_t need = CRC_LEN + KEY_VALUE_OFF + node->key_len + KEY_VALUE_OFF + node->value_len;
			if (p + need > end) {
				size_t used   = (size_t)(p - *msg);
				size_t want   = used + need;
				size_t newcap = (*length > 0) ? *length : 4096;

				while (newcap < want) newcap += newcap / 2;

				void *ptr = realloc(*msg, newcap);
				if (!ptr) return 0;

				*msg    = ptr;
				*length = newcap;
				payload = *msg + FULL_HDR_LEN;
				p       = *msg + used;
				end     = *msg + *length;
				
            }
		
			uint32_t klen = node->key_len;
			char *crc_p = p;
			p += CRC_LEN;

			memcpy(p, &klen, KEY_VALUE_OFF); p += KEY_VALUE_OFF;
			memcpy(p, node->key, node->key_len); p += node->key_len;

			uint32_t vlen = node->value_len;
			memcpy(p, &vlen, KEY_VALUE_OFF); p += KEY_VALUE_OFF;
			memcpy(p, node->value, node->value_len); p += node->value_len;

			uint32_t c_r = (uint32_t)crc32(0L, crc_p + CRC_LEN, p - crc_p - CRC_LEN);
			memcpy(crc_p, &c_r, CRC_LEN);

            node = node->next;
        }

    }

	uint32_t pack_len = (uint32_t)(p - payload);  
	memcpy(*msg + CRC_LEN, &pack_len, PACK_LEN_OFF);

	memcpy(*msg + CRC_LEN + PACK_LEN_OFF, "V1.0", VERSION_OFF);
	memcpy(*msg + CRC_LEN + PACK_LEN_OFF + VERSION_OFF, &count, COUNT_OFF);

	uint32_t crc_total = (uint32_t)crc32(0L, *msg + CRC_LEN, (uInt)(p - *msg - CRC_LEN));
	memcpy(*msg, &crc_total, CRC_LEN);


    return (size_t)(FULL_HDR_LEN + pack_len);
}


int kvs_hash_save_slave(kvs_hash_t *hash, char **msg, size_t *length){
    if (!hash || !msg ) return -1;

	char *p = *msg;
	char *end = *msg + *length;
	
    int i = 0;
    for(i = 0; i < hash->max_slots; i++){

        hashnode_t *node = hash->nodes[i];
        while(node != NULL){
			
			size_t need = KEY_VALUE_OFF + node->key_len + KEY_VALUE_OFF + node->value_len;
			if (p + need > end) {
				size_t p_offset = p - *msg;
                size_t want = p_offset + need;
                size_t newcap = *length ? *length : (1024 * 1024);
                while (newcap < want) newcap += newcap / 2; 

				void *ptr = realloc(*msg, newcap);
                if (!ptr) return -1;
				*msg    = ptr;
				*length = newcap;
				p       = *msg + p_offset;
				end     = *msg + *length;
            }
		
			uint32_t klen = node->key_len;
			memcpy(p, &klen, KEY_VALUE_OFF); p += KEY_VALUE_OFF;
			memcpy(p, node->key, node->key_len); p += node->key_len;

			uint32_t vlen = node->value_len;
			memcpy(p, &vlen, KEY_VALUE_OFF); p += KEY_VALUE_OFF;
			memcpy(p, node->value, node->value_len); p += node->value_len;

            node = node->next;
        }

    }

	size_t snap_len = (size_t)(p - *msg); 

	int fd = open("config/save_mes.tmp", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "[sync] open save_mes.tmp fail: %s\n", strerror(errno));
        return -1;
    }else {
        size_t w = 0;
        while (w < snap_len) {
            ssize_t n = write(fd, *msg + w, snap_len - w);
            if (n < 0) { if (errno == EINTR) continue; perror("write tmp"); break; }
            if (n == 0) { fprintf(stderr, "[sync] write tmp: 0 bytes\n"); break; }
            w += (size_t)n;
        }
        fsync(fd);
        close(fd);
    }

    return (int)(snap_len);

}
