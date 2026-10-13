#include "../NtyCo/core/nty_coroutine.h"
#include <netinet/in.h>
#include <sys/random.h>
#include <malloc.h>
#include <fcntl.h>


#include "kvstore.h"
#include "rdma.h"
#include "load_conf.h"
#include "../libbpf-bootstrap/examples/c/resp_capture.h"
#include "mem_pool.h"


#define ADD_FILENAME    "config/add.txt"
#define FULL_FILENAME   "config/full.txt"


char *write_command[] = {
    "SET","MOD"
};

static struct mp_pool *pool = NULL;
static Config cfg = {0};
static void *s_storage;


__g_rdma g_rdma = {0};
__g_slave g_slave = {.fd = -1};
__g_slave g_s_slave = {0};

#if 1
int load_save_mes(){
    if(cfg.add_save == 1){
        int add_save_fd = open(ADD_FILENAME, O_RDONLY);
        if(add_save_fd < 0) {
            perror("open failed");
            return -1;
        }
        return add_save_fd;
    }else if(cfg.full_save == 1){
        int full_save_fd = open(FULL_FILENAME, O_RDONLY);
        if(full_save_fd < 0) {
            perror("open failed");
            return -1;
        }
        return full_save_fd;
    }else return -1; 
}
#else
char load_save_mes(){
    if(strcmp(cfg.storage_structure, "hash") == 0){
        kvs_hash_t *hash = s_storage;
        char mes[1024 * 1024] = {0};

        uint32_t count = (uint32_t)kvs_hash_count(hash);
        kvs_hash_save(hash, &mes, 1024 * 1024, count);

        return mes;
    }else if(strcmp(cfg.storage_structure, "array") == 0){
        
    }else if(strcmp(cfg.storage_structure, "skiplist") == 0){
        
    }
}
#endif


extern int resp_capture_poll(int max_records);
void master_forward_to_slave(const char *data, unsigned len);
int rdma_send_mes_to_slave(uint32_t length);

static int rdma_push_stream(const char *stream, uint32_t total)
{
    uint32_t chunk_max = (uint32_t)g_rdma.buf_size;  
    uint32_t off       = 0;

    if (chunk_max == 0) { fprintf(stderr, "[push] rdma buf_size == 0\n"); return -1; }
    if (g_slave.fd < 0) { fprintf(stderr, "[push] no slave conn\n");       return -1; }

    for (;;) {
        uint32_t chunk = total - off;

        if (chunk == 0) { 
            if (rdma_notify_slave(&g_slave, 0) < 0) {
                fprintf(stderr, "[push] notify(0) fail\n");
                return -1;
            }
            break;
        }

        if (chunk > chunk_max) chunk = chunk_max;

        memcpy(g_rdma.buf, stream + off, chunk);

        if (rdma_send_mes_to_slave(chunk) < 0) {
            fprintf(stderr, "[push] write fail off=%u len=%u\n", off, chunk);
            return -1;
        }

        if (rdma_notify_slave(&g_slave, chunk) < 0) {
            fprintf(stderr, "[push] notify fail len=%u\n", chunk);
            return -1;
        }

        uint32_t ack;                                       
        if (nty_recv(g_slave.fd, &ack, sizeof(ack), 0) != (ssize_t)sizeof(ack)) {
            fprintf(stderr, "[push] wait ack fail off=%u\n", off);
            return -1;
        }

        off += chunk;
    }

    uint32_t st;                                            
    if (nty_recv(g_slave.fd, &st, sizeof(st), 0) != (ssize_t)sizeof(st)) {
        fprintf(stderr, "[push] read final status fail\n");
        return -1;
    }

    return (ntohl(st) == 1) ? 0 : -1;
}



static int sync_mes_to_rdma(char **msg, size_t *size){
    
    int ret = kvs_hash_save_slave(globle_hash, msg, size);
    if(ret < 0) return -1;

    return ret;
}

