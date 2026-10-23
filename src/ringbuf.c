
#include "ringbuf.h"


#include <string.h>
#include <stdlib.h>




int kvs_ringbuf_init(ringbuf_t *r, size_t cap){

    if(r == NULL || cap == 0) return -1;
    if ((cap & (cap - 1)) != 0) return -1;

    r->buf = malloc(cap);
    if(r->buf == NULL) return -2;

    r->cap = cap;
    r->send_off = r->write_off = 0;

    r->mask = cap - 1;

    return 0;
}

void ringbuf_destroy(ringbuf_t *r) {
    if (r == NULL) return;
    free(r->buf);
    r->buf = NULL;
    r->cap = r->mask = 0;
    r->write_off = r->send_off = 0;
}

static inline size_t ringbuf_used(ringbuf_t *r){
    return (size_t)(r->write_off - r->send_off);
}

static inline size_t ringbuf_free(ringbuf_t *r){
    return r->cap - ringbuf_used(r);
}

int ringbuf_append(ringbuf_t *r, void *data, size_t len){

    if(r == NULL || data == NULL || len == 0) return -1;

    if(len > ringbuf_free(r)) return -2;

    size_t pos = r->write_off & r->mask;
    size_t first = r->cap - pos;
    if(first > len) first = len;

    memcpy(r->buf + pos, data, first);
    memcpy(r->buf, (char *)data + first, len - first); 

    r->write_off += len;

    return 0;
}

size_t ringbuf_peek(ringbuf_t *r, char **p){

    if(r == NULL || p == NULL) return 0;

    size_t used = ringbuf_used(r);
    size_t pos = r->send_off & r->mask;
    size_t first = r->cap - pos;

    if(first > used) first = used;

    *p = r->buf + pos;
    return first;
}


int ringbuf_consume(ringbuf_t *r, size_t n){
    if (r == NULL || n > ringbuf_used(r)) return -1;
    r->send_off += n;
    return 0;
}
