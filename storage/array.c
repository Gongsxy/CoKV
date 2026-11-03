#include "array.h"
#include "node_alloc.h"


#include <string.h>
#include <stdlib.h>
#include <unistd.h>




#ifndef PACK_LEN_OFF
#define PACK_LEN_OFF				4
#define CRC_LEN						4
#define KEY_VALUE_OFF				4
#define VERSION_OFF					4
#define COUNT_OFF					4
#define FULL_HDR_LEN   (PACK_LEN_OFF + CRC_LEN + VERSION_OFF + COUNT_OFF)
#endif


static kvs_arrnode_t *_arr_create_node(char *key, char *value, int key_len, int value_len)
{
    size_t need = sizeof(kvs_arrnode_t) + (size_t)key_len + 1 + (size_t)value_len + 1;

    uint8_t cls;
    kvs_arrnode_t *node = kvs_node_alloc(need, &cls);
    if (!node) return NULL;
    node->cls = cls;

    char *p = (char *)(node + 1);

    node->key = p;
    if (key_len) memcpy(p, key, key_len);
    p[key_len] = '\0';
    p += key_len + 1;

    node->value = p;
    if (value_len) memcpy(p, value, value_len);
    p[value_len] = '\0';

    node->key_len   = key_len;
    node->value_len = value_len;

    return node;
}


static void _arr_free_node(kvs_arrnode_t *node) {
    if (!node) return;
    kvs_node_free(node, node->cls);  
}


/* 返回下標，找不到 -1 */
static long _arr_find(kvs_array_t *inst, char *key, int key_len) {
    for (size_t i = 0; i < inst->total; i++) {
        kvs_arrnode_t *n = inst->table[i];
        if (n->key_len == key_len && memcmp(n->key, key, (size_t)key_len) == 0)
            return (long)i;
    }
    return -1;
}


/* 新 value 不比舊的長就原地覆寫，否則換新節點。成功 0，失敗 -1 */
static int _arr_update_value(kvs_array_t *inst, size_t idx, char *value, int value_len) {
    kvs_arrnode_t *node = inst->table[idx];

    if (value_len <= node->value_len) {
        if (value_len) memmove(node->value, value, value_len);
        node->value[value_len] = '\0';
        node->value_len = value_len;
        return 0;
    }

    kvs_arrnode_t *nn = _arr_create_node(node->key, value, node->key_len, value_len);
    if (!nn) return -1;

    inst->table[idx] = nn;
    _arr_free_node(node);
    return 0;
}


static int _arr_grow(kvs_array_t *inst) {
    size_t newcap = inst->cap ? inst->cap * 2 : KVS_ARRAY_INIT_CAP;
    void *p = realloc(inst->table, newcap * sizeof(kvs_arrnode_t *));
    if (!p) return -1;
    inst->table = p;
    inst->cap   = newcap;
    return 0;
}


//
int kvs_array_create(kvs_array_t *inst) {

    if (!inst) return -1;

    memset(inst, 0, sizeof(kvs_array_t));

    inst->table = malloc(sizeof(kvs_arrnode_t *) * KVS_ARRAY_INIT_CAP);
    if (!inst->table) return -1;

    inst->cap   = KVS_ARRAY_INIT_CAP;
    inst->total = 0;

    return 0;
}

//
void kvs_array_destory(kvs_array_t *inst) {

    if (!inst) return;

    for (size_t i = 0; i < inst->total; i++) _arr_free_node(inst->table[i]);
    kvs_node_pools_destroy();
    
    free(inst->table);
    inst->table = NULL;
    inst->total = 0;
    inst->cap   = 0;
}


/* 返回：0 新增，1 已存在並覆蓋，-1 新增失敗，-2 覆蓋失敗 */
int kvs_array_set(kvs_array_t *inst, char *key, char *value, size_t key_len, size_t value_len) {

    if (!inst || !inst->table || !key || !value) return -1;

    long idx = _arr_find(inst, key, (int)key_len);
    if (idx >= 0) {
        if (_arr_update_value(inst, (size_t)idx, value, (int)value_len) < 0) return -2;
        return 1;
    }

    if (inst->total == inst->cap && _arr_grow(inst) < 0) return -1;

    kvs_arrnode_t *node = _arr_create_node(key, value, (int)key_len, (int)value_len);
    if (!node) return -1;

    inst->table[inst->total++] = node;
    return 0;
}


char *kvs_array_get(kvs_array_t *inst, char *key, size_t key_len, size_t *value_len) {

    if (!inst || !inst->table || !key) return NULL;

    long idx = _arr_find(inst, key, (int)key_len);
    if (idx < 0) return NULL;

    kvs_arrnode_t *node = inst->table[idx];
    if (value_len) *value_len = node->value_len;
    return node->value;
}


int kvs_array_count(kvs_array_t *inst) {
    return inst ? (int)inst->total : 0;
}