int rdma_send_mes_to_slave(uint32_t length){

    if(g_slave.fd < 0){
        perror("tcp connection error");
        return -1;
    }
    struct ibv_sge sge = {
        .addr = (uint64_t)g_rdma.buf,
        .length = length,
        .lkey = g_rdma.mr->lkey,
    };
    
    struct ibv_send_wr wr = {0};
    wr.wr_id = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.num_sge = 1;
    wr.sg_list = &sge;
    wr.wr.rdma.remote_addr = g_slave.remote_vaddr;
    wr.wr.rdma.rkey = g_slave.remote_rkey;

    struct ibv_send_wr *bad_wr = NULL;
    if(ibv_post_send(g_rdma.qp, &wr, &bad_wr) != 0){
        perror("ibv post send error");
        return -1;
    }

    struct ibv_wc wc = {0};
    int n = 0;
    while((n = ibv_poll_cq(g_rdma.cq, 1, &wc)) == 0){
    };
    if(n < 0 || wc.status != IBV_WC_SUCCESS){
        fprintf(stderr, "rdma write fail: %s\n", ibv_wc_status_str(wc.status));
        return -1;
    }

    return 0;
}

#if 0
void rdma_handshake(void *arg){
    int listen_fd = nty_socket(AF_INET, SOCK_STREAM, 0);
    if(listen_fd < 0){
        perror("nty socket error");
        return ;
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9998);
    addr.sin_addr.s_addr = INADDR_ANY;

    socklen_t len = sizeof(addr);
    if(bind(listen_fd, &addr, len) < 0){
        close(listen_fd);
        perror("bind error");
        return ;
    }

    int ret = listen(listen_fd, 5);
    if(ret < 0){
        close(listen_fd);
        perror("listen");
        return ;
    }

    int fd = nty_accept(listen_fd, (struct sockaddr *)&addr, &len);
    if(fd < 0){
       nty_close(listen_fd);
       perror("nty accept error");
       return ;
   }

    g_slave.fd = fd;

    struct {
        uint32_t qpn;
        uint32_t psn;
        uint32_t rkey;
        uint64_t vaddr;
        union ibv_gid gid;
    }__attribute__((packed)) remote_info;

    ssize_t nr = nty_recv(fd, &remote_info, sizeof(remote_info), 0);
    if(nr != sizeof(remote_info)){
        perror("rdma nty recv error");
        return ;
    }
    

    g_slave.remote_gid = remote_info.gid;
    g_slave.remote_psn = ntohl(remote_info.psn);
    g_slave.remote_qpn = ntohl(remote_info.qpn);
    g_slave.remote_rkey = ntohl(remote_info.rkey);
    g_slave.remote_vaddr = be64toh(remote_info.vaddr);

    g_rdma.gid_index = pick_rocev2_gid_index(g_rdma.ctx, 1);

    union ibv_gid local_gid;
    if(ibv_query_gid(g_rdma.ctx, 1, g_rdma.gid_index, &local_gid) != 0){
        perror("ibv query gid error");
        return ;
    }

    struct {
        uint32_t qpn;
        uint32_t psn;
        union ibv_gid gid;
    }__attribute__((packed)) local_info;

    uint32_t local_psn;
    ssize_t r_ret = getrandom(&local_psn, sizeof(local_psn), 0);
    local_psn &= 0xffffff;
    if (r_ret != sizeof(local_psn)) {
        perror("getrandom error"); 
        return ;
    }

    local_info.gid = local_gid;
    local_info.psn = htonl(local_psn);
    local_info.qpn = htonl(g_rdma.qp->qp_num);

    if(nty_send(fd, &local_info, sizeof(local_info), 0) != sizeof(local_info)){
        perror("nty send error");
        return ;
    }

    if(rdma_modify(g_rdma.qp, local_psn, g_slave.remote_qpn, &g_slave.remote_gid, g_slave.remote_psn, g_rdma.gid_index) != 0){ fprintf(stderr, "modify fail\n"); return ; }

    printf("master load finish\n");


    size_t mes_size = 1024 * 1024 * 1;
    char  *mes      = malloc(mes_size);
    if (!mes) { perror("mes malloc"); return; }
    memset(mes, 0, mes_size);

    int sync_ret = sync_mes_to_rdma(&mes, &mes_size);  
    if (sync_ret < 0) {
        fprintf(stderr, "sync message error\n");
        free(mes);
        return;
    }

    printf("master snapshot=%d bytes", sync_ret);


    int push_ret = rdma_push_stream(mes, (uint32_t)sync_ret);

    free(mes);
    mes = NULL;

    if (push_ret < 0) {
        fprintf(stderr, "[master] push snapshot FAILED\n");
        nty_close(fd);
        g_slave.fd = -1;
        nty_close(listen_fd);
        return;
    }

    printf("master snapshot pushed ok\n");

    nty_close(listen_fd);
    return;
}
#else 
void rdma_handshake(void *arg){


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
    inet_pton(AF_INET, cfg.sync_ip, &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("nty_connect"); nty_close(fd); return;
    }


    g_slave.fd = fd;

    int   rc  = -1;
    char *mes = NULL;

    g_rdma.gid_index = pick_rocev2_gid_index(g_rdma.ctx, 1);

    union ibv_gid local_gid;
    if(ibv_query_gid(g_rdma.ctx, 1, g_rdma.gid_index, &local_gid) != 0){
        perror("ibv query gid error"); goto out;
    }

    uint32_t local_psn;
    if(getrandom(&local_psn, sizeof(local_psn), 0) != sizeof(local_psn)){
        perror("getrandom error"); goto out;
    }
    local_psn &= 0xffffff;


    struct { uint32_t qpn; uint32_t psn; union ibv_gid gid; }
        __attribute__((packed)) local_short;
    local_short.gid = local_gid;
    local_short.psn = htonl(local_psn);
    local_short.qpn = htonl(g_rdma.qp->qp_num);

    if(nty_send(fd, &local_short, sizeof(local_short), 0) != (ssize_t)sizeof(local_short)){
        perror("nty send error"); goto out;
    }

    struct { uint32_t qpn; uint32_t psn; uint32_t rkey;
             uint64_t vaddr; union ibv_gid gid; }
        __attribute__((packed)) remote_full;

    if(nty_recv(fd, &remote_full, sizeof(remote_full), 0) != (ssize_t)sizeof(remote_full)){
        perror("rdma nty recv error"); goto out;
    }

    g_slave.remote_qpn   = ntohl(remote_full.qpn);
    g_slave.remote_psn   = ntohl(remote_full.psn);
    g_slave.remote_rkey  = ntohl(remote_full.rkey);
    g_slave.remote_vaddr = be64toh(remote_full.vaddr);
    g_slave.remote_gid   = remote_full.gid;

    if(rdma_modify(g_rdma.qp, local_psn, g_slave.remote_qpn,
                   &g_slave.remote_gid, g_slave.remote_psn,
                   g_rdma.gid_index) != 0){
        fprintf(stderr, "modify fail\n"); goto out;
    }

    printf("master load finish\n");

    rc = 0; 
    
out:
    free(mes);
    if(rc < 0){
        if(g_slave.fd >= 0) nty_close(g_slave.fd);
        g_slave.fd = -1;
    }

}

