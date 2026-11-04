#define _POSIX_C_SOURCE 200809L 

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)


#include "kvstore.h"
#include "load_conf.h"
#include "uring_slot.h"
#include "rdma.h"
#include "mem_pool.h"
#include "ringbuf.h"

#include "../storage/storage.h"
#include "kvs_parse.h"

#include <zlib.h>
#include <sodium.h>
#include <sys/eventfd.h>
#include <signal.h>
#include <malloc.h>
#include <sys/uio.h>
#include <time.h>
#include <stdatomic.h>

#include <sched.h>
#include "../storage/node_alloc.h" 

#define PUT_LIT(dst,src)(memcpy(dst, src, sizeof(src) - 1), (int)(sizeof(src) - 1))

#define IO_URING_ADD        0


#if USE_JEMALLOC
const char *malloc_conf =
    "background_thread:true,dirty_decay_ms:1000,muzzy_decay_ms:0,"
    "retain:true,abort_conf:true";
#endif


kvs_array_t *globle_array = NULL;
kvs_rbtree_t *globle_rbtree = NULL;
kvs_hash_t *globle_hash = NULL;
kvs_skiplist_t *globle_skiplist = NULL;


const char *command[] = {
    "SET","GET","DEL","MOD","EXIST",
    "RSET","RGET","RDEL","RMOD","REXIST",
    "HSET","HGET","HDEL","HMOD","HEXIST",
    "SSET","SGET","SDEL","SMOD","SEXIST",
    "PING","QUIT","SYNC","INFO","SAVE",
    "ECHO","CLEAR"
};


enum {
    KVS_CMD_START = 0,
    KVS_CMD_SET = KVS_CMD_START,
    KVS_CMD_GET,
    KVS_CMD_DEL,
    KVS_CMD_MOD,
    KVS_CMD_EXIST,
// rbtree
    KVS_CMD_RSET,
    KVS_CMD_RGET,
    KVS_CMD_RDEL,
    KVS_CMD_RMOD,
    KVS_CMD_REXIST,
// hash
    KVS_CMD_HSET,
    KVS_CMD_HGET,
    KVS_CMD_HDEL,
    KVS_CMD_HMOD,
    KVS_CMD_HEXIST,
// skiplist
    KVS_CMD_SSET,
    KVS_CMD_SGET,
    KVS_CMD_SDEL,
    KVS_CMD_SMOD,
    KVS_CMD_SEXIST,

    KVS_CMD_PING,
    KVS_CMD_QUIT,
    KVS_CMD_SYNC,
    KVS_CMD_INFO,
    KVS_CMD_SAVE,
    KVS_CMD_ECHO,
    KVS_CMD_CLEAR,
    KVS_CMD_COUNT
};



Config server_cfg;


#if IO_URING_ADD
struct io_uring add_ring;
struct io_uring_params add_params;
#endif

static struct mp_pool *pool = NULL;
static struct mp_pool *cmd_pool = NULL;


ringbuf_t g_ring;
int g_repl_efd = -1;
int g_sender_waiting = 0;
int g_repl_cmd = 0;
int g_repl_collect = 0;

static __g_rdma gr = {0};


static int globle_add_fd = -1;
static int load_saved_msg = 1;


#define MAC_LEN crypto_auth_hmacsha256_BYTES
static unsigned char g_hmac_key[crypto_auth_hmacsha256_KEYBYTES];


static int get_add_fd(void){
    if(globle_add_fd < 0){
        globle_add_fd = open("config/add.txt", O_WRONLY | O_APPEND | O_CREAT, 0644);
        if(globle_add_fd < 0){
            perror("open config/add.txt");
            return -1;
        }
    }
    return globle_add_fd;
}



#define AOF_MAX_REC     512

static struct iovec g_aof_iov[AOF_MAX_REC * 2];
static uint32_t g_aof_crc[AOF_MAX_REC];
static int g_aof_num = 0;


static atomic_int g_aof_dirty = 0;
static atomic_int g_aof_fsync_err = 0;

static int writev_all(int fd, struct iovec *iov, size_t num){

    while(num > 0){
       ssize_t n = writev(fd, iov, num);

        if(n < 0){
            if (errno == EINTR) continue;
            return -1;
        }

        if (n == 0) {
            return -1; 
        }

        while(num > 0 && (size_t)n >= iov->iov_len){
            n -= iov->iov_len;
            iov++;
            num--;
        }

        if(num > 0){

            iov->iov_len -= n;
            iov->iov_base = (char *)iov->iov_base + n;

        }
    }
    return 0;
}

static void aof_flush(void){

    if(g_aof_num == 0) return ;

    int fd = get_add_fd();

    if(fd < 0 || writev_all(fd, g_aof_iov, g_aof_num * 2) < 0){
        perror("[aof] writev");
        abort();
    }

    g_aof_num = 0;

    atomic_store(&g_aof_dirty, 1);

    int err = atomic_load(&g_aof_fsync_err);
    if(err != 0){
        fprintf(stderr, "[aof] fdatasync failed: %s\n", strerror(err));
        abort();
    }
}


static void aof_add(const char *frame, size_t len){

    if(g_aof_num == AOF_MAX_REC) aof_flush();

    int i = g_aof_num++;

    g_aof_crc[i] = crc32(0L, (const Bytef *)frame, (uInt)len);

    g_aof_iov[i * 2].iov_base = &g_aof_crc[i];
    g_aof_iov[i * 2].iov_len = 4;
    g_aof_iov[i * 2 + 1].iov_base = (void *)frame;
    g_aof_iov[i * 2 + 1].iov_len = len;

}


static void *aof_fsync_thread(void *arg){

    int fd = (int)(intptr_t)arg;

    struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};

    for(;;){
        
        clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);

        if(atomic_exchange(&g_aof_dirty, 0) == 0){
            continue;
        }

        if(fdatasync(fd) < 0){
            atomic_store(&g_aof_fsync_err, errno);
            return NULL;
        }

    }

    return NULL;
}



static pid_t g_bgsave_pid = -1;
static int g_bgsave_last_ok = 0;
static unsigned g_bgsave_gen = 0;

