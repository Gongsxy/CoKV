
#include <errno.h>
#include <fcntl.h>
#include <zlib.h>

#include "kvstore.h"
#include "rbtree.h"


#ifndef PACK_LEN_OFF
#define PACK_LEN_OFF				4
#define CRC_LEN						4
#define KEY_VALUE_OFF				4
#define VERSION_OFF					4
#define COUNT_OFF					4
#define FULL_HDR_LEN   (PACK_LEN_OFF + CRC_LEN + VERSION_OFF + COUNT_OFF)
#endif

#define RB_NIL(t)   (&(t)->nil)
#define RED         KVS_RB_RED
#define BLACK       KVS_RB_BLACK


/* 二進位安全的比較：先比共同長度，再比長度 */
static int _rb_cmp(const char *k1, int l1, const char *k2, int l2) {
    int n = l1 < l2 ? l1 : l2;
    int r = memcmp(k1, k2, (size_t)n);
    if (r) return r;
    return (l1 > l2) - (l1 < l2);
}


static kvs_rbnode_t *_rb_create_node(kvs_rbtree_t *tree, char *key, char *value,
                                     int key_len, int value_len)
{
    size_t need = sizeof(kvs_rbnode_t) + (size_t)key_len + 1 + (size_t)value_len + 1;

    kvs_rbnode_t *node = malloc(need);
    if (!node) return NULL;
    node->cls = 0xFF;

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

    node->color  = RED;
    node->left   = RB_NIL(tree);
    node->right  = RB_NIL(tree);
    node->parent = RB_NIL(tree);

    return node;
}


static void _rb_free_node(kvs_rbnode_t *node) {
    if (!node) return;
    free(node);
}


static void _rb_left_rotate(kvs_rbtree_t *T, kvs_rbnode_t *x) {
    kvs_rbnode_t *nil = RB_NIL(T);
    kvs_rbnode_t *y = x->right;

    x->right = y->left;
    if (y->left != nil) y->left->parent = x;

    y->parent = x->parent;
    if (x->parent == nil)             T->root = y;
    else if (x == x->parent->left)    x->parent->left = y;
    else                              x->parent->right = y;

    y->left   = x;
    x->parent = y;
}


static void _rb_right_rotate(kvs_rbtree_t *T, kvs_rbnode_t *y) {
    kvs_rbnode_t *nil = RB_NIL(T);
    kvs_rbnode_t *x = y->left;

    y->left = x->right;
    if (x->right != nil) x->right->parent = y;

    x->parent = y->parent;
    if (y->parent == nil)             T->root = x;
    else if (y == y->parent->right)   y->parent->right = x;
    else                              y->parent->left = x;

    x->right  = y;
    y->parent = x;
}


static void _rb_insert_fixup(kvs_rbtree_t *T, kvs_rbnode_t *z) {

    while (z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            kvs_rbnode_t *y = z->parent->parent->right;
            if (y->color == RED) {
                z->parent->color         = BLACK;
                y->color                 = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    _rb_left_rotate(T, z);
                }
                z->parent->color         = BLACK;
                z->parent->parent->color = RED;
                _rb_right_rotate(T, z->parent->parent);
            }
        } else {
            kvs_rbnode_t *y = z->parent->parent->left;
            if (y->color == RED) {
                z->parent->color         = BLACK;
                y->color                 = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    _rb_right_rotate(T, z);
                }
                z->parent->color         = BLACK;
                z->parent->parent->color = RED;
                _rb_left_rotate(T, z->parent->parent);
            }
        }
    }
    T->root->color = BLACK;
}


static kvs_rbnode_t *_rb_minimum(kvs_rbtree_t *T, kvs_rbnode_t *x) {
    while (x->left != RB_NIL(T)) x = x->left;
    return x;
}


static kvs_rbnode_t *_rb_successor(kvs_rbtree_t *T, kvs_rbnode_t *x) {
    kvs_rbnode_t *nil = RB_NIL(T);
    if (x->right != nil) return _rb_minimum(T, x->right);

    kvs_rbnode_t *y = x->parent;
    while (y != nil && x == y->right) {
        x = y;
        y = y->parent;
    }
    return y;
}


static kvs_rbnode_t *_rb_search(kvs_rbtree_t *T, char *key, int key_len) {
    kvs_rbnode_t *nil  = RB_NIL(T);
    kvs_rbnode_t *node = T->root;

    while (node != nil) {
        int c = _rb_cmp(key, key_len, node->key, node->key_len);
        if (c < 0)      node = node->left;
        else if (c > 0) node = node->right;
        else            return node;
    }
    return NULL;
}