// int fd = nty_socket(AF_INET, SOCK_STREAM, 0);

//     if(fd < 0){
//         perror("nty socket error");
//         return ;
//     }

//     int reuse = 1;
//     setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

//     struct sockaddr_in addr;
//     addr.sin_family = AF_INET;
//     addr.sin_port = htons(9998);
//     addr.sin_addr.s_addr = INADDR_ANY;
//     inet_pton(AF_INET, cfg.ip, &addr.sin_addr);

//     if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
//         perror("nty_connect"); nty_close(fd); return;
//     }

//     g_rdma_slave.gid_index = pick_rocev2_gid_index(g_rdma_slave.ctx, 1);

//     union ibv_gid local_gid;
//     if(ibv_query_gid(g_rdma_slave.ctx, 1, g_rdma_slave.gid_index, &local_gid) != 0){
//         perror("ibv query gid error");
//         nty_close(fd);
//         return ;
//     }


//     uint32_t local_psn;
//     ssize_t r_ret = getrandom(&local_psn, sizeof(local_psn), 0);
//     local_psn &= 0xffffff;

//     if (r_ret != sizeof(local_psn)) {
//         perror("getrandom error");
//         nty_close(fd); 
//         return ;
//     }

//     struct {
//         uint32_t qpn;
//         uint32_t psn;
//         uint32_t rkey;
//         uint64_t vaddr;
//         union ibv_gid gid;
//     }__attribute__((packed)) local_info;