static int pwrite_all(int fd, const char *buf, size_t len) {

    off_t off = 0;
    while (len > 0) {
        ssize_t n = pwrite(fd, buf, len, off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf += n;
        off += n;
        len -= (size_t)n;
    }
    return 0;
}


static void bgsave_child(const char *tmp_path) {// full save for child


    // cpu_set_t set;
    // CPU_ZERO(&set);
    // long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    // for (long c = 0; c < ncpu; c++) CPU_SET(c, &set);
    // sched_setaffinity(0, sizeof(set), &set);




    signal(SIGTERM, SIG_DFL);
    signal(SIGINT,  SIG_DFL);

    // close_range(3, ~0U, 0);

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) _exit(2);

    size_t cap = 1024 * 1024;
    char *buf = calloc(1, cap + MAC_LEN);
    if (!buf) _exit(3);

    uint32_t count = 0;
    size_t len = 0;

    if (strncmp(server_cfg.storage_structure, "rbtree", 6) == 0) {
        count = (uint32_t)kvs_rbtree_count(globle_rbtree);
        len = kvs_rbtree_save(globle_rbtree, &buf, &cap, count);
    } else if(strncmp(server_cfg.storage_structure, "hash", 4) == 0) {
        count = (uint32_t)kvs_hash_count(globle_hash);
        len = kvs_hash_save(globle_hash, &buf, &cap, count);
    } else if (strncmp(server_cfg.storage_structure, "skiplist", 8) == 0) {
        count = (uint32_t)kvs_skiplist_count(globle_skiplist);
        len = kvs_skiplist_save(globle_skiplist, &buf, &cap, count);
    }else if (strncmp(server_cfg.storage_structure, "array", 5) == 0) {
        count = (uint32_t)kvs_array_count(globle_array);
        len = kvs_array_save(globle_array, &buf, &cap, count);
    } else {
        _exit(4);
    }
    
    if (len == 0) _exit(4);

    void *p = realloc(buf, len + MAC_LEN);
    if (!p) _exit(3);
    buf = p;

    if (crypto_auth_hmacsha256((unsigned char *)(buf + len),
                               (const unsigned char *)buf, len, g_hmac_key) != 0)
        _exit(5);

    if (pwrite_all(fd, buf, len + MAC_LEN) < 0) _exit(6);
    if (fsync(fd) < 0)  _exit(7);
    if (close(fd) < 0)  _exit(7);

    if (rename(tmp_path, "config/full.txt") < 0){
        dprintf(2, "[bgsave] rename %s -> config/full.txt failed: %s\n",
                tmp_path, strerror(errno));
        _exit(8);
    } 

    int dfd = open("config", O_RDONLY | O_DIRECTORY);
    if (dfd < 0) _exit(9);
    if (fsync(dfd) < 0) _exit(9);
    close(dfd);

    _exit(0);
}


void check_bgsave_done(void) {// check child progression
    if (g_bgsave_pid <= 0) return;
    int st;
    pid_t r = waitpid(g_bgsave_pid, &st, WNOHANG);
    if (r == 0) return;
    if (r == g_bgsave_pid) {
        g_bgsave_last_ok = (WIFEXITED(st) && WEXITSTATUS(st) == 0);
        if (g_bgsave_last_ok){
            fprintf(stderr,"bgsave ok\n");
        }else if (WIFEXITED(st)){
            fprintf(stderr,"bgsave failed, code=%d\n", WEXITSTATUS(st));
        }else fprintf(stderr,"bgsave killed by signal %d\n", WTERMSIG(st));
            
        unlink("config/tmp_mes.tmp");
    }else {
        g_bgsave_last_ok = 0;
    }
    g_bgsave_pid = -1;
    g_bgsave_gen++;
}


int bgsave_start(int for_repl){

    check_bgsave_done();

    if (g_bgsave_pid > 0) return -2;

    if (for_repl) {
        g_ring.send_off = g_ring.write_off;
        g_repl_collect  = 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (for_repl) g_repl_collect = 0;
        return -1;
    }else if (pid == 0) {
        bgsave_child("config/tmp_mes.tmp");
    }
    g_bgsave_pid = pid;

    return 0;
}


int bgsave_running(void) { return g_bgsave_pid > 0; }
unsigned bgsave_gen(void) { return g_bgsave_gen; }
int bgsave_last_ok(void) { return g_bgsave_last_ok; }

void kvs_heap_compact(void){
#if USE_JEMALLOC
    mallctl("arena." STRINGIFY(MALLCTL_ARENAS_ALL) ".purge", NULL, NULL, NULL, 0);
#else
    void *p = malloc(1024);
    if (p) free(p);
    malloc_trim(0);
#endif
}

#if IO_URING_ADD
static int skip_add_log = 0;

#endif



static inline int put_bulk(char *dst, const char *s, size_t n){

    char tmp[20];

    int k = 0;
    size_t v = n;
    char *p = dst;
    do{
        tmp[k++] = (char)('0' + v % 10);
        v /= 10;
    }while(v);

    *p++ = '$';

    while(k) *p++ = tmp[--k]; 
    *p++ = '\r'; *p++ = '\n';

    memcpy(p, s, n);
    p += n;
    *p++ = '\r'; *p++ = '\n';

    return (int)(p - dst);
}


static int resp_reserve(char **buf, size_t *cap, size_t off, size_t need){
    if (*buf != NULL && *cap >= off && *cap - off >= need) return 0;
    size_t nc = *cap ? *cap : 4096;
    while (nc < off + need) nc *= 2;
    void *p = realloc(*buf, nc);
    if (p == NULL) return -1;
    *buf = p;
    *cap = nc;
    return 0;
}

#define RESP_SMALL  256


#define REPLY_GET(result)                       \
    do{                                         \
        if(!show) break;                        \
        if(result == NULL){                     \
            length = PUT_LIT(*response, "$-1\r\n"); \
            break;                              \
        }                                       \
        if(resp_reserve(response, resp_length, 0, *value_len + 32) < 0) return -1;\
        length = put_bulk(*response, (result), *value_len);\
    }while(0)                                                   


