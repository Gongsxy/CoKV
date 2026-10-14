#include "../NtyCo/core/nty_coroutine.h"
#include <netinet/in.h>
#include <sys/random.h>
#include <malloc.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h> 
#include <string.h> 

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


// static int slave_fd = -1;
// static int master_fd = -1;

static volatile int rdma_syncing = 0;

static volatile int fwd_collecting = 0;
static volatile int handshaking = 0;

__g_rdma g_rdma = {0};
__g_slave g_slave = {.fd = -1};
// __g_slave g_s_slave = {0};



typedef struct {
    int    fd;
    char  *buf;        
    size_t len;       
    size_t off;    
    int    sending;    
    int    closed;  
    int    want; 
    size_t cap;
} send_resp_t;


static send_resp_t g_slave_sr = { .fd = -1, .sending = 0 };


static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
}
static void co_sleep_ms(uint64_t ms) {
    uint64_t deadline = now_ms() + ms;
    while (now_ms() < deadline) {
        nty_coroutine_sleep(0);
    }
}



// int load_save_mes(){
//     if(cfg.add_save == 1){
//         int add_save_fd = open(ADD_FILENAME, O_RDONLY);
//         if(add_save_fd < 0) {
//             perror("open failed");
//             return -1;
//         }
//         return add_save_fd;
//     }else if(cfg.full_save == 1){
//         int full_save_fd = open(FULL_FILENAME, O_RDONLY);
//         if(full_save_fd < 0) {
//             perror("open failed");
//             return -1;
//         }
//         return full_save_fd;
//     }else return -1; 
// }



extern int resp_capture_poll(int max_records);
void master_forward_to_slave(const char *data, unsigned len);



static int rdma_push_stream(uint32_t total)//rdma send
{

    uint32_t chunk_max = (uint32_t)g_rdma.buf_size;
    if (chunk_max == 0) { fprintf(stderr, "[push] rdma buf_size == 0\n"); return -1; }
    if (g_slave.fd < 0) { fprintf(stderr, "[push] no slave conn\n");       return -1; }

    void *map = MAP_FAILED;
    uint32_t off = 0;
    int rc = -1;
    struct ibv_mr *mr = NULL;

    if (total == 0) {
        if (rdma_notify_slave(&g_slave, 0) < 0) return -1;
        uint32_t st0;
        if (nty_recv(g_slave.fd, &st0, sizeof(st0), 0) != (ssize_t)sizeof(st0)) return -1;
        return (ntohl(st0) == 1) ? 0 : -1;
    }


    int tmp_fd = open("config/save_mes.tmp", O_RDONLY);
    if (tmp_fd < 0) {
        fprintf(stderr, "[sync] open save_mes.tmp fail: %s\n", strerror(errno));
        return -1;
    }
    struct stat tmp_st;
    if(fstat(tmp_fd, &tmp_st) != 0){
        fprintf(stderr, "open tmp failed\n");
        close(tmp_fd); 
        return -1;
    }

    if ((uint32_t)tmp_st.st_size != total) {
        fprintf(stderr, "[push] size mismatch: file=%ld total=%u\n", (long)tmp_st.st_size, total);
        goto out;
    }

    map = mmap(NULL, tmp_st.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, tmp_fd, 0);
    if (map == MAP_FAILED) { perror("mmap"); goto out; }

    close(tmp_fd); tmp_fd = -1;


    for (;;) {
        uint32_t chunk = total - off;

        if (chunk == 0) { 
            if (rdma_notify_slave(&g_slave, 0) < 0) {
                fprintf(stderr, "[push] notify(0) fail\n");
                goto out;
            }
            break;
        }

        if (chunk > chunk_max) chunk = chunk_max;

        mr = ibv_reg_mr(g_rdma.pd, (char *)map + off, chunk, 0);
        if (!mr) {
            fprintf(stderr, "[push] reg_mr(off=%u len=%u) fail: %s\n",
                    off, chunk, strerror(errno));
            goto out;
        }

        struct ibv_sge sge = {
            .addr = (uint64_t)((char *)map + off),
            .length = chunk,
            .lkey = mr->lkey,
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
            goto out;
        }

        struct ibv_wc wc = {0};
        int n;
        uint64_t cq_start = now_ms();
        while ((n = ibv_poll_cq(g_rdma.cq, 1, &wc)) == 0) {
            if (now_ms() - cq_start > 3000) {
                fprintf(stderr, "[push] poll cq timeout\n");
                goto out;
            }
            nty_coroutine_sleep(0);
        }
        if(n < 0 || wc.status != IBV_WC_SUCCESS){
            fprintf(stderr, "rdma write fail: %s\n", ibv_wc_status_str(wc.status));
            goto out;
        }

        ibv_dereg_mr(mr);
        mr = NULL;

        if (rdma_notify_slave(&g_slave, chunk) < 0) {
            fprintf(stderr, "[push] notify fail len=%u\n", chunk);
            goto out;
        }

        uint32_t ack;                                       
        if (nty_recv(g_slave.fd, &ack, sizeof(ack), 0) != (ssize_t)sizeof(ack)) {
            fprintf(stderr, "[push] wait ack fail off=%u\n", off);
            goto out;
        }

        off += chunk;
    }

    // recv final_finish_sync
    uint32_t st;                                            
    if (nty_recv(g_slave.fd, &st, sizeof(st), 0) != (ssize_t)sizeof(st)) {
        fprintf(stderr, "[push] read final status fail\n");
        goto out;
    }

    rc = (ntohl(st) == 1) ? 0 : -1;

out:
    if (mr) ibv_dereg_mr(mr);
    if (map != MAP_FAILED) munmap(map, total);
    if (tmp_fd >= 0) close(tmp_fd);
    return rc;
}