//     local_info.gid = local_gid;
//     local_info.psn = htonl(local_psn);
//     local_info.qpn = htonl(g_rdma_slave.qp->qp_num);
//     local_info.rkey = htonl(g_rdma_slave.mr->rkey);
//     local_info.vaddr = htobe64((uint64_t)g_rdma_slave.buf);

//     if(nty_send(fd, &local_info, sizeof(local_info), 0) != sizeof(local_info)){
//         perror("nty send error");
//         nty_close(fd);
//         return ;
//     }

//     struct {
//         uint32_t qpn;
//         uint32_t psn;
//         union ibv_gid gid;
//     }__attribute__((packed)) remote_info;

//     ssize_t nr = nty_recv(fd, &remote_info, sizeof(remote_info), 0);
//     if(nr != sizeof(remote_info)){
//         perror("rdma nty recv error");
//         nty_close(fd);
//         return ;
//     }

//     g_s_slave.remote_gid = remote_info.gid;
//     g_s_slave.remote_psn = ntohl(remote_info.psn);
//     g_s_slave.remote_qpn = ntohl(remote_info.qpn);

//     rdma_modify(g_rdma_slave.qp, local_psn, g_s_slave.remote_qpn, &g_s_slave.remote_gid, g_s_slave.remote_psn, g_rdma_slave.gid_index);
#endif

typedef struct {
    int    fd;
    char  *buf;        /* 队列 */
    size_t len;        /* 有效字节数 */
    size_t off;        /* 已发出偏移 */
    int    sending;    /* 是否已有发送协程在跑（保证同一时刻只有一个写者） */
    int    closed;     /* 读端已结束 */
    int    want;       /* 队列里又追加了新数据，写者协程别退出 */
    size_t cap;
} send_resp_t;


static send_resp_t g_slave_sr = { .fd = -1, .sending = 0 };

static void slave_send_func(void *arg)
{
    send_resp_t *sr = &g_slave_sr;

    char *sendbuf = malloc(64 * 1024);
    if (sendbuf == NULL) {
        sr->sending = 0;
        perror("malloc sendbuf");
        return;
    }
    
    while (1) {
        while (sr->off < sr->len) {

            size_t chunk = sr->len - sr->off;
            if (chunk > 64 * 1024) chunk = 64 * 1024;

            memcpy(sendbuf, sr->buf + sr->off, chunk);

            ssize_t n = nty_send(sr->fd, sendbuf, chunk, 0);

            if (n > 0) {
                sr->off += (size_t)n;
                continue;
            }



            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)) continue;

            // fprintf(stderr,
            //         "[send] fatal n=%zd errno=%d (%s)\n",
            //         n, errno, strerror(errno));

            free(sendbuf);

            sr->off = 0;
            sr->len = 0;
            sr->sending = 0;
            return;
            
        }

        if (sr->off > 0) {
            size_t remain = sr->len - sr->off;

            if (remain > 0) {
                memmove( sr->buf, sr->buf + sr->off, remain);
            }

            sr->len = remain;
            sr->off = 0;
        }
        if (!sr->want) { sr->sending = 0; free(sendbuf); return; }
        sr->want = 0;
    }

}

void master_forward_to_slave(const char *data, unsigned len) {

    if (g_slave.fd < 0 || data == NULL || len == 0) return;


    g_slave_sr.fd = g_slave.fd;



    size_t need = g_slave_sr.len + len;
    if (need > KVS_MAX_FORWARD_QUEUE) {
        fprintf(stderr, "[fwd] queue overflow %zu, drop slave conn\n", need);
        nty_close(g_slave.fd); g_slave.fd = -1;
        g_slave_sr.len = g_slave_sr.off = 0;
        return;
    }

    if (need > g_slave_sr.cap) {
       size_t newcap = g_slave_sr.cap ? g_slave_sr.cap : (1024 * 1024);
        while (newcap < need) newcap += newcap / 2;
        void *p = realloc(g_slave_sr.buf, newcap);
        if (!p) { fprintf(stderr, "[fwd] realloc fail\n"); return; }
        g_slave_sr.buf = p;
        g_slave_sr.cap = newcap;
    }

    memcpy(g_slave_sr.buf + g_slave_sr.len, data, len);
    g_slave_sr.len += len;
    g_slave_sr.want = 1;

    if (!g_slave_sr.sending) {
        g_slave_sr.sending = 1;
        g_slave_sr.want = 0;
        nty_coroutine *co;
        nty_coroutine_create(&co, slave_send_func, NULL);
    }

}