static void _rb_transplant(kvs_rbtree_t *T, kvs_rbnode_t *u, kvs_rbnode_t *v) {
    if (u->parent == RB_NIL(T))       T->root = v;
    else if (u == u->parent->left)    u->parent->left = v;
    else                              u->parent->right = v;
    v->parent = u->parent;
}


static void _rb_delete_fixup(kvs_rbtree_t *T, kvs_rbnode_t *x) {

    while (x != T->root && x->color == BLACK) {
        if (x == x->parent->left) {
            kvs_rbnode_t *w = x->parent->right;
            if (w->color == RED) {
                w->color         = BLACK;
                x->parent->color = RED;
                _rb_left_rotate(T, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == BLACK && w->right->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->right->color == BLACK) {
                    w->left->color = BLACK;
                    w->color       = RED;
                    _rb_right_rotate(T, w);
                    w = x->parent->right;
                }
                w->color         = x->parent->color;
                x->parent->color = BLACK;
                w->right->color  = BLACK;
                _rb_left_rotate(T, x->parent);
                x = T->root;
            }
        } else {
            kvs_rbnode_t *w = x->parent->left;
            if (w->color == RED) {
                w->color         = BLACK;
                x->parent->color = RED;
                _rb_right_rotate(T, x->parent);
                w = x->parent->left;
            }
            if (w->right->color == BLACK && w->left->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->left->color == BLACK) {
                    w->right->color = BLACK;
                    w->color        = RED;
                    _rb_left_rotate(T, w);
                    w = x->parent->left;
                }
                w->color         = x->parent->color;
                x->parent->color = BLACK;
                w->left->color   = BLACK;
                _rb_right_rotate(T, x->parent);
                x = T->root;
            }
        }
    }
    x->color = BLACK;
}


