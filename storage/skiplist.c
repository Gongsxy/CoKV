

#include "skiplist.h"


#include <stdlib.h>
#include <string.h>
#include <time.h>


#ifndef PACK_LEN_OFF
#define PACK_LEN_OFF				4
#define CRC_LEN						4
#define KEY_VALUE_OFF				4
#define VERSION_OFF					4
#define COUNT_OFF					4
#define FULL_HDR_LEN   (PACK_LEN_OFF + CRC_LEN + VERSION_OFF + COUNT_OFF)
#endif


/* 二進位安全的比較：先比共同長度，再比長度 */
static int _sk_cmp(const char *k1, int l1, const char *k2, int l2) {
    int n = l1 < l2 ? l1 : l2;
    int r = memcmp(k1, k2, (size_t)n);
    if (r) return r;
    return (l1 > l2) - (l1 < l2);
}


/* xorshift32，比 rand() 快，且不干擾全域隨機數 */
static uint32_t _sk_rand(kvs_skiplist_t *inst) {
    uint32_t x = inst->rnd;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    inst->rnd = x;
    return x;
}

static int _sk_random_level(kvs_skiplist_t *inst) {
    int level = 1;
    uint32_t threshold = (uint32_t)(KVS_SKIP_P * 0xFFFFFFFFu);
    while (level < KVS_SKIP_MAX_LEVEL && _sk_rand(inst) < threshold) level++;
    return level;
}


