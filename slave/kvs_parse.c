#include "kvs_parse.h"

#include <ctype.h>
#include <string.h>

static int read_num(const char *buf, size_t n, size_t *pos, size_t limit, char preflex, size_t *out){

    if(buf == NULL) return KVS_PARSE_ERR;

    size_t i = *pos;

    if (i >= n) return KVS_PARSE_INCOMPLETE;
    if(buf[i] != preflex) return KVS_PARSE_ERR;

    i++;

    size_t len = 0;
    size_t digits = 0;
    while(i < n && buf[i] >= '0' && buf[i] <= '9'){

        size_t d = buf[i] - '0';

        if(digits > 0 && len == 0) return KVS_PARSE_ERR; 

        if(d > limit || len > (limit - d) / 10) return KVS_PARSE_ERR;

        len = len * 10 + d;


        digits++;
        i++;

    }

    if(i >= n) return KVS_PARSE_INCOMPLETE;
    if(digits == 0 || buf[i] != '\r') return KVS_PARSE_ERR;
    if(i + 1>= n) return KVS_PARSE_INCOMPLETE;
    if(digits == 0 || buf[i + 1] != '\n') return KVS_PARSE_ERR;
    

    *out = len;
    *pos = i + 2;

    return KVS_PARSE_OK;
}

int kvs_parse(char *buf, size_t n, kvs_req_t *req){

    if (req == NULL) return KVS_PARSE_ERR;
    if (n == 0) return KVS_PARSE_INCOMPLETE;
    if (buf == NULL) return KVS_PARSE_ERR;

    size_t argc = 0;
    size_t pos = 0;
    int r = read_num(buf, n, &pos, KVS_ARGC_MAX, '*', &argc);
    if(r != KVS_PARSE_OK) return r;
    if (argc < 1) return KVS_PARSE_ERR;

    char *argv[KVS_ARGC_MAX] = {0};
    size_t argl[KVS_ARGC_MAX] = {0};
    
    for(size_t i = 0; i < argc; i++){

        size_t out = 0; 
        int r = read_num(buf, n, &pos, KVS_BULK_LEN_MAX, '$', &out);
        if(r != KVS_PARSE_OK) return r;

        if (out > n - pos)     return KVS_PARSE_INCOMPLETE;
        if (n - pos - out < 2) return KVS_PARSE_INCOMPLETE;
        if (buf[pos + out] != '\r' || buf[pos + out + 1] != '\n') return KVS_PARSE_ERR;

        argv[i] = (char *)buf + pos;
        argl[i] = out;

        pos += out + 2;
    }

    req->argc = (int)argc;
    
    memcpy(req->argv, argv, sizeof(argv));
    memcpy(req->argl, argl, sizeof(argl));

    if (argl[0] < KVS_CMD_NAME_MAX) {
        for (size_t k = 0; k < argl[0]; k++) {
            char c = argv[0][k];
            req->cmd[k] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;  
        }
        req->cmd[argl[0]] = '\0';
    } else {
        req->cmd[0] = '\0';   
    }  

    req->frame_len = pos;

    return KVS_PARSE_OK;
}