static int sync_mes_to_rdma(char **msg, size_t *size){//save .tmp
    
    int ret = kvs_hash_save_slave(globle_hash, msg, size);

    if(ret < 0) return -1;

    return ret;
}



static void slave_send_func(void *arg);

void rdma_handshake(void *arg){

    int rc = -1;
    int fd = -1;
    char *mes = NULL;
    size_t mes_size = 0;
    int sync_ret = 0;


    if (g_slave.fd >= 0) nty_close(g_slave.fd);
    g_slave.fd = -1;
    g_slave_sr.fd  = -1;


    g_slave_sr.len = 0;
    g_slave_sr.off = 0;
    fwd_collecting = 1;

    mes_size = 1024 * 1024;
    mes = malloc(mes_size);
    if(!mes){ perror("mes malloc"); goto out; }

    sync_ret = sync_mes_to_rdma(&mes, &mes_size);
    if(sync_ret < 0){ fprintf(stderr, "sync message error\n"); goto out; }
    printf("master snapshot=%d bytes\n", sync_ret);

    free(mes);
    mes = NULL;


    fd = nty_socket(AF_INET, SOCK_STREAM, 0);

    if(fd < 0){
        perror("nty socket error");
        goto out;
    }
    g_slave.fd = fd;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9998);
    addr.sin_addr.s_addr = INADDR_ANY;

    if(inet_pton(AF_INET, cfg.sync_ip, &addr.sin_addr) != 1){
        fprintf(stderr, "[rdma] cfg.sync_ip not IPv4\n");
        goto out;
    }

    if (nty_connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[sync] connect %s:9998 fail: %s\n", cfg.sync_ip, strerror(errno));
        goto out;
    }  

    // fprintf(stderr, "[rdma] connected to %s:9998 fd=%d\n", cfg.sync_ip, fd);

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


    struct { 
        uint32_t qpn; 
        uint32_t psn; 
        union ibv_gid gid; 
    }__attribute__((packed)) local_short;
    local_short.gid = local_gid;
    local_short.psn = htonl(local_psn);
    local_short.qpn = htonl(g_rdma.qp->qp_num);

    if(nty_send(fd, &local_short, sizeof(local_short), 0) != (ssize_t)sizeof(local_short)){
        perror("nty send error"); goto out;
    }

    struct { 
        uint32_t qpn; 
        uint32_t psn; 
        uint32_t rkey;
        uint64_t vaddr; 
        union ibv_gid gid; 
    }__attribute__((packed)) remote_full;

        
    if(nty_recv(fd, &remote_full, sizeof(remote_full), 0) != (ssize_t)sizeof(remote_full)){
        perror("rdma nty recv error"); goto out;
    }

    g_slave.remote_qpn   = ntohl(remote_full.qpn);
    g_slave.remote_psn   = ntohl(remote_full.psn);
    g_slave.remote_rkey  = ntohl(remote_full.rkey);
    g_slave.remote_vaddr = be64toh(remote_full.vaddr);
    g_slave.remote_gid   = remote_full.gid;

    if(rdma_modify(g_rdma.qp, local_psn, g_slave.remote_qpn, &g_slave.remote_gid, g_slave.remote_psn,
                    g_rdma.gid_index) != 0){
        fprintf(stderr, "modify fail\n"); goto out;
    }


    if(rdma_push_stream((uint32_t)sync_ret) < 0){
        fprintf(stderr, "[master] push snapshot FAILED\n"); goto out;
    }
    printf("master snapshot pushed ok\n");

    rc = 0;
    g_slave_sr.fd = fd;
    rdma_syncing = 1;
    if (g_slave_sr.len > g_slave_sr.off && !g_slave_sr.sending) {
        g_slave_sr.sending = 1;
        g_slave_sr.want = 0;
        nty_coroutine *co;
        nty_coroutine_create(&co, slave_send_func, NULL);
    }
    