static kvs_sknode_t *_sk_create_node(int level, char *key, char *value,
                                     int key_len, int value_len)
{
    size_t fwd  = sizeof(kvs_sknode_t *) * (size_t)level;
    size_t need = sizeof(kvs_sknode_t) + fwd + (size_t)key_len + 1 + (size_t)value_len + 1;

    kvs_sknode_t *node = malloc(need);
    if (!node) return NULL;
    node->cls   = 0xFF;
    node->level = (uint8_t)level;
    memset(node->forward, 0, fwd);

    char *p = (char *)node->forward + fwd;

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


static void _sk_free_node(kvs_sknode_t *node) {
    if (!node) return;
    free(node);
}


/*
 * 從上往下找，update[i] 記錄第 i 層最後一個 < key 的節點。
 * 找到返回節點，否則 NULL。update 可為 NULL。
 */
static kvs_sknode_t *_sk_find(kvs_skiplist_t *inst, char *key, int key_len,
                              kvs_sknode_t **update)
{
    kvs_sknode_t *cur = inst->header;

    for (int i = inst->level - 1; i >= 0; i--) {
        while (cur->forward[i] &&
               _sk_cmp(cur->forward[i]->key, cur->forward[i]->key_len, key, key_len) < 0) {
            cur = cur->forward[i];
        }
        if (update) update[i] = cur;
    }

    cur = cur->forward[0];
    if (cur && _sk_cmp(cur->key, cur->key_len, key, key_len) == 0) return cur;
    return NULL;
}


/*
 * 更新 value：新 value 不比舊的長就原地覆寫，
 * 否則建同層數的新節點替換。成功 0，失敗 -1
 */
static int _sk_update_value(kvs_skiplist_t *inst, kvs_sknode_t *node,
                            kvs_sknode_t **update, char *value, int value_len)
{
    (void)inst;

    if (value_len <= node->value_len) {
        if (value_len) memmove(node->value, value, value_len);
        node->value[value_len] = '\0';
        node->value_len = value_len;
        return 0;
    }

    kvs_sknode_t *nn = _sk_create_node(node->level, node->key, value, node->key_len, value_len);
    if (!nn) return -1;

    for (int i = 0; i < node->level; i++) {
        nn->forward[i]        = node->forward[i];
        update[i]->forward[i] = nn;
    }

    _sk_free_node(node);
    return 0;
}


//
int kvs_skiplist_create(kvs_skiplist_t *inst) {

    if (!inst) return -1;

    memset(inst, 0, sizeof(kvs_skiplist_t));

    inst->header = _sk_create_node(KVS_SKIP_MAX_LEVEL, "", "", 0, 0);
    if (!inst->header) return -1;

    inst->level = 1;
    inst->count = 0;

    inst->rnd = (uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)inst;
    if (inst->rnd == 0) inst->rnd = 2463534242u;

    return 0;
}

//
void kvs_skiplist_destory(kvs_skiplist_t *inst) {

    if (!inst || !inst->header) return;

    kvs_sknode_t *cur = inst->header->forward[0];
    while (cur) {
        kvs_sknode_t *next = cur->forward[0];
        _sk_free_node(cur);
        cur = next;
    }

    _sk_free_node(inst->header);
    inst->header = NULL;
    inst->level  = 1;
    inst->count  = 0;
}


/* 返回：0 新增，1 已存在並覆蓋，-1 新增失敗，-2 覆蓋失敗 */
int kvs_skiplist_set(kvs_skiplist_t *inst, char *key, char *value, size_t key_len, size_t value_len) {

    if (!inst || !inst->header || !key || !value) return -1;

    kvs_sknode_t *update[KVS_SKIP_MAX_LEVEL];

    kvs_sknode_t *node = _sk_find(inst, key, (int)key_len, update);
    if (node) {
        if (_sk_update_value(inst, node, update, value, (int)value_len) < 0) return -2;
        return 1;
    }

    int level = _sk_random_level(inst);
    if (level > inst->level) {
        for (int i = inst->level; i < level; i++) update[i] = inst->header;
        inst->level = level;
    }

    kvs_sknode_t *nn = _sk_create_node(level, key, value, (int)key_len, (int)value_len);
    if (!nn) return -1;

    for (int i = 0; i < level; i++) {
        nn->forward[i]        = update[i]->forward[i];
        update[i]->forward[i] = nn;
    }

    inst->count++;
    return 0;
}


char *kvs_skiplist_get(kvs_skiplist_t *inst, char *key, size_t key_len, size_t *value_len) {

    if (!inst || !inst->header || !key) return NULL;

    kvs_sknode_t *node = _sk_find(inst, key, (int)key_len, NULL);
    if (!node) return NULL;

    if (value_len) *value_len = node->value_len;
    return node->value;
}


int kvs_skiplist_count(kvs_skiplist_t *inst) {
    return inst ? (int)inst->count : 0;
}


/* 返回：0 成功，-1 不存在，-2 參數錯誤 */
int kvs_skiplist_del(kvs_skiplist_t *inst, char *key, size_t key_len) {

    if (!inst || !inst->header || !key) return -2;

    kvs_sknode_t *update[KVS_SKIP_MAX_LEVEL];

    kvs_sknode_t *node = _sk_find(inst, key, (int)key_len, update);
    if (!node) return -1;

    for (int i = 0; i < node->level; i++) {
        update[i]->forward[i] = node->forward[i];
    }
    _sk_free_node(node);
    inst->count--;

    while (inst->level > 1 && inst->header->forward[inst->level - 1] == NULL) {
        inst->level--;
    }

    return 0;
}


/* 返回：0 成功，1 不存在，-2 錯誤 */
int kvs_skiplist_mod(kvs_skiplist_t *inst, char *key, char *value, size_t key_len, size_t value_len) {

    if (!inst || !inst->header || !key || !value) return -2;

    kvs_sknode_t *update[KVS_SKIP_MAX_LEVEL];

    kvs_sknode_t *node = _sk_find(inst, key, (int)key_len, update);
    if (!node) return 1;

    if (_sk_update_value(inst, node, update, value, (int)value_len) < 0) return -2;
    return 0;
}


/* 返回：0 存在，1 不存在 */
int kvs_skiplist_exist(kvs_skiplist_t *inst, char *key, size_t key_len) {
    return kvs_skiplist_get(inst, key, key_len, NULL) ? 0 : 1;
}


/* 確保 *msg 至少有 want 位元組 */
static int _sk_reserve(char **msg, size_t *length, size_t want, size_t init_cap) {
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
size_t kvs_skiplist_save(kvs_skiplist_t *inst, char **msg, size_t *length, uint32_t count) {

    if (!inst || !inst->header || !msg || !length) return 0;

    if (_sk_reserve(msg, length, FULL_HDR_LEN, 4096) < 0) return 0;

    size_t used = FULL_HDR_LEN;

    for (kvs_sknode_t *node = inst->header->forward[0]; node; node = node->forward[0]) {

        size_t need = CRC_LEN + KEY_VALUE_OFF + node->key_len + KEY_VALUE_OFF + node->value_len;
        if (_sk_reserve(msg, length, used + need, 4096) < 0) return 0;

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


int kvs_skiplist_save_slave(kvs_skiplist_t *inst, char **msg, size_t *length) {

    if (!inst || !inst->header || !msg || !length) return -1;

    size_t used = 0;

    for (kvs_sknode_t *node = inst->header->forward[0]; node; node = node->forward[0]) {

        size_t need = KEY_VALUE_OFF + node->key_len + KEY_VALUE_OFF + node->value_len;
        if (_sk_reserve(msg, length, used + need, 1024 * 1024) < 0) return -1;

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