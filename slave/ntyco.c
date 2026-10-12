
#include "../NtyCo/core/nty_coroutine.h"
#include "kvstore.h"
#include "load_conf.h"
#include "../src/rdma.h"



#include <netinet/in.h>
#include <infiniband/verbs.h>
#include <sys/random.h>
#include <arpa/inet.h>
#include <endian.h> 
#include <malloc.h>
#include <limits.h>

#define SNAP_INIT_SIZE  (1024 * 1024) 

static char   *g_snap_buf = NULL;
static size_t  g_snap_cap = 0;
static size_t  g_snap_len = 0;

Config cfg;

__g_rdma g_rdma_slave = {0};
__g_slave g_s_slave = {0};




#define KEY_VALUE_OFF   4
static int kvs_apply_kv_chunk(const char *msg, uint32_t len)
{
    if (!msg) return -1;


    if (g_snap_len + len > g_snap_cap) {
        size_t newcap = g_snap_cap ? g_snap_cap : SNAP_INIT_SIZE;
        while (newcap < g_snap_len + len) newcap *= 2;

        void *p = realloc(g_snap_buf, newcap);
        if (!p) { fprintf(stderr, "[snap] oom\n"); return -1; }
        g_snap_buf = p;
        g_snap_cap = newcap;
    }
    memcpy(g_snap_buf + g_snap_len, msg, len);
    g_snap_len += len;


    char *cname = "SET";
    if (strncmp(cfg.storage_structure, "hash", 4) == 0)          cname = "HSET";
    else if (strncmp(cfg.storage_structure, "skiplist", 8) == 0) cname = "SSET";

    size_t i = 0;
    int    loaded = 0;

    while (g_snap_len - i >= KEY_VALUE_OFF) {
        const char *p = g_snap_buf + i;

        uint32_t k_len;
        memcpy(&k_len, p, KEY_VALUE_OFF);
        if (k_len == 0 || k_len > (uint32_t)INT_MAX) return -1;

        size_t off = KEY_VALUE_OFF + k_len;
        if (off > g_snap_len - i) break;

        const char *key = p + KEY_VALUE_OFF;
        if (g_snap_len - i - off < KEY_VALUE_OFF) break; 

        uint32_t v_len;
        memcpy(&v_len, key + k_len, KEY_VALUE_OFF);
        if (v_len > (uint32_t)INT_MAX) return -1;

        size_t total = off + KEY_VALUE_OFF + v_len;
        if (total > g_snap_len - i) break; 

        const char *value = key + k_len + KEY_VALUE_OFF;
        kvs_txt_load(cname, (char *)key, (char *)value, k_len, v_len);

        loaded++;
        i += total;
    }

    if (i > 0) {
        memmove(g_snap_buf, g_snap_buf + i, g_snap_len - i);
        g_snap_len -= i;
    }

    return loaded;
}