out:
    free(mes);
    if(rc < 0){
        rdma_syncing = 0;
        fwd_collecting = 0;
        if (fd >= 0 && g_slave.fd == fd){ 
            nty_close(fd);
            g_slave.fd = -1;
        }
        g_slave_sr.fd = -1;
        g_slave_sr.len = 0;
        g_slave_sr.off = 0;
    }

    handshaking = 0;
    return;
    
}



static void slave_send_func(void *arg)//send info in ebpf send 
{
    send_resp_t *sr = &g_slave_sr;

    size_t sendbuf_size = 64 * 1024;

    char *sendbuf = malloc(sendbuf_size);
    if (sendbuf == NULL) {
        sr->sending = 0;
        perror("malloc sendbuf");
        return;
    }
    
    while (1) {
        while (sr->off < sr->len) {
            
            if (!rdma_syncing) {
                sr->sending = 0; 
                free(sendbuf); 
                return;
            }

            size_t chunk = sr->len - sr->off;
            if (chunk > 64 * 1024) chunk = 64 * 1024;

            memcpy(sendbuf, sr->buf + sr->off, chunk);

            ssize_t n = nty_send(sr->fd, sendbuf, chunk, 0);

            if (n > 0) {
                sr->off += (size_t)n;
                continue;
            }



            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)) {
                nty_coroutine_sleep(0);
                continue;
            }
            // fprintf(stderr,
            //         "[send] fatal n=%zd errno=%d (%s)\n",
            //         n, errno, strerror(errno));

            free(sendbuf);

            rdma_syncing = 0;
            fwd_collecting = 0;
            if (sr->fd >= 0 && g_slave.fd == sr->fd) {
                nty_close(g_slave.fd);
                g_slave.fd = -1;
            }

            sr->fd  = -1;
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


void master_forward_to_slave(const char *data, unsigned len) {//ebpf send

    if (!fwd_collecting || data == NULL || len == 0) return;

    if (g_slave.fd >= 0) g_slave_sr.fd = g_slave.fd;


    size_t need = g_slave_sr.len + len;
    if (need > KVS_MAX_FORWARD_QUEUE) {
        fprintf(stderr, "[fwd] queue overflow %zu, drop slave conn\n", need);
        rdma_syncing = 0; 
        fwd_collecting = 0;
        if (g_slave.fd >= 0) {
            nty_close(g_slave.fd);
            g_slave.fd = -1;
        }
        g_slave_sr.fd = -1;
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

    if (rdma_syncing && !g_slave_sr.sending) {
        g_slave_sr.sending = 1;
        g_slave_sr.want = 0;
        nty_coroutine *co;
        nty_coroutine_create(&co, slave_send_func, NULL);
        
    }

}


int kvs_sync(void){//recv SYNC 

    if (handshaking) return -1;
    handshaking = 1;

    nty_coroutine *co = NULL;
    fwd_collecting = 0;
    rdma_syncing = 0;
    if(nty_coroutine_create(&co, rdma_handshake, NULL) != 0){
        handshaking = 0;
        return -1;
    }
    return 0;
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
        uint64_t cur_ms = (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
        if (cur_ms - last_trim_ms >= 1000) {        
            last_trim_ms = cur_ms;
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
    
    printf("master load finish\n");

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
    
    nty_schedule_run();
    return 0;
}