int kvs_protocol_filter(char **tokens, char **response, size_t *resp_length, size_t *key_len, size_t *value_len){
    if(tokens == NULL || response == NULL) return -1;
#if IO_URING_ADD
    skip_add_log = 0;
#endif
    g_repl_cmd = 0;

    int i = KVS_CMD_START;
    for(i = KVS_CMD_START; i < KVS_CMD_COUNT; i ++){
        if(strcmp(tokens[0],command[i]) == 0) break;
    }

    if(i >= KVS_CMD_SET && i <= KVS_CMD_EXIST){
        if(strncmp(server_cfg.storage_structure, "rbtree", 6) == 0)        i += 5;
        else if(strncmp(server_cfg.storage_structure, "hash", 4) == 0)     i += 10;
        else if(strncmp(server_cfg.storage_structure, "skiplist", 8) == 0) i += 15;
    }

    
    const int show = (server_cfg.show_response == 1);
    char *key = tokens[1];
    char *value = tokens[2];
    int length = 0;
    int ret = 0;

    switch(i){
        case KVS_CMD_SET:{
            if (*value_len > KVS_MAX_VALUE) {
                if(server_cfg.show_response == 1)
                    length = sprintf(*response, "-ERR value too large (max 512 MiB)\r\n");
                break;
            }
            ret = kvs_array_set(globle_array, key, value, *key_len, *value_len);
            if(server_cfg.show_response == 1)
                length = sprintf(*response, ret < 0 ? "-ERR internal error\r\n" : "+OK\r\n");
            break;
        }
        case KVS_CMD_GET:{
            char *result = kvs_array_get(globle_array, key, *key_len, value_len);
            if(result == NULL){
                if(server_cfg.show_response == 1) length = sprintf(*response,"$-1\r\n");
            }else if(server_cfg.show_response == 1){
                size_t need = *value_len + 32;
                if(need > *resp_length){
                    void *p = realloc(*response, need);
                    if(p == NULL) return -1;
                    *response = p; *resp_length = need;
                }
                length  = sprintf(*response, "$%zu\r\n", *value_len);
                memcpy(*response + length, result, *value_len);
                length += *value_len;
                (*response)[length++] = '\r';
                (*response)[length++] = '\n';
            }
            break;
        }
        case KVS_CMD_DEL:{
            ret = kvs_array_del(globle_array, key, *key_len);
            if(server_cfg.show_response == 1){
                if(ret == -2)      length = sprintf(*response,"-ERR internal error\r\n");
                else if(ret == 0)  length = sprintf(*response,":1\r\n");
                else               length = sprintf(*response,":0\r\n");
            }
            break;
        }
        case KVS_CMD_MOD:{
            ret = kvs_array_mod(globle_array, key, value, *key_len, *value_len);
            if(server_cfg.show_response == 1){
                if(ret < 0)        length = sprintf(*response,"-ERR internal error\r\n");
                else if(ret == 0)  length = sprintf(*response,"+OK\r\n");
                else               length = sprintf(*response,"-ERR no such key\r\n");
            }
            break;
        }
        case KVS_CMD_EXIST:{
            ret = kvs_array_exist(globle_array, key, *key_len);
            if(server_cfg.show_response == 1)
                length = sprintf(*response, ret == 0 ? ":1\r\n" : ":0\r\n");
            break;
        }
//rbtree
        case KVS_CMD_RSET:{
            if (*value_len > KVS_MAX_VALUE) {
                if(server_cfg.show_response == 1)
                    length = sprintf(*response, "-ERR value too large (max 512 MiB)\r\n");
                break;
            }
            ret = kvs_rbtree_set(globle_rbtree, key, value, *key_len, *value_len);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR internal error\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,"+OK\r\n");
            }
            break;
        }
        case KVS_CMD_RGET:{
            char *result = kvs_rbtree_get(globle_rbtree, key, *key_len, value_len);
            if(result == NULL){
                if(server_cfg.show_response == 1) length = sprintf(*response,"$-1\r\n");
            }else if(server_cfg.show_response == 1){
                size_t need = *value_len + 32;   // "$<len>\r\n" + value + "\r\n"
                if(need > *resp_length){
                    void *p = realloc(*response, need);
                    if(p == NULL) return -1;
                    *response = p; *resp_length = need;
                }
                length  = sprintf(*response, "$%zu\r\n", *value_len);
                memcpy(*response + length, result, *value_len);
                length += *value_len;
                (*response)[length++] = '\r';
                (*response)[length++] = '\n';
            }
            break;
        }
        case KVS_CMD_RDEL:{
            ret = kvs_rbtree_del(globle_rbtree, key, *key_len);
            if(server_cfg.show_response == 1){
                if(ret == -2)      length = sprintf(*response,"-ERR internal error\r\n");
                else if(ret == 0)  length = sprintf(*response,":1\r\n");
                else               length = sprintf(*response,":0\r\n");
            }
            break;
        }
        case KVS_CMD_RMOD:{
            ret = kvs_rbtree_mod(globle_rbtree, key, value, *key_len, *value_len);
            if(server_cfg.show_response == 1){
                if(ret < 0)        length = sprintf(*response,"-ERR internal error\r\n");
                else if(ret == 0)  length = sprintf(*response,"+OK\r\n");
                else               length = sprintf(*response,"-ERR no such key\r\n");
            }
            break;
        }
        case KVS_CMD_REXIST:{
            ret = kvs_rbtree_exist(globle_rbtree, key, *key_len);
            if(server_cfg.show_response == 1)
                length = sprintf(*response, ret == 0 ? ":1\r\n" : ":0\r\n");
            break;
        }
// hash
        case KVS_CMD_HSET:{
            if (*value_len > KVS_MAX_VALUE) {
                if(server_cfg.show_response == 1)
                    length = PUT_LIT(*response, "-ERR value too large (max 512 MiB)\r\n");
                break;
            }

            ret = kvs_hash_set(globle_hash,key,value, *key_len, *value_len);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = PUT_LIT(*response,"-ERR internal error\r\n");
            }else{
                if(server_cfg.show_response == 1) length = PUT_LIT(*response,"+OK\r\n");
                g_repl_cmd = 1;
            }

            break;
        }
        case KVS_CMD_HGET:{
            char *result = kvs_hash_get(globle_hash, key, *key_len, value_len);
            // if(result == NULL){
            //     if(server_cfg.show_response == 1) length = sprintf(*response,"$-1\r\n");
            // }else{
            //     if(server_cfg.show_response == 1){
            //         size_t need = *value_len + 32;
            //         if(need > *resp_length){
            //             void *p = realloc(*response, need);
            //             if(p == NULL) return -1;
            //             *response = p; *resp_length = need;
            //         }
            //         length  = sprintf(*response, "$%zu\r\n", *value_len);
            //         memcpy(*response + length, result, *value_len);
            //         length += *value_len;
            //         (*response)[length++] = '\r';
            //         (*response)[length++] = '\n';
            //     }
            // }
            REPLY_GET(result);
            break;
        }
        case KVS_CMD_HDEL:{
            ret = kvs_hash_del(globle_hash, key, *key_len);
            if (ret == -2) {
                length = sprintf(*response, "-ERR internal error\r\n");
            } else if (ret == 0) {
                length = sprintf(*response, ":1\r\n");
                g_repl_cmd = 1;
            } else {
                length = sprintf(*response, ":0\r\n");
                
            }
            break;
        }
        case KVS_CMD_HMOD:{
            ret = kvs_hash_mod(globle_hash,key,value, *key_len, *value_len);
            if(ret < 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR internal error\r\n");
            }else if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,"+OK\r\n");
                g_repl_cmd = 1;
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR no such key\r\n");
            }
            break;
        }
        case KVS_CMD_HEXIST:{
            ret = kvs_hash_exist(globle_hash,key, *key_len);
            if(ret == 0){
                if(server_cfg.show_response == 1) length = sprintf(*response,":1\r\n");
            }else{
                if(server_cfg.show_response == 1) length = sprintf(*response,":0\r\n");
            }
            break;
        }
