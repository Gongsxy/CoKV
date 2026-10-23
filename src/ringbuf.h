#ifndef __RINGBUF_H__
#define __RINGBUF_H__

#include <stddef.h>   // size_t
#include <stdint.h>   // uint64_t

typedef struct {
    uint64_t write_off;
    uint64_t send_off;
    size_t   cap;
    size_t   mask;
    char    *buf;
} ringbuf_t;

extern ringbuf_t g_ring;
extern int g_repl_efd;
extern int g_sender_waiting;

int kvs_ringbuf_init(ringbuf_t *r, size_t cap);
void ringbuf_destroy(ringbuf_t *r);
int ringbuf_append(ringbuf_t *r, void *data, size_t len);
size_t ringbuf_peek(ringbuf_t *r, char **p);
int ringbuf_consume(ringbuf_t *r, size_t n);


#endif