void rdma_handshake(void *arg)
{
    int fd = nty_socket(AF_INET, SOCK_STREAM, 0);

    if(fd < 0){
        perror("nty socket error");
        return ;
    }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9998);
    addr.sin_addr.s_addr = INADDR_ANY;
    inet_pton(AF_INET, cfg.ip, &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("nty_connect"); nty_close(fd); return;
    }

    g_rdma_slave.gid_index = pick_rocev2_gid_index(g_rdma_slave.ctx, 1);

    union ibv_gid local_gid;
    if(ibv_query_gid(g_rdma_slave.ctx, 1, g_rdma_slave.gid_index, &local_gid) != 0){
        perror("ibv query gid error");
        nty_close(fd);
        return ;
    }


    uint32_t local_psn;
    ssize_t r_ret = getrandom(&local_psn, sizeof(local_psn), 0);
    local_psn &= 0xffffff;

    if (r_ret != sizeof(local_psn)) {
        perror("getrandom error");
        nty_close(fd); 
        return ;
    }

    struct {
        uint32_t qpn;
        uint32_t psn;
        uint32_t rkey;
        uint64_t vaddr;
        union ibv_gid gid;
    }__attribute__((packed)) local_info;

    local_info.gid = local_gid;
    local_info.psn = htonl(local_psn);
    local_info.qpn = htonl(g_rdma_slave.qp->qp_num);
    local_info.rkey = htonl(g_rdma_slave.mr->rkey);
    local_info.vaddr = htobe64((uint64_t)g_rdma_slave.buf);

    if(nty_send(fd, &local_info, sizeof(local_info), 0) != sizeof(local_info)){
        perror("nty send error");
        nty_close(fd);
        return ;
    }

    struct {
        uint32_t qpn;
        uint32_t psn;
        union ibv_gid gid;
    }__attribute__((packed)) remote_info;

    ssize_t nr = nty_recv(fd, &remote_info, sizeof(remote_info), 0);
    if(nr != sizeof(remote_info)){
        perror("rdma nty recv error");
        nty_close(fd);
        return ;
    }

    g_s_slave.remote_gid = remote_info.gid;
    g_s_slave.remote_psn = ntohl(remote_info.psn);
    g_s_slave.remote_qpn = ntohl(remote_info.qpn);

    rdma_modify(g_rdma_slave.qp, local_psn, g_s_slave.remote_qpn, &g_s_slave.remote_gid, g_s_slave.remote_psn, g_rdma_slave.gid_index);

    printf("slave load finish\n");
    
    int total_loaded = 0;
    int snap_failed  = 0;

    for (;;) {
        uint32_t len_net;
        if (nty_recv(fd, &len_net, sizeof(len_net), 0) != (ssize_t)sizeof(len_net)) {
            fprintf(stderr, "[snap] recv length fail\n");
            snap_failed = 1;
            break;
        }

        uint32_t chunk_len = ntohl(len_net);
        if (chunk_len == 0) break;

        if (chunk_len > g_rdma_slave.buf_size) {
            fprintf(stderr, "[snap] chunk %u > rdma buf %zu\n",
                    chunk_len, g_rdma_slave.buf_size);
            snap_failed = 1;
            break;
        }

        int n = kvs_apply_kv_chunk(g_rdma_slave.buf, chunk_len);
        if (n < 0) { snap_failed = 1; break; }
        total_loaded += n;

        uint32_t ack = htonl(1);
        if (nty_send(fd, &ack, sizeof(ack), 0) != (ssize_t)sizeof(ack)) {
            fprintf(stderr, "[snap] send ack fail\n");
            snap_failed = 1;
            break;
        }
    }

    if (snap_failed) {
        fprintf(stderr, "[slave] snapshot aborted, closing\n");
        nty_close(fd);
        return;
    }

    if (g_snap_len != 0) {
        fprintf(stderr, "[snap] stream tail not aligned: %zu bytes left\n", g_snap_len);
        snap_failed = 1;
    }

    if (g_snap_buf && g_snap_cap > SNAP_INIT_SIZE) {
        void *p = realloc(g_snap_buf, SNAP_INIT_SIZE);
        if (p) { g_snap_buf = p; g_snap_cap = SNAP_INIT_SIZE; }
    }

    printf("slave snapshot loaded: %d records (failed=%d)\n", total_loaded, snap_failed);


    uint32_t final_st = htonl(snap_failed ? 0 : 1);
    if (nty_send(fd, &final_st, sizeof(final_st), 0) != (ssize_t)sizeof(final_st)) {
        fprintf(stderr, "[snap] send final status fail\n");
        nty_close(fd);
        return;
    }

    size_t buf_size = 1024 * 1024 * 1;
    size_t res_size = 1024 * 1024 * 1;
    char  *cbuf = calloc(1, buf_size);
    char  *cres = calloc(1, res_size);
    if (!cbuf || !cres) { free(cbuf); free(cres); nty_close(fd); return; }

    for (;;) {
        int n = nty_recv(fd, cbuf, buf_size, 0);
        if (n <= 0) break;
        kvs_protocol(fd, cbuf, n, &cres, &res_size);
    }

    free(cbuf);
    free(cres);
    return;
}


typedef struct {
    int    fd;
    char  *buf;        /* 队列 */
    size_t len;        /* 有效字节数 */
    size_t off;        /* 已发出偏移 */
    int    sending;    /* 是否已有发送协程在跑（保证同一时刻只有一个写者） */
    int    closed;     /* 读端已结束 */
} send_resp_t;

static int send_resp_append(send_resp_t *s, char *msg, int length){

    if(s == NULL || msg == NULL || length == 0) return -1;

    void *p = realloc(s->buf, s->len + length);
    if(p == NULL) return -2;
    s->buf = p;

    memcpy(s->buf + s->len, msg, length);
    s->len += length;

    return 0;
}


void send_func(void *arg){

    send_resp_t *sr = arg;

    while(sr->off < sr->len){
        size_t chunk = sr->len - sr->off;
        if (chunk > 64 * 1024) chunk = 64 * 1024;

        ssize_t n = nty_send(sr->fd, sr->buf + sr->off, chunk, 0);
        if(n > 0) {sr->off += (size_t)n; continue;}
        if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)){
            continue;
        }
        break;
    }

    if(sr->off >= sr->len) {
        sr->off = 0; sr->len = 0;
    }
    sr->sending = 0;

    if (sr->closed && sr->len == 0) {
        // nty_close(sr->fd);
        free(sr->buf);
        free(sr);
    }
    
}


