
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


static int g_listen_ready;
// static int rdma_syncing = 0;

Config cfg;

__g_rdma g_rdma_slave = {0};
// __g_slave g_s_slave = {0};

__g_slave g_slave = {.fd = -1};



static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
}

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000ull;
}

static void co_sleep_ms(uint64_t ms) {
    uint64_t deadline = now_ms() + ms;
    while (now_ms() < deadline) {
        nty_coroutine_sleep(0);
    }
}


#define KEY_VALUE_OFF   4
static int kvs_apply_kv_chunk(const char *msg, uint32_t len)//解析全量日誌
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

    int listen_fd = nty_socket(AF_INET, SOCK_STREAM, 0);
    if(listen_fd < 0){
        perror("nty socket error");
        return ;
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9998);
    addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        nty_close(listen_fd);
        perror("bind error");
        return ;
    }

    int ret = listen(listen_fd, 5);
    if(ret < 0){
        nty_close(listen_fd);
        perror("listen");
        return ;
    }

    g_listen_ready = 1;


    const size_t buf_size = 1024 * 1024;
    size_t res_size = 1024 * 1024;
    


    for(;;){

        char *cbuf = NULL, *cres = NULL;
        res_size = 1024 * 1024;
        int total_loaded = 0;
        int snap_failed  = 0;


        uint64_t t_load0 = 0, t_load1 = 0;     // 新增
        uint64_t t_apply_us = 0, t_wait_us = 0; // 新增
        int chunks = 0;                         // 新增


        uint32_t local_psn = 0;
        union ibv_gid local_gid;

        socklen_t len = sizeof(addr);

        int fd = nty_accept(listen_fd, (struct sockaddr *)&addr, &len);
        if(fd < 0){
            perror("nty accept error");
            co_sleep_ms(100);
            continue;
        }

        g_slave.fd = fd;

        cbuf = calloc(1, buf_size);
        cres = calloc(1, res_size);
        if (!cbuf || !cres) goto conn_out;

#if 0
        char mode = 0;
        {
            int got = 0;
            while (got < 1) {
                ssize_t r = nty_recv(fd, &mode + got, 1 - got, 0);
                if (r > 0) { got += (int)r; break; }
                if (r == 0) { fprintf(stderr, "[snap] peer closed\n"); goto conn_out; }
                if (errno == EAGAIN || errno == EWOULDBLOCK ||
                    errno == EINTR   || errno == EINPROGRESS) {
                    nty_coroutine_sleep(0);          /* ★ 关键：让出后重试 */
                    continue;
                }
                fprintf(stderr, "[snap] recv mode errno=%d (%s)\n", errno, strerror(errno));
                goto conn_out;
            }
        }



        if (mode == 'S') {
            /* ================= sendfile 模式 ================= */
            uint32_t total_net = 0;
            if (nty_recv(fd, &total_net, sizeof(total_net), 0) != (ssize_t)sizeof(total_net)) {
                fprintf(stderr, "[snap] recv total fail\n");
                goto conn_out;
            }
            uint32_t total = ntohl(total_net);

            uint64_t t_a0 = now_us();
            uint32_t got = 0;

            while (got < total) {
                uint32_t want = total - got;
                if (want > buf_size) want = (uint32_t)buf_size;

                int n = nty_recv(fd, cbuf, want, 0);
                if (n > 0) {
                    int r = kvs_apply_kv_chunk(cbuf, (uint32_t)n);
                    if (r < 0) { snap_failed = 1; break; }
                    total_loaded += r;
                    got += (uint32_t)n;
                    chunks++;
                    continue;
                }
                if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                    nty_coroutine_sleep(0);
                    continue;
                }
                fprintf(stderr, "[snap] recv body fail at %u/%u\n", got, total);
                snap_failed = 1;
                break;
            }
            t_apply_us = now_us() - t_a0;

            fprintf(stderr, "[slave] sendfile total=%.3fs chunks=%d records=%d\n",
                    t_apply_us / 1e6, chunks, total_loaded);

            /* 发最终状态 */
            {
                uint32_t final_st = htonl(snap_failed ? 0 : 1);
                if (nty_send(fd, &final_st, sizeof(final_st), 0) != (ssize_t)sizeof(final_st)) {
                    fprintf(stderr, "[snap] send final status fail\n");
                }
            }
            goto conn_out;              /* sendfile 模式结束，不走后面的增量循环 */
        }
#endif


        struct { uint32_t qpn; uint32_t psn; union ibv_gid gid; }
            __attribute__((packed)) remote_short;

        if(nty_recv(fd, &remote_short, sizeof(remote_short), 0) != (ssize_t)sizeof(remote_short)){
            perror("rdma nty recv error"); goto conn_out;
        }

        g_slave.remote_qpn = ntohl(remote_short.qpn);
        g_slave.remote_psn = ntohl(remote_short.psn);
        g_slave.remote_gid = remote_short.gid;

        g_rdma_slave.gid_index = pick_rocev2_gid_index(g_rdma_slave.ctx, 1);


        if(ibv_query_gid(g_rdma_slave.ctx, 1, g_rdma_slave.gid_index, &local_gid) != 0){
            perror("ibv query gid error"); goto conn_out;
        }

        if(getrandom(&local_psn, sizeof(local_psn), 0) != sizeof(local_psn)){
            perror("getrandom error"); goto conn_out;
        }
        local_psn &= 0xffffff;

        struct { uint32_t qpn; uint32_t psn; uint32_t rkey;
                uint64_t vaddr; union ibv_gid gid; }
            __attribute__((packed)) local_full;
        local_full.qpn   = htonl(g_rdma_slave.qp->qp_num);
        local_full.psn   = htonl(local_psn);
        local_full.rkey  = htonl(g_rdma_slave.mr->rkey);
        local_full.vaddr = htobe64((uint64_t)g_rdma_slave.buf);
        local_full.gid   = local_gid;

        if(nty_send(fd, &local_full, sizeof(local_full), 0) != (ssize_t)sizeof(local_full)){
            perror("nty send error"); goto conn_out;
        }

        if(rdma_modify(g_rdma_slave.qp, local_psn, g_slave.remote_qpn,
                    &g_slave.remote_gid, g_slave.remote_psn,
                    g_rdma_slave.gid_index) != 0){
            fprintf(stderr, "modify fail\n"); goto conn_out;
        }
        