/* 返回：0 成功，-1 不存在，-2 參數錯誤 */
int kvs_array_del(kvs_array_t *inst, char *key, size_t key_len) {

    if (!inst || !inst->table || !key) return -2;

    long idx = _arr_find(inst, key, (int)key_len);
    if (idx < 0) return -1;

    _arr_free_node(inst->table[idx]);
    inst->table[idx] = inst->table[inst->total - 1];   /* 最後一個補位 */
    inst->total--;

    return 0;
}


/* 返回：0 成功，1 不存在，-2 錯誤 */
int kvs_array_mod(kvs_array_t *inst, char *key, char *value, size_t key_len, size_t value_len) {

    if (!inst || !inst->table || !key || !value) return -2;

    long idx = _arr_find(inst, key, (int)key_len);
    if (idx < 0) return 1;

    if (_arr_update_value(inst, (size_t)idx, value, (int)value_len) < 0) return -2;
    return 0;
}


/* 返回：0 存在，1 不存在 */
int kvs_array_exist(kvs_array_t *inst, char *key, size_t key_len) {
    return kvs_array_get(inst, key, key_len, NULL) ? 0 : 1;
}


/* 確保 *msg 至少有 want 位元組 */
static int _arr_reserve(char **msg, size_t *length, size_t want, size_t init_cap) {
    if (*msg && *length >= want) return 0;

    size_t newcap = (*msg && *length > 0) ? *length : init_cap;
    if (newcap < 64) newcap = 64;
    while (newcap < want) newcap += newcap / 2;

    void *ptr = realloc(*msg, newcap);
    if (!ptr) return -1;

    *msg    = ptr;
    *length = newcap;
    return 0;
}


/*
 * 格式與 kvs_hash_save 相同：
 * [crc_total 4][pack_len 4]["V1.0" 4][count 4]
 * 每筆：[crc 4][klen 4][key][vlen 4][value]
 */
size_t kvs_array_save(kvs_array_t *inst, char **msg, size_t *length, uint32_t count) {

    if (!inst || !inst->table || !msg || !length) return 0;

    if (_arr_reserve(msg, length, FULL_HDR_LEN, 4096) < 0) return 0;

    size_t used = FULL_HDR_LEN;

    for (size_t i = 0; i < inst->total; i++) {
        kvs_arrnode_t *node = inst->table[i];

        size_t need = CRC_LEN + KEY_VALUE_OFF + node->key_len + KEY_VALUE_OFF + node->value_len;
        if (_arr_reserve(msg, length, used + need, 4096) < 0) return 0;

        char *p     = *msg + used;
        char *crc_p = p;
        p += CRC_LEN;

        uint32_t klen = node->key_len;
        memcpy(p, &klen, KEY_VALUE_OFF); p += KEY_VALUE_OFF;
        memcpy(p, node->key, node->key_len); p += node->key_len;

        uint32_t vlen = node->value_len;
        memcpy(p, &vlen, KEY_VALUE_OFF); p += KEY_VALUE_OFF;
        memcpy(p, node->value, node->value_len); p += node->value_len;

        uint32_t c_r = (uint32_t)crc32(0L, (Bytef *)(crc_p + CRC_LEN), (uInt)(p - crc_p - CRC_LEN));
        memcpy(crc_p, &c_r, CRC_LEN);

        used = (size_t)(p - *msg);
    }

    uint32_t pack_len = (uint32_t)(used - FULL_HDR_LEN);
    memcpy(*msg + CRC_LEN, &pack_len, PACK_LEN_OFF);

    memcpy(*msg + CRC_LEN + PACK_LEN_OFF, "V1.0", VERSION_OFF);
    memcpy(*msg + CRC_LEN + PACK_LEN_OFF + VERSION_OFF, &count, COUNT_OFF);

    uint32_t crc_total = (uint32_t)crc32(0L, (Bytef *)(*msg + CRC_LEN), (uInt)(used - CRC_LEN));
    memcpy(*msg, &crc_total, CRC_LEN);

    return (size_t)(FULL_HDR_LEN + pack_len);
}


int kvs_array_save_slave(kvs_array_t *inst, char **msg, size_t *length) {

    if (!inst || !inst->table || !msg || !length) return -1;

    size_t used = 0;

    for (size_t i = 0; i < inst->total; i++) {
        kvs_arrnode_t *node = inst->table[i];

        size_t need = KEY_VALUE_OFF + node->key_len + KEY_VALUE_OFF + node->value_len;
        if (_arr_reserve(msg, length, used + need, 1024 * 1024) < 0) return -1;

        char *p = *msg + used;

        uint32_t klen = node->key_len;
        memcpy(p, &klen, KEY_VALUE_OFF); p += KEY_VALUE_OFF;
        memcpy(p, node->key, node->key_len); p += node->key_len;

        uint32_t vlen = node->value_len;
        memcpy(p, &vlen, KEY_VALUE_OFF); p += KEY_VALUE_OFF;
        memcpy(p, node->value, node->value_len); p += node->value_len;

        used = (size_t)(p - *msg);
    }

    size_t snap_len = used;

    int fd = open("config/full.tmp", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "[sync] open full.tmp fail: %s\n", strerror(errno));
        return -1;
    } else {
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

    return (int)snap_len;
}