void send_func(void *arg){

    send_resp_t *sr = arg;

    while(sr->off < sr->len){
        size_t chunk = sr->len - sr->off;
        if (chunk > 64 * 1024) chunk = 64 * 1024; 

        ssize_t n = nty_send(sr->fd, sr->buf + sr->off, chunk, 0);
        if(n > 0) {sr->off += (size_t)n; continue;}
        if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            continue;
        }
        sr->off = 0;
        sr->len = 0;
        sr->sending = 0;
        if (sr->closed) { nty_close(sr->fd); free(sr->buf); free(sr); }
        return;
    }

    if (sr->off >= sr->len) { 
        sr->off = 0; 
        sr->len = 0; 
    }

    sr->sending = 0;

}



void read_func(void *arg){
    int cli_fd = *(int *)arg;
    if(cfg.use_mempool == 0){
        free(arg);
    }

    if(cli_fd < 0){
        perror("read arg error");
        return ;
    }
    char *buf = NULL; char *response = NULL;

    size_t response_cap = 1024 * 64;
    // size_t capbuf_cap   = 1024 * 1024;
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
        free(buf); 
        free(response);
        buf = NULL; 
        response = NULL;
        return ;
    }

    send_resp_t *sr = calloc(1, sizeof(send_resp_t));;
    if(!sr) { free(buf); free(response); nty_close(cli_fd); return; }
    sr->fd = cli_fd;

    while(1){

        int recv_len = nty_recv(cli_fd, buf, 64 * 1024 - 1, 0);

        if (recv_len == 0) { resp_capture_poll(0); break; }

        if (recv_len < 0) {
            resp_capture_poll(0);
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("nty_recv");
            break;
        }

        int length = kvs_protocol(cli_fd, buf, recv_len, &response, &response_cap);
        resp_capture_poll(0);

        if (length < 0) {
            goto out;
        }

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

        
        static uint64_t last_trim_ms = 0;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_ms = (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
        if (now_ms - last_trim_ms >= 1000) {        
            last_trim_ms = now_ms;
            malloc_trim(0);
        }
        
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
    if(cfg.use_mempool == 0){
        free(arg);
    }
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
        perror("bind error");
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
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue; 
                if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS) continue; 
                if (errno == EBADF || errno == EINVAL) break;
                perror("nty_accept");
                continue;
            }
        }

        
        int *arg = malloc(sizeof(int));
        if (!arg) { nty_close(client_fd); continue; }
        *arg = client_fd;

        nty_coroutine *read_co;
        if (nty_coroutine_create(&read_co, read_func, arg) != 0) {           
            nty_close(client_fd);
            free(arg);
        }
    }
}


int ntyco_start(Config *server_cfg, __g_rdma *g_m_rdma){
    cfg = *server_cfg;
    nty_coroutine *co = NULL;

    int *p = calloc(1, sizeof(int));  if(!p){ return -1; } *p = cfg.port;
    int *p2 = calloc(1, sizeof(int));  if(!p2){ return -1; } *p2 = cfg.port;
    g_rdma = *g_m_rdma;

    if(cfg.use_mempool == 1){
        pool = mp_create(64 * 1024);
    }
    if (ringbuf_init() < 0) {
        fprintf(stderr, "ringbuf_init failed\n");
        return -1;
    }


    nty_coroutine_create(&co, accept_func, p);
    nty_coroutine_create(&co, rdma_handshake, p2);
    nty_schedule_run();
    return 0;
}