// skiplist
        case KVS_CMD_SSET:{
            if (*value_len > KVS_MAX_VALUE) {
                if(server_cfg.show_response == 1)
                    length = sprintf(*response, "-ERR value too large (max 512 MiB)\r\n");
                break;
            }
            ret = kvs_skiplist_set(globle_skiplist, key, value, *key_len, *value_len);
            if(server_cfg.show_response == 1)
                length = sprintf(*response, ret < 0 ? "-ERR internal error\r\n" : "+OK\r\n");
            break;
        }
        case KVS_CMD_SGET:{
            char *result = kvs_skiplist_get(globle_skiplist, key, *key_len, value_len);
            if(result == NULL){
                if(server_cfg.show_response == 1) length = sprintf(*response,"$-1\r\n");
            }else if(server_cfg.show_response == 1){
                size_t need = *value_len + 32;
                if(need > *resp_length){
                    void *p = realloc(*response, need);
                    if(p == NULL) return -1;
                    *response = p; *resp_length = need;
                }
                length  = sprintf(*response, "$%zu\r\n", *value_len);
                memcpy(*response + length, result, *value_len);
                length += *value_len;
                (*response)[length++] = '\r';
                (*response)[length++] = '\n';
            }
            break;
        }
        case KVS_CMD_SDEL:{
            ret = kvs_skiplist_del(globle_skiplist, key, *key_len);
            if(server_cfg.show_response == 1){
                if(ret == -2)      length = sprintf(*response,"-ERR internal error\r\n");
                else if(ret == 0)  length = sprintf(*response,":1\r\n");
                else               length = sprintf(*response,":0\r\n");
            }
            break;
        }
        case KVS_CMD_SMOD:{
            ret = kvs_skiplist_mod(globle_skiplist, key, value, *key_len, *value_len);
            if(server_cfg.show_response == 1){
                if(ret < 0)        length = sprintf(*response,"-ERR internal error\r\n");
                else if(ret == 0)  length = sprintf(*response,"+OK\r\n");
                else               length = sprintf(*response,"-ERR no such key\r\n");
            }
            break;
        }
        case KVS_CMD_SEXIST:{
            ret = kvs_skiplist_exist(globle_skiplist, key, *key_len);
            if(server_cfg.show_response == 1)
                length = sprintf(*response, ret == 0 ? ":1\r\n" : ":0\r\n");
            break;
        }
        case KVS_CMD_PING:{
            if(server_cfg.show_response == 1) length = sprintf(*response,"+PONG\r\n");
            break;
        }
        case KVS_CMD_QUIT:{
            if(server_cfg.show_response == 1) length = sprintf(*response,"+OK\r\n");
            break;
        }
        case KVS_CMD_SYNC:{
            extern int kvs_sync(void);
            int r = kvs_sync(); 
            if (server_cfg.show_response == 1){
                length = sprintf(*response, (r == 0) ? "+OK\r\n" : "-ERR sync failed\r\n");
            }
                
            break;
        }
        case KVS_CMD_INFO:{
            
            if (strncmp(server_cfg.storage_structure, "array", 5) == 0) {
                length = sprintf(*response, "+storage:%s count:%d cap:%zu",
                                server_cfg.storage_structure,
                                kvs_array_count(globle_array), globle_array->cap);
            } else if (strncmp(server_cfg.storage_structure, "rbtree", 6) == 0) {
                length = sprintf(*response, "+storage:%s count:%d",
                                server_cfg.storage_structure, kvs_rbtree_count(globle_rbtree));
            } else if (strncmp(server_cfg.storage_structure, "skiplist", 8) == 0) {
                length = sprintf(*response, "+storage:%s count:%d level:%d",
                                server_cfg.storage_structure,
                                kvs_skiplist_count(globle_skiplist), globle_skiplist->level);
            } else if (strncmp(server_cfg.storage_structure, "hash", 4) == 0) {
                int n   = (globle_hash != NULL) ? kvs_hash_count(globle_hash) : -1;
                int cap = (globle_hash != NULL) ? globle_hash->max_slots : 0;
                unsigned long idx_bytes = (unsigned long)cap * sizeof(hashnode_t *);
                length = sprintf(*response, "+storage:%s count:%d max_slots:%d avg_chain:%.2f index_bytes:%lu",
                                server_cfg.storage_structure, n, cap,
                                (cap > 0) ? (double)n / cap : 0.0, idx_bytes);
            } else {
                length = sprintf(*response, "+storage:unknown");
            }
            length += kvs_node_pool_info(*response + length, *resp_length - length - 3);
            length += sprintf(*response + length, "\r\n");
            break;
        }
        case KVS_CMD_SAVE:{
#if IO_URING_ADD   
            skip_add_log = 1;

#endif

            if (server_cfg.full_save != 1) {
                length = sprintf(*response, "-ERR snapshot disabled\r\n");
                break;
            }


            int r = bgsave_start(0);
            if (r == -2) {
                length = sprintf(*response, "-ERR background save already in progress\r\n");
            }else if (r < 0) {
                length = sprintf(*response, "-ERR fork failed\r\n");
            }else length = sprintf(*response, "+Background saving started\r\n");

            break;
        }
        case KVS_CMD_ECHO:{
            const char *arg = tokens[1] ? tokens[1] : "";
            
            if (show) {
                if (resp_reserve(response, resp_length, 0, *key_len + 32) < 0) return -1;
                length = put_bulk(*response, arg, *key_len);
            }
            break;
        }
        case KVS_CMD_CLEAR:{
#if IO_URING_ADD
            skip_add_log = 1;
#endif
            const char *paths[2] = { "config/add.txt", "config/full.txt" };
            int ok = 1;

            for(int k = 0; k < 2; k++){
                int cfd = open(paths[k], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if(cfd < 0){
                    fprintf(stderr, "[clear] %s: %s\n", paths[k], strerror(errno));
                    ok = 0;
                }else{
                    fsync(cfd);
                    close(cfd); 
                }
            }

            length = ok ? sprintf(*response, "+OK\r\n") : sprintf(*response, "-ERR clear failed\r\n");
            break;
        }

        default:{
            if(server_cfg.show_response == 1) length = sprintf(*response,"-ERR unknown command '%s'\r\n", tokens[0]);
            break;
        }
    }

    return length;
};


typedef struct {
    char  *mes;
    size_t mes_len;
    size_t mes_size; 
    char  *resp;  
    size_t resp_cap;       
} kvs_conn_ctx_t;
kvs_conn_ctx_t kvs_ctx[1024] = {0};

void kvs_conn_ctx_free(int fd){
    if (fd < 0 || fd >= 1024) return;

    free(kvs_ctx[fd].mes); kvs_ctx[fd].mes = NULL;

    free(kvs_ctx[fd].resp);

    kvs_ctx[fd].resp = NULL;
 
    kvs_ctx[fd].mes_len = 0; 
    kvs_ctx[fd].mes_size = 0;
    kvs_ctx[fd].resp_cap = 0;
}

#if IO_URING_ADD
static void reap_add_completions(void)
{
    struct io_uring_cqe *cqe;

    while (io_uring_peek_cqe(&add_ring, &cqe) == 0) {
        uring_info *done = io_uring_cqe_get_data(cqe);

        if (done != NULL) {
            if (done->len > 0 && cqe->res != done->len) {
                fprintf(stderr,
                        "[aof] write result=%d expected=%d\n",
                        cqe->res, done->len);
            }

            u_info_free(done);
        }

        io_uring_cqe_seen(&add_ring, cqe);
    }
}
#endif


void ringbuf_repl_notify(void){
    if(g_sender_waiting){
        g_sender_waiting = 0;
        uint64_t one = 1;
        if (write(g_repl_efd, &one, sizeof(one)) < 0) {}
    }
}