static uint64_t g_last_trim_ms = 0;
static void trim_func(void){

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;

    if (now - g_last_trim_ms >= 3000) {
        g_last_trim_ms = now;
        malloc_trim(0);
    }
}

void read_func(void *arg){
    int cli_fd = *(int *)arg;

    free(arg);

    if(cli_fd < 0){
        perror("read arg error");
        return ;
    }
    const int ctx_fd = cli_fd;

    char *buf = NULL; 
    char *response = NULL;

    size_t response_cap = 1024 * 64;

    // if(cfg.use_mempool == 1){
    //     buf = mp_alloc(pool, 64 * 1024);
    //     response = mp_alloc(pool, 1024 * 1024 * 1);
    //     capbuf = mp_alloc(pool, 1024 * 1024 * 1);
    // }else {
        buf = calloc(1, 64 * 1024);
        response = calloc(1, response_cap);
        // capbuf = calloc(1, 1024 * 1024 * 1);
    // }
    if (!buf || !response) {
        nty_close(cli_fd);
        free(buf); free(response);
        buf = NULL; response = NULL;
        return ;
    }

    send_resp_t *sr = calloc(1, sizeof(send_resp_t));
    if(!sr) { free(buf); free(response); nty_close(cli_fd); return; }
    sr->fd = cli_fd;


    while(1){
        int recv_len = nty_recv(cli_fd, buf, 64 * 1024 - 1, 0);

        if (recv_len == 0) {
            break;
        }

        if (recv_len < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }

            perror("nty_recv");
            break;
        }

        int length = kvs_protocol(cli_fd, buf, recv_len, &response, &response_cap);
        
        if (length < 0) goto out;

        if(length > 0){

            size_t off = 0;
            while (off < (size_t)length) {
                ssize_t n = nty_send(cli_fd, response + off, (size_t)length - off, 0);

                if (n > 0) {
                    off += (size_t)n;
                    continue;
                }

                if (n < 0 && errno == EINTR) {
                    continue;
                }

                goto out;

            }

        }

        trim_func();
    }
 
out:
    if (sr) {
        sr->closed = 1;
        if (!sr->sending) {
            free(sr->buf); sr->buf = NULL;
            free(sr);
        }
        sr = NULL;
    }

    kvs_conn_ctx_free(cli_fd);
    nty_close(cli_fd);

    free(buf);      buf = NULL;
    free(response); response = NULL;
    return ;

}


void accept_func(void *arg){

    int port = *(int *)arg;
    free(arg);
    if(!port) return ;
    int fd = nty_socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0){
        perror("nty_socket error");
        return ;
    }


    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if(bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))){
        perror("bind2 error");
        nty_close(fd);
        return;
    }

    int listen_ret = listen(fd, 10);
    if(listen_ret < 0){
        perror("listen error");
        nty_close(fd);
        return ;
    }

    while(1){
        socklen_t len = sizeof(struct sockaddr_in);
        int client_fd = nty_accept(fd, (struct sockaddr *)&addr, &len);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue; 
            if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS) continue; 
            if (errno == EBADF || errno == EINVAL) break;
            perror("nty_accept");
            continue;
        }

        int *arg = malloc(sizeof(int));
        if (!arg) { nty_close(client_fd); continue; }
        *arg = client_fd;

        int sndbuf = 8 * 1024 * 1024;
        int rcvbuf = 8 * 1024 * 1024;
        setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
        setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        nty_coroutine *read_co;
        if(nty_coroutine_create(&read_co, read_func, arg) != 0){
            nty_close(client_fd);
            free(arg);
        }
    }
}


int ntyco_start(Config *server_cfg, __g_rdma *g_m_rdma){

    srand(time(NULL));

    cfg = *server_cfg;

    nty_coroutine *co = NULL;
    int *p = calloc(1, sizeof(int));
    *p = server_cfg->port;
    int *p2 = calloc(1, sizeof(int));  
    if(!p2){ return -1; } 
    *p2 = cfg.port;

    g_rdma_slave = *g_m_rdma;


    nty_coroutine_create(&co, accept_func, p);
    nty_coroutine_create(&co, rdma_handshake, p2);

    nty_schedule_run();

    return 0;
}
