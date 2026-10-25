#ifndef __KVS_PARSE_H__
#define __KVS_PARSE_H__


#include <stddef.h>


#define KVS_ARGC_MAX        3
#define KVS_CMD_NAME_MAX    16
#define KVS_BULK_LEN_MAX    (size_t)(512 * 1024 * 1024)


enum{
    KVS_PARSE_ERR           = -1,
    KVS_PARSE_INCOMPLETE    = 0,
    KVS_PARSE_OK            = 1
};


typedef struct {
    int argc;
    char *argv[KVS_ARGC_MAX];
    size_t argl[KVS_ARGC_MAX];
    char cmd[KVS_CMD_NAME_MAX];
    size_t frame_len;
} kvs_req_t;

int kvs_parse(char *buf, size_t n, kvs_req_t *req);


#endif