static int resp_append(char **response, size_t *response_size, size_t *total, const char *src, size_t n) {
    if (response == NULL || *response == NULL) return 0;
 
    size_t need = *total + n;
    if (need > *response_size) {
        size_t newcap = (*response_size > 0) ? *response_size : 4096;
        while (newcap < need) newcap += newcap / 2 + 64;
        void *p = realloc(*response, newcap);
        if (p == NULL) return -1;
        *response = p;
        *response_size = newcap;
    }
    memcpy(*response + *total, src, n);
    *total = need;
    return 0;
}



int kvs_protocol(int fd, char *msg, int length, char **response, size_t *response_size){ 

    if (fd < 0 || fd >= 1024 || msg == NULL || length < 0) return -1;

    check_bgsave_done();

#if REPL_MODE == 2
    extern void master_forward_to_slave(const char *data, unsigned len);
    if (load_saved_msg == 1) master_forward_to_slave(msg, length);
#endif


    if (kvs_ctx[fd].mes == NULL) {

        kvs_ctx[fd].mes_size = 1024 * 64;
        kvs_ctx[fd].mes = malloc(kvs_ctx[fd].mes_size);
        if (kvs_ctx[fd].mes == NULL) { kvs_ctx[fd].mes_size = 0; return -1; }
        kvs_ctx[fd].mes_len = 0;
    }

    size_t need = (size_t)length + kvs_ctx[fd].mes_len;

    if (load_saved_msg == 1 && need > KVS_MAX_FRAME) {
        fprintf(stderr, "[proto] fd=%d frame %zu > %u, drop conn\n", fd, need, (unsigned)KVS_MAX_FRAME);
        kvs_ctx[fd].mes_len = 0;
        return KVS_RET_DROP;
    }

    if(need > kvs_ctx[fd].mes_size){
        size_t new_size = kvs_ctx[fd].mes_size;
        while(need > new_size) {
            if (new_size > SIZE_MAX / 2) {
                new_size = need;
                break;
            }

            new_size += new_size / 2;
        }
        void *ptr = realloc(kvs_ctx[fd].mes, new_size);
        if(ptr == NULL) {
            const char *err = "-ERR frame too large\r\n";
            int elen = (int)strlen(err);
            if(response != NULL && *response != NULL) memcpy(*response, err, elen);
            kvs_ctx[fd].mes_len = 0;
            return *response ? elen : -1;
        }
        kvs_ctx[fd].mes = ptr; 
        kvs_ctx[fd].mes_size = new_size;
    }
    memcpy(kvs_ctx[fd].mes + kvs_ctx[fd].mes_len, msg, length);
    kvs_ctx[fd].mes_len += (size_t)length;


#if 0

    size_t total_response_len = 0;

    while (kvs_ctx[fd].mes_len > 0) {
        size_t off = 0;
        key_len = 0; value_len = 0;
        int ret = kvs_is_complete(kvs_ctx[fd].mes, kvs_ctx[fd].mes_len, &off);
        if (ret == 0) break; 
        if (ret < 0){
            if (ret == -2) break;
            else {
                kvs_ctx[fd].mes_len = 0;
                const char *err = "-ERR protocol error\r\n";
                int elen = (int)strlen(err);
                if (response != NULL && *response) memcpy(*response, err, elen);
                return elen;
            }
        }
        if ((size_t)ret > kvs_ctx[fd].mes_len){
            const char *err = "-ERR over max range\r\n";
            int elen = (int)strlen(err);
            if(response != NULL && *response != NULL) memcpy(*response, err, elen);
            kvs_ctx[fd].mes_len = 0;
            return *response ? elen : -1;
        }

        packet_len = (size_t)ret;

        int count = kvs_split_token(kvs_ctx[fd].mes, tokens, packet_len, &key_len, &value_len);

        if(count < 0){
            memmove(kvs_ctx[fd].mes, kvs_ctx[fd].mes + packet_len, kvs_ctx[fd].mes_len - packet_len);
            kvs_ctx[fd].mes_len -= packet_len;

            if(server_cfg.use_mempool == 1){
                mp_large_free(cmd_pool, tokens[0]);
            }else{
                free(tokens[0]);
            }
            tokens[0] = tokens[1] = tokens[2] = NULL;

            const char *err = "-ERR split token error\r\n";
            int elen = (int)strlen(err);
            if(response != NULL && *response != NULL){
                size_t need_r = total_response_len + (size_t)elen;
                if(need_r > *response_size){
                    size_t newcap = (*response_size > 0) ? *response_size : 4096;
                    while (newcap < need_r) newcap += newcap / 2 + 64;

                    void *ptr = realloc(*response, newcap);
                    if(ptr == NULL) return -2;
                    *response = ptr;
                    *response_size = newcap;
                }
                memcpy(*response + total_response_len, err, (size_t)elen);
                total_response_len = need_r;
            }
            break;
        } 
        
        int direct = (total_response_len == 0 && response != NULL && *response != NULL && *response_size >= 1024);

        char  *resp_buf;
        size_t resp_cap;

        if (direct) {
            resp_buf = *response;
            resp_cap = *response_size;
            resp_buf[0] = '\0';
        } else {
            
            if (kvs_ctx[fd].resp == NULL) {
                kvs_ctx[fd].resp_cap = 1024;
                // if(server_cfg.use_mempool == 1){
                //     kvs_ctx[fd].resp = mp_alloc(pool, 1024 * 1024 * 5);
                // }else {
                    kvs_ctx[fd].resp = malloc(kvs_ctx[fd].resp_cap);
                    if (kvs_ctx[fd].resp == NULL) {

                        free(tokens[0]);
                        
                        tokens[0] = tokens[1] = tokens[2] = NULL;
                        return -2;
                    }
                // }
            }
            kvs_ctx[fd].resp[0] = '\0';
            resp_buf = kvs_ctx[fd].resp;
            resp_cap = kvs_ctx[fd].resp_cap;
        }
        int resp_used = 0;
        resp_used = kvs_protocol_filter(tokens, &resp_buf, &resp_cap, &key_len, &value_len);

#else

    size_t total_response_len = 0;

    size_t rpos = 0;      
    int ret_code = 0;
    int pr;
    kvs_req_t req;

    while (rpos < kvs_ctx[fd].mes_len &&
           (kvs_ctx[fd].mes[rpos] == '\r' || kvs_ctx[fd].mes[rpos] == '\n')) rpos++;

    while ((pr = kvs_parse(kvs_ctx[fd].mes + rpos, kvs_ctx[fd].mes_len - rpos, &req)) == KVS_PARSE_OK) {

        char *frame = kvs_ctx[fd].mes + rpos;
        size_t packet_len = req.frame_len;

        char *tokens[3] = { req.cmd, req.argv[1], req.argv[2] };
        size_t key_len = req.argl[1];
        size_t value_len = req.argl[2];

        int direct = (total_response_len == 0 && response != NULL && *response != NULL && *response_size >= 1024);

        char *resp_buf;
        size_t resp_cap;

        if(direct){
            resp_buf = *response;
            resp_cap = *response_size;
            resp_buf[0] = '\0';
        }else {

            if (kvs_ctx[fd].resp == NULL) {
                kvs_ctx[fd].resp_cap = 1024;
                kvs_ctx[fd].resp = malloc(kvs_ctx[fd].resp_cap);
                if (kvs_ctx[fd].resp == NULL) {
                    kvs_ctx[fd].resp_cap = 0;
                    ret_code = -2;
                    break;
                }
            }
            kvs_ctx[fd].resp[0] = '\0';
            resp_buf = kvs_ctx[fd].resp;
            resp_cap = kvs_ctx[fd].resp_cap;
            
        }


        int resp_used = kvs_protocol_filter(tokens, &resp_buf, &resp_cap, &key_len, &value_len);

        if (direct) {
            *response = resp_buf;
            *response_size = resp_cap;
        } else {
            kvs_ctx[fd].resp = resp_buf;
            kvs_ctx[fd].resp_cap = resp_cap;
        }

        int repl_full = 0;
        if(g_repl_cmd == 1 && load_saved_msg == 1 && g_repl_collect == 1){
            int ring_ret = ringbuf_append(&g_ring, frame, packet_len);
            if(ring_ret == 0) {
                ringbuf_repl_notify();
            }else if (ring_ret == -2) {
                fprintf(stderr, "[repl] ringbuf full, replica needs full resync\n");
                repl_full = 1;
            }
        }

    
#if IO_URING_ADD
#define AOF_BIG_THRESHOLD   (1024 * 1024)

        if(server_cfg.add_save == 1) {
            reap_add_completions();
            
            if(strncmp(tokens[0], "SYNC", 4) == 0){
                skip_add_log = 1;
            }else if(strncmp(tokens[0], "SAVE", 4) == 0){
                skip_add_log = 1;
            }else if(strncmp(tokens[0], "INFO", 4) == 0){
                skip_add_log = 1;
            }

            if(load_saved_msg == 1 && skip_add_log == 0){

                int add_fd = get_add_fd();
     

                if ((size_t)packet_len + 4 > AOF_BIG_THRESHOLD){
                    uint32_t crc_ret = (uint32_t)crc32(0L, (const Bytef *)frame,
                                                       (uInt)packet_len);

                    struct iovec iov[2];
                    iov[0].iov_base = &crc_ret;
                    iov[0].iov_len = 4;
                    iov[1].iov_base = frame;
                    iov[1].iov_len = packet_len;

                    struct io_uring_sqe *sqe = io_uring_get_sqe(&add_ring);
                    if (!sqe) {
                        io_uring_submit(&add_ring);
                        sqe = io_uring_get_sqe(&add_ring);                       
                    }

                    if(!sqe){
                        fprintf(stderr, "[aof] big: no sqe, drop\n");
                        abort();
                    }

                    io_uring_prep_writev(sqe, add_fd, iov, 2, 0);
                    io_uring_sqe_set_data(sqe, NULL);

                    if (io_uring_submit(&add_ring) < 0) {
                        fprintf(stderr, "[aof] big: submit fail\n");
                        abort();
                    } else {
                        int retry = 0;
                        for (;;) {
                            struct io_uring_cqe *cqe = NULL;
                            if (io_uring_wait_cqe(&add_ring, &cqe) < 0){
                                if (++retry > 100) {
                                    fprintf(stderr, "[aof] big: wait_cqe error, close AOF\n");
                                    abort(); 
                                }
                                continue;
                            }
                            retry = 0;

                            void *d = io_uring_cqe_get_data(cqe);
                            if (d == NULL) {
                                if (cqe->res != (int)(packet_len + 4))
                                    fprintf(stderr, "[aof] big write res=%d expect=%zu\n",
                                                cqe->res, packet_len + 4);
                                io_uring_cqe_seen(&add_ring, cqe);
                                break;
                            }
                            u_info_free((uring_info *)d);
                            io_uring_cqe_seen(&add_ring, cqe);
                        }   
                    }
                }else{
                    uring_info *slot = u_info_alloc();  

                    if (!slot) {
                        // fprintf(stderr, "[aof] pool empty, drop this record\n");   
                    } else {
                        if(server_cfg.use_mempool == 1){
                            slot->buffer = mp_alloc(cmd_pool, (size_t)packet_len + 4);
                        }else {
                            slot->buffer = malloc((size_t)packet_len + 4);
                        }
                        if (!slot->buffer) {
                            u_info_free(slot);
                        } else {
                            uint32_t crc_ret = (uint32_t)crc32(
                                    0L, (const Bytef *)frame, (uInt)packet_len);
                            slot->fd  = add_fd;
                            slot->len = (int)packet_len + 4;
                            memcpy(slot->buffer, &crc_ret, 4);
                            memcpy(slot->buffer + 4, frame, packet_len);
                            
                            struct io_uring_sqe *sqe = io_uring_get_sqe(&add_ring);    
                            if (!sqe) {
                                if(server_cfg.use_mempool == 1){
                                    mp_large_free(cmd_pool, slot->buffer);
                                }else{
                                    free(slot->buffer); 
                                }
                                slot->buffer = NULL;
                                u_info_free(slot);
                                fprintf(stderr, "[aof] no sqe, drop this record\n");
                            }else{
                                io_uring_prep_write(sqe, add_fd, slot->buffer, packet_len + 4, 0);
                                io_uring_sqe_set_data(sqe, slot);
                                if (io_uring_submit(&add_ring) < 0) {              
                                    fprintf(stderr, "[aof] submit failed, drop\n");
                                    abort();
                                }
                            
                            }
                        }
                    }
                }
                
            }
        }
#else
        if (server_cfg.add_save == 1 && load_saved_msg == 1 && g_repl_cmd == 1) {
            aof_add(frame, packet_len);
        }
#endif
        rpos += packet_len;

        while (rpos < kvs_ctx[fd].mes_len &&
               (kvs_ctx[fd].mes[rpos] == '\r' || kvs_ctx[fd].mes[rpos] == '\n')) rpos++;

        if (resp_used > 0) {
            if (direct) {
                total_response_len += (size_t)resp_used;      /* �ѽ�ֱ�ӌ��� *response �e�� */
            } else if (resp_append(response, response_size, &total_response_len,
                                    kvs_ctx[fd].resp, (size_t)resp_used) < 0) {
                ret_code = -2;
                break;
            }
        }
    
        if (repl_full) { 
            ret_code = -2; 
            break; 
        }

    }
    
    aof_flush();

    /* ��ʽ�e�`��������Y���ѽ��o�����R��ȫ���G�����e�`ӍϢ������ǰ������Ļؑ�֮�� */
    if (pr == KVS_PARSE_ERR && ret_code == 0) {
        static const char err[] = "-ERR protocol error\r\n";
        rpos = kvs_ctx[fd].mes_len;
        resp_append(response, response_size, &total_response_len, err, sizeof err - 1);
    }
 
    /* ����ֻ��һ�Σ���߀�]̎���İ���Ƶ��_�^ */
    if (rpos > 0) {
        memmove(kvs_ctx[fd].mes, kvs_ctx[fd].mes + rpos, kvs_ctx[fd].mes_len - rpos);
        kvs_ctx[fd].mes_len -= rpos;
    }


#endif
    if (kvs_ctx[fd].mes_len == 0 && kvs_ctx[fd].mes_size > 64 * 1024) {
        void *p = realloc(kvs_ctx[fd].mes, 64 * 1024);
        if (p) { kvs_ctx[fd].mes = p; kvs_ctx[fd].mes_size = 64 * 1024; }
    }

    if (kvs_ctx[fd].resp != NULL && kvs_ctx[fd].resp_cap > 64 * 1024) {
        void *p = realloc(kvs_ctx[fd].resp, 1024);
        if (p) { kvs_ctx[fd].resp = p; kvs_ctx[fd].resp_cap = 1024; }
    }

    if (ret_code != 0) return ret_code;
    return (int)total_response_len;
}


int init_kvs(void){
    if(strcmp(server_cfg.storage_structure, "array") == 0){
        if(server_cfg.use_mempool == 1){
            globle_array = mp_alloc(pool, sizeof(kvs_array_t));
        }else{
            globle_array = malloc(sizeof(kvs_array_t));
        }
        kvs_array_create(globle_array);
        
    }else if(strcmp(server_cfg.storage_structure, "hash") == 0){
        if(server_cfg.use_mempool == 1){
            globle_hash = mp_alloc(pool, sizeof(kvs_hash_t));
        }else{
            globle_hash = malloc(sizeof(kvs_hash_t));
        }
        kvs_hash_create(globle_hash);
    }else if(strcmp(server_cfg.storage_structure, "skiplist") == 0){
        if(server_cfg.use_mempool == 1){
            globle_skiplist = mp_alloc(pool, sizeof(kvs_skiplist_t));
        }else{
            globle_skiplist = malloc(sizeof(kvs_skiplist_t));
        }
        kvs_skiplist_create(globle_skiplist);
    }else if(strcmp(server_cfg.storage_structure, "rbtree") == 0){
        if(server_cfg.use_mempool == 1){
            globle_rbtree = mp_alloc(pool, sizeof(kvs_rbtree_t));
        }else{
            globle_rbtree = malloc(sizeof(kvs_rbtree_t));
        }
        kvs_rbtree_create(globle_rbtree);
    }
    return 0;
}


void dest_kvs(void){
    if(strcmp(server_cfg.storage_structure, "array") == 0){
        kvs_array_destory(globle_array);
    }else if(strcmp(server_cfg.storage_structure, "hash") == 0){
        kvs_hash_destory(globle_hash);
    }else if(strcmp(server_cfg.storage_structure, "skiplist") == 0){
        kvs_skiplist_destroy(globle_skiplist);
    }else if(strcmp(server_cfg.storage_structure, "rbtree") == 0){
        kvs_rbtree_destory(globle_rbtree);
    }

#if IO_URING_ADD
    close(globle_add_fd);
#else
    aof_flush();
    if (globle_add_fd >= 0) {
        fdatasync(globle_add_fd);
        close(globle_add_fd);
    }
#endif
}


#define FULL_HDR_SIZE  16
#define FULL_OFF_CRC     0
#define FULL_OFF_VER     8
#define FULL_OFF_COUNT   12
#define FULL_OFF_PACKLEN 4

void kvs_add_read(void){
    
    int fd = open("config/add.txt", O_RDONLY);
    if(fd == -1){
        perror("open config/add.txt");
        return;
    }

    struct stat st;
    if(fstat(fd, &st) < 0){
        perror("fstat error");
        close(fd);
        return ;
    }
    off_t size = st.st_size;
    if(size < 0){
        perror("fstat size error");
        close(fd);
        return ;
    }
    size_t len = st.st_size;
    if(len == 0){
        close(fd);
        return;   
    }
    char *addr = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    if(addr == MAP_FAILED){
        close(fd);
        perror("mmap error");
        return ;
    }

    load_saved_msg = 0;

    char *p = addr;
    char *end = addr + len;

    char *replay_response = NULL;
    size_t replay_response_size = 0;

    while (p + 4 <= end) {
        uint32_t crc_exp;
        memcpy(&crc_exp, p, 4);

        kvs_req_t req;
        if (kvs_parse(p + 4, (size_t)(end - p - 4), &req) != KVS_PARSE_OK) {
            fprintf(stderr, "[load] tail incomplete at offset %zu (%zd bytes left)\n",
                    (size_t)(p - addr), (ssize_t)(end - p));
            break;
        }
        size_t plen = req.frame_len;

        uint32_t crc_real = (uint32_t)crc32(0L, (const Bytef *)(p + 4), (uInt)plen);
        if(crc_exp != crc_real){
            fprintf(stderr, "[load] record crc mismatch at offset %zu (file=%08x calc=%08x)\n",
                    (size_t)(p - addr), crc_exp, crc_real);
            break;
        }

        kvs_protocol(fd, p + 4, (int)plen, &replay_response, &replay_response_size);
        p += 4 + plen;
    }

    kvs_conn_ctx_free(fd);
    free(replay_response);
    load_saved_msg = 1;
    munmap(addr, len);
    close(fd);
    // if(server_cfg.use_mempool == 1) mp_large_free(pool, tmp);
    // else free(tmp);
}



void kvs_full_read(void) {

    uint32_t loaded = 0, count_hdr = 0;

    int fd = open("config/full.txt", O_RDONLY);
    if(fd == -1){
        perror("open config/full.txt");
        return;
    }

    struct stat st;
    if(fstat(fd, &st) < 0){
        perror("fstat error");
        close(fd);
        return ;
    }
    off_t size = st.st_size;
    if(size < 0){
        perror("fstat size error");
        close(fd);
        return ;
    }
    size_t len = st.st_size;
    if(len == 0){
        close(fd);
        return;   
    }

    if (len < FULL_HDR_SIZE + MAC_LEN) {
        fprintf(stderr, "[load] too small: %zu\n", len);
        close(fd); return;
    }

    char *addr = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    if(addr == MAP_FAILED){
        perror("mmap error");
        close(fd);
        return ;
    }

    size_t mac_off = len - MAC_LEN;
    
    unsigned char mac_calc[MAC_LEN];
    if (crypto_auth_hmacsha256(mac_calc,(const unsigned char *)addr, mac_off,
                               g_hmac_key) != 0) {
        fprintf(stderr, "[load] hmac compute failed\n");
        goto out;
    }

    if (sodium_memcmp(mac_calc, addr + mac_off, MAC_LEN) != 0) {
        fprintf(stderr, "[load] MAC mismatch: file forged or key wrong\n");
        munmap(addr, len);
        close(fd);
        return ;                    
    }
    
    uint32_t crc_exp, pack_len;
    memcpy(&crc_exp,   addr + FULL_OFF_CRC, 4);
    memcpy(&count_hdr, addr + FULL_OFF_COUNT, 4);
    memcpy(&pack_len,  addr + FULL_OFF_PACKLEN, 4);

    if (memcmp(addr + FULL_OFF_VER, "V1.0", 4) != 0) {
        fprintf(stderr, "[load] bad version, refuse\n");
        goto out;
    }

    if ((size_t)FULL_HDR_SIZE + (size_t)pack_len + MAC_LEN != len) {
        fprintf(stderr, "[load] length mismatch: file=%zu expect=%zu (pack_len=%u)\n",
                len, (size_t)FULL_HDR_SIZE + pack_len + MAC_LEN, pack_len);
        goto out;
    }

    uint32_t crc_real = (uint32_t)crc32(0L,(const Bytef *)(addr + FULL_OFF_PACKLEN),
                        (uInt)(FULL_HDR_SIZE + pack_len - FULL_OFF_PACKLEN));

    if (crc_exp != crc_real) {
        fprintf(stderr, "[load] header/data crc mismatch: file=%08x calc=%08x\n",
                crc_exp, crc_real);
        goto out;
    }

    char *p = addr + FULL_HDR_SIZE;
    char *end = addr + FULL_HDR_SIZE + pack_len;

    // p += 16;
    while(p + 12 <= end){
        uint32_t crc_single_exp;
        memcpy(&crc_single_exp, p, 4);
        p += 4;

        uint32_t klen; uint32_t vlen;
        memcpy(&klen, p, 4);
        if (klen == 0 || (size_t)(end - p) < (size_t)4 + klen + 4 + 4) {
            fprintf(stderr, "[load] bad klen=%u at %zu, stop\n", klen, (size_t)(p - addr));
            goto out;
        }

        memcpy(&vlen, p + 4 + klen, 4);

        size_t rec_len = (size_t)4 + klen + 4 + (size_t)vlen;
        if ((size_t)(end - p) < rec_len) {
            fprintf(stderr, "[load] truncated record klen=%u vlen=%u at %zu, stop\n",
                    klen, vlen, (size_t)(p - addr));
            goto out;
        }


        uint32_t crc_single_real = crc32(0L, (const Bytef *)p, (uInt)(p + 4 + klen + 4 + vlen - p));
        
        if(crc_single_exp != crc_single_real){ 
            fprintf(stderr, "[load] single record crc mismatch at %zu\n", (size_t)(p - addr)); 
            goto out;
        }
        
        if (strncmp(server_cfg.storage_structure, "hash", 4) == 0) {
            if (kvs_hash_set(globle_hash, (char *)(p + 4), (char *)(p + 4 + klen + 4), klen, vlen) < 0) {
                fprintf(stderr, "[load] insert failed at %zu\n", (size_t)(p - addr));
                goto out;
            }
        } else if (strncmp(server_cfg.storage_structure, "rbtree", 6) == 0) {
            if (kvs_rbtree_set(globle_rbtree, (char *)(p + 4), (char *)(p + 4 + klen + 4), klen, vlen) < 0) {
                fprintf(stderr, "[load] insert failed at %zu\n", (size_t)(p - addr));
                goto out;
            }
        } else if (strncmp(server_cfg.storage_structure, "skiplist", 8) == 0) {
            if (kvs_skiplist_set(globle_skiplist, (char *)(p + 4), (char *)(p + 4 + klen + 4), klen, vlen) < 0) {
                fprintf(stderr, "[load] insert failed at %zu\n", (size_t)(p - addr));
                goto out;
            }
        } else if (strncmp(server_cfg.storage_structure, "array", 5) == 0) {
            if (kvs_array_set(globle_array, (char *)(p + 4), (char *)(p + 4 + klen + 4), klen, vlen) < 0) {
                fprintf(stderr, "[load] insert failed at %zu\n", (size_t)(p - addr));
                goto out;
            }
        }else {
            fprintf(stderr, "[load] unknown storage '%s'\n", server_cfg.storage_structure);
            goto out;
        }

        loaded++;

        p += 4 + klen + 4 + vlen;
    }
    // fprintf(stderr, "[load] full snapshot: loaded=%u (header count=%u)\n", loaded, count_hdr);

out:
    munmap(addr, len);
    close(fd);

    if (loaded != count_hdr) {
        fprintf(stderr, "[load] count mismatch: loaded=%u header=%u\n", loaded, count_hdr);
        abort(); 
    }

    return ;
}


static int load_hmac_key(void){
    int fd = open("config/hmac.key", O_RDONLY | __O_CLOEXEC| __O_NOFOLLOW);
    if (fd < 0) { perror("hmac.key"); return -1; }
    ssize_t n = read(fd, g_hmac_key, sizeof g_hmac_key);
    close(fd);
    return (n == (ssize_t)sizeof g_hmac_key) ? 0 : -1;
}


int main(int argc, char const *argv[]){

    load_config("config/server.conf", &server_cfg);


    if (sodium_init() < 0) { fprintf(stderr, "sodium_init failed\n"); return 1; }

    if (load_hmac_key() != 0) {
        fprintf(stderr, "[init] HMAC key missing/invalid -> refuse to start\n");
        return 1;
    }

#if !USE_JEMALLOC
    mallopt(M_MMAP_THRESHOLD, 64 * 1024);
    mallopt(M_TRIM_THRESHOLD, 64 * 1024);
#endif


    signal(SIGPIPE, SIG_IGN);

#if IO_URING_ADD
    memset(&add_params, 0, sizeof(add_params));

    u_info_pool_init();

    int add_ok = io_uring_queue_init_params(1024, &add_ring, &add_params);
    if (add_ok < 0) {
        fprintf(stderr, "[init] add_ring init error: %s\n", strerror(-add_ok));
        if (server_cfg.add_save == 1) return 1;   
    }

#else

    if(server_cfg.add_save == 1){

        int aof_fd = get_add_fd();
        if(aof_fd < 0) return 1;
        pthread_t tid;
        if(pthread_create(&tid, NULL, aof_fsync_thread, (void *)(intptr_t)aof_fd) != 0){
            perror("[init] pthread_create aof_fsync");
            return 1;
        }

        pthread_detach(tid);
    }

#endif

    if(server_cfg.use_mempool == 1){
        pool = mp_create(64 * 1024);
        cmd_pool = mp_create(4096);
    }

    init_kvs();
    kvs_ringbuf_init(&g_ring, 64 * 1024 * 1024);

    g_repl_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (g_repl_efd < 0) { perror("eventfd"); return 1; }

    if(server_cfg.add_save == 1) kvs_add_read();
    if(server_cfg.full_save == 1) {
        kvs_full_read();
    }

    rdma_init(&gr);

    kvs_heap_compact();
    

    fprintf(stderr, "master load finish\n");

    // ringbuf_init();

    ntyco_start(&server_cfg, &gr);
    
    dest_kvs();
    mp_destory(pool);
    mp_destory(cmd_pool);

    return 0;
}