t_load0 = now_us();

        for (;;) {
            uint32_t len_net;
uint64_t w0 = now_us();
            if (nty_recv(fd, &len_net, sizeof(len_net), 0) != (ssize_t)sizeof(len_net)) {
                fprintf(stderr, "[snap] recv length fail\n");
                snap_failed = 1;
                break;
            }
t_wait_us += now_us() - w0;
            uint32_t chunk_len = ntohl(len_net);
            if (chunk_len == 0) break;

            if (chunk_len > g_rdma_slave.buf_size) {
                fprintf(stderr, "[snap] chunk %u > buf %zu —— please modify rdma buf_size to >= %u\n",
                        chunk_len, g_rdma_slave.buf_size, chunk_len);
                snap_failed = 1;
                break;
            }
uint64_t a0 = now_us();
            int n = kvs_apply_kv_chunk(g_rdma_slave.buf, chunk_len);
t_apply_us += now_us() - a0;
chunks++;
            if (n < 0) { snap_failed = 1; break; }
            total_loaded += n;

            uint32_t ack = htonl(1);
            if (nty_send(fd, &ack, sizeof(ack), 0) != (ssize_t)sizeof(ack)) {
                fprintf(stderr, "[snap] send ack fail\n");
                snap_failed = 1;
                break;
            }
        }
t_load1 = now_us();
// fprintf(stderr, "[slave] load total=%.3fs wait_master=%.3fs apply=%.3fs chunks=%d records=%d\n",
//             (t_load1 - t_load0) / 1e6, t_wait_us / 1e6, t_apply_us / 1e6,
//             chunks, total_loaded);
        if (snap_failed) {
            fprintf(stderr, "[slave] snapshot aborted, closing\n");
            goto conn_out;
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

        //sync finish success --> 1 failed --> 0
        {
            uint32_t final_st = htonl(snap_failed ? 0 : 1);
            if (nty_send(fd, &final_st, sizeof(final_st), 0) != (ssize_t)sizeof(final_st)) {
                fprintf(stderr, "[snap] send final status fail\n");
                goto conn_out;
            }
        }

        if (snap_failed) goto conn_out;


        uint64_t t_snap_done = now_us();
fprintf(stderr, "[SNAP_DONE] t=%llu records=%d\n", (unsigned long long)t_snap_done, total_loaded);

uint64_t t_repl_first = 0;

        for (;;) {
            int n = nty_recv(fd, cbuf, buf_size, 0);
            if (n == 0) break;
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                perror("repl nty_recv");
                break;
            }

if (t_repl_first == 0) {
        t_repl_first = now_us();
        fprintf(stderr, "[REPL_FIRST] t=%llu dt_since_snap_done=%.3f ms\n",
                (unsigned long long)t_repl_first,
                (t_repl_first - t_snap_done) / 1000.0);
    }

            int r = kvs_protocol(fd, cbuf, n, &cres, &res_size);
            if (r < 0) {
                fprintf(stderr, "[repl] apply fail(%d), drop stream\n", r);
                break;
            }
        }

conn_out:
        free(cbuf);  cbuf = NULL;
        free(cres);  cres = NULL;
        if (fd >= 0) {
            kvs_conn_ctx_free(fd);
            nty_close(fd);
        }
        g_slave.fd = -1;
        g_snap_len = 0;  
    }

}


void normal_func(void *arg){

    while(!g_listen_ready){
        co_sleep_ms(100);
    }

    int master_fd = nty_socket(AF_INET, SOCK_STREAM, 0);

    if (master_fd < 0) return;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg.port);
    if(inet_pton(AF_INET, cfg.ip, &addr.sin_addr) != 1){ 
        fprintf(stderr, "[repl] cfg.sync_ip not IPv4\n");
        nty_close(master_fd); 
        return ;
    }

    if(nty_connect(master_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0){ 
        fprintf(stderr, "[repl] connect master fail: %s\n", strerror(errno));
        nty_close(master_fd); 
        return;
    }

    nty_send(master_fd, "*1\r\n$4\r\nSYNC\r\n", 14, 0);

    nty_close(master_fd);
        
    return; 
}






typedef struct {
    int    fd;
    char  *buf;        
    size_t len;        
    size_t off;        
    int    sending;   
    int    closed;     
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

    printf("slave load finish\n");

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

    nty_coroutine *co_a = NULL, *co_b = NULL;

    int *p = calloc(1, sizeof(int));
    *p = server_cfg->port;

    g_rdma_slave = *g_m_rdma;


    nty_coroutine_create(&co_a, accept_func, p);
    nty_coroutine_create(&co_b, rdma_handshake, NULL);
    nty_coroutine *co_repl = NULL;
    nty_coroutine_create(&co_repl, normal_func, NULL);

    nty_schedule_run();

    return 0;
}