static void _rb_delete(kvs_rbtree_t *T, kvs_rbnode_t *z) {
    kvs_rbnode_t *nil = RB_NIL(T);
    kvs_rbnode_t *y = z;
    kvs_rbnode_t *x;
    unsigned char y_orig_color = y->color;

    if (z->left == nil) {
        x = z->right;
        _rb_transplant(T, z, z->right);
    } else if (z->right == nil) {
        x = z->left;
        _rb_transplant(T, z, z->left);
    } else {
        y = _rb_minimum(T, z->right);
        y_orig_color = y->color;
        x = y->right;
        if (y->parent == z) {
            x->parent = y;
        } else {
            _rb_transplant(T, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        _rb_transplant(T, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    if (y_orig_color == BLACK) _rb_delete_fixup(T, x);
}


/* 用 nn 原地取代 old 在樹中的位置（顏色、父子關係全部繼承） */
static void _rb_replace_node(kvs_rbtree_t *T, kvs_rbnode_t *old, kvs_rbnode_t *nn) {
    kvs_rbnode_t *nil = RB_NIL(T);

    nn->color  = old->color;
    nn->left   = old->left;
    nn->right  = old->right;
    nn->parent = old->parent;

    if (old->parent == nil)               T->root = nn;
    else if (old == old->parent->left)    old->parent->left = nn;
    else                                  old->parent->right = nn;

    if (old->left  != nil) old->left->parent  = nn;
    if (old->right != nil) old->right->parent = nn;
}


/*
 * 更新 value：新 value 不比舊的長就直接覆寫（不用 malloc），
 * 否則建新節點替換。成功 0，失敗 -1
 */
static int _rb_update_value(kvs_rbtree_t *T, kvs_rbnode_t *node, char *value, int value_len) {

    if (value_len <= node->value_len) {
        memmove(node->value, value, value_len);
        node->value[value_len] = '\0';
        node->value_len = value_len;
        return 0;
    }

    kvs_rbnode_t *nn = _rb_create_node(T, node->key, value, node->key_len, value_len);
    if (!nn) return -1;

    _rb_replace_node(T, node, nn);
    _rb_free_node(node);
    return 0;
}


static void _rb_free_tree(kvs_rbtree_t *T, kvs_rbnode_t *node) {
    while (node != RB_NIL(T)) {
        _rb_free_tree(T, node->left);
        kvs_rbnode_t *right = node->right;
        _rb_free_node(node);
        node = right;
    }
}


//
int kvs_rbtree_create(kvs_rbtree_t *tree) {

    if (!tree) return -1;

    memset(tree, 0, sizeof(kvs_rbtree_t));

    tree->nil.color  = BLACK;
    tree->nil.left   = RB_NIL(tree);
    tree->nil.right  = RB_NIL(tree);
    tree->nil.parent = RB_NIL(tree);

    tree->root  = RB_NIL(tree);
    tree->count = 0;

    return 0;
}

//
void kvs_rbtree_destory(kvs_rbtree_t *tree) {

    if (!tree) return;

    _rb_free_tree(tree, tree->root);

    tree->root  = RB_NIL(tree);
    tree->count = 0;
}


/* 返回：0 新增，1 已存在並覆蓋，-1 新增失敗，-2 覆蓋失敗 */
int kvs_rbtree_set(kvs_rbtree_t *tree, char *key, char *value, size_t key_len, size_t value_len) {

    if (!tree || !key || !value) return -1;

    kvs_rbnode_t *nil = RB_NIL(tree);
    kvs_rbnode_t *y = nil;
    kvs_rbnode_t *x = tree->root;
    int c = 0;

    while (x != nil) {
        y = x;
        c = _rb_cmp(key, (int)key_len, x->key, x->key_len);
        if (c < 0)      x = x->left;
        else if (c > 0) x = x->right;
        else {
            if (_rb_update_value(tree, x, value, (int)value_len) < 0) return -2;
            return 1;
        }
    }

    kvs_rbnode_t *z = _rb_create_node(tree, key, value, (int)key_len, (int)value_len);
    if (!z) return -1;

    z->parent = y;
    if (y == nil)    tree->root = z;
    else if (c < 0)  y->left    = z;
    else             y->right   = z;

    _rb_insert_fixup(tree, z);
    tree->count++;

    return 0;
}


char *kvs_rbtree_get(kvs_rbtree_t *tree, char *key, size_t key_len, size_t *value_len) {

    if (!tree || !key) return NULL;

    kvs_rbnode_t *node = _rb_search(tree, key, (int)key_len);
    if (!node) return NULL;

    if (value_len) *value_len = node->value_len;
    return node->value;
}


int kvs_rbtree_count(kvs_rbtree_t *tree) {
    return tree ? (int)tree->count : 0;
}


/* 返回：0 成功，-1 不存在，-2 參數錯誤 */
int kvs_rbtree_del(kvs_rbtree_t *tree, char *key, size_t key_len) {

    if (!tree || !key) return -2;

    kvs_rbnode_t *node = _rb_search(tree, key, (int)key_len);
    if (!node) return -1;

    _rb_delete(tree, node);
    _rb_free_node(node);
    tree->count--;

    return 0;
}


/* 返回：0 成功，1 不存在，-2 錯誤 */
int kvs_rbtree_mod(kvs_rbtree_t *tree, char *key, char *value, size_t key_len, size_t value_len) {

    if (!tree || !key || !value) return -2;

    kvs_rbnode_t *node = _rb_search(tree, key, (int)key_len);
    if (!node) return 1;

    if (_rb_update_value(tree, node, value, (int)value_len) < 0) return -2;
    return 0;
}


/* 返回：0 存在，1 不存在 */
int kvs_rbtree_exist(kvs_rbtree_t *tree, char *key, size_t key_len) {
    return kvs_rbtree_get(tree, key, key_len, NULL) ? 0 : 1;
}


/* 確保 *msg 至少有 want 位元組 */
static int _rb_reserve(char **msg, size_t *length, size_t want, size_t init_cap) {
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
 * 紅黑樹按 key 有序輸出
 */
size_t kvs_rbtree_save(kvs_rbtree_t *tree, char **msg, size_t *length, uint32_t count) {

    if (!tree || !msg || !length) return 0;

    if (_rb_reserve(msg, length, FULL_HDR_LEN, 4096) < 0) return 0;

    size_t used = FULL_HDR_LEN;
    kvs_rbnode_t *nil  = RB_NIL(tree);
    kvs_rbnode_t *node = (tree->root == nil) ? nil : _rb_minimum(tree, tree->root);

    for (; node != nil; node = _rb_successor(tree, node)) {

        size_t need = CRC_LEN + KEY_VALUE_OFF + node->key_len + KEY_VALUE_OFF + node->value_len;
        if (_rb_reserve(msg, length, used + need, 4096) < 0) return 0;

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


int kvs_rbtree_save_slave(kvs_rbtree_t *tree, char **msg, size_t *length) {

    if (!tree || !msg || !length) return -1;

    size_t used = 0;
    kvs_rbnode_t *nil  = RB_NIL(tree);
    kvs_rbnode_t *node = (tree->root == nil) ? nil : _rb_minimum(tree, tree->root);

    for (; node != nil; node = _rb_successor(tree, node)) {

        size_t need = KEY_VALUE_OFF + node->key_len + KEY_VALUE_OFF + node->value_len;
        if (_rb_reserve(msg, length, used + need, 1024 * 1024) < 0) return -1;

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

    int fd = open("config/full.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "[sync] open full.txt fail: %s\n", strerror(errno));
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