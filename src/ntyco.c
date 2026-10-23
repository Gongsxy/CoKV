

#include "kvstore.h"
#include "rdma.h"
#include "load_conf.h"

//  #include "../libbpf-bootstrap/examples/c/resp_capture.h"

#include "../storage/storage.h"
#include "ringbuf.h"

#include <netinet/in.h>

 #include <sys/random.h>

#include <malloc.h>
#include <fcntl.h>

#include <errno.h> 



#include <sys/sendfile.h>


#define FULL_FILE_TMP   "config/full.tmp"


#define EBPF        0


static Config cfg = {0};


static volatile int rdma_syncing = 0;
static volatile int fwd_collecting = 0;
static volatile int fwd_queueing = 0;


static volatile int handshaking = 0;

__g_rdma g_rdma = {0};
__g_slave g_slave = {.fd = -1};



#if EBPF
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

static void slave_send_func(void *arg);
extern int resp_capture_poll(int max_records);
void master_forward_to_slave(const char *data, unsigned len);

#endif

static int sendfile_push_stream(uint32_t total)
{
    if (g_slave.fd < 0) { fprintf(stderr, "[sf] no slave conn\n"); return -1; }

    int tmp_fd = open(FULL_FILE_TMP, O_RDONLY);
    if (tmp_fd < 0) {
        fprintf(stderr, "[sf] open full.txt fail: %s\n", strerror(errno));
        return -1;
    }

    struct stat st;
    if (fstat(tmp_fd, &st) != 0) { perror("fstat"); close(tmp_fd); return -1; }
    if ((uint32_t)st.st_size != total) {
        fprintf(stderr, "[sf] size mismatch: file=%ld total=%u\n", (long)st.st_size, total);
        close(tmp_fd);
        return -1;
    }

    int rc = -1;
    size_t sent = 0;

    while (sent < total) {
        size_t remain = total - sent;
        if (remain > 0x7ffff000u) remain = 0x7ffff000u;   /* sendfile limit */

        ssize_t n = sendfile(g_slave.fd, tmp_fd, NULL, remain);

        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            nty_coroutine_sleep(0);          
            continue;
        }
        if (n == 0) break;                     
        perror("[sf] sendfile");
        goto out;
    }

    
    {
        uint32_t st_net;
        if (nty_recv(g_slave.fd, &st_net, sizeof(st_net), 0) != (ssize_t)sizeof(st_net)) {
            fprintf(stderr, "[sf] read final status fail\n");
            goto out;
        }
        rc = (ntohl(st_net) == 1) ? 0 : -1;
    }

out:
    close(tmp_fd);
    return rc;
}



static int repl_make_snapshot(char **msg, size_t *size){//save .tmp
    extern kvs_rbtree_t *globle_rbtree;
    extern kvs_skiplist_t *globle_skiplist;
    extern kvs_array_t *globle_array;
    int ret = 0;
    if (strncmp(cfg.storage_structure, "array", 5) == 0){
        ret = kvs_array_save_slave(globle_array, msg, size);
    }else if (strncmp(cfg.storage_structure, "rbtree", 6) == 0){
        ret = kvs_rbtree_save_slave(globle_rbtree, msg, size);
    }else if (strncmp(cfg.storage_structure, "hash", 4) == 0){
        ret = kvs_hash_save_slave(globle_hash, msg, size);
    }else if (strncmp(cfg.storage_structure, "skiplist", 8) == 0){
        ret = kvs_skiplist_save_slave(globle_skiplist, msg, size);
    }

    if(ret < 0) return -1;

    return ret;
}



static void repl_sender_func(void *arg)
{
    int fd = g_slave.fd;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    for(;;){

        if(g_slave.fd != fd) break;

        char *p;
        size_t n = ringbuf_peek(&g_ring, &p);

        if(n == 0){

            g_sender_waiting = 1;

            uint64_t v;
            read(g_repl_efd, &v, sizeof(v));
            g_sender_waiting = 0;
            continue;

        }

        if(n > 64 * 1024) n = 64 * 1024;
        ssize_t s = nty_send(fd, p, n, 0);

        if(s > 0){
            ringbuf_consume(&g_ring, s);
            continue;
        }

        if (s < 0 && errno == EINTR) continue;

        fprintf(stderr, "[repl] send to slave failed: %s\n", strerror(errno));
        break;
    }

    if(g_slave.fd == fd){
        nty_close(fd);
        g_slave.fd = -1;

    }

    g_repl_collect = 0;
}


#if 0
// void rdma_handshake(void *arg){

//     int rc = -1;
//     int fd = -1;
//     char *mes = NULL;
//     size_t mes_size = 0;
//     int sync_ret = 0;


//     if (g_slave.fd >= 0) nty_close(g_slave.fd);
//     g_slave.fd = -1;
//     g_slave_sr.fd  = -1;


//     g_slave_sr.len = 0;
//     g_slave_sr.off = 0;
//     fwd_collecting = 0;

//     mes_size = 1024 * 1024;
//     mes = malloc(mes_size);
//     if(!mes){ perror("mes malloc"); goto out; }

//     // fwd_queueing = 1;


//     g_ring.send_off = g_ring.write_off;
//     g_repl_collect = 1;


//     sync_ret = sync_mes_to_rdma(&mes, &mes_size);
//     if(sync_ret < 0){ fprintf(stderr, "sync message error\n"); goto out; }
//     // printf("master snapshot=%d bytes\n", sync_ret);


//     fprintf(stderr, "master snapshot=%d bytes\n", sync_ret);
    
//     free(mes);
//     mes = NULL;



//     fd = nty_socket(AF_INET, SOCK_STREAM, 0);

//     if(fd < 0){
//         perror("nty socket error");
//         goto out;
//     }
//     g_slave.fd = fd;

//     struct sockaddr_in addr;
//     memset(&addr, 0, sizeof(addr));
//     addr.sin_family = AF_INET;
//     addr.sin_port = htons(9998);
//     addr.sin_addr.s_addr = INADDR_ANY;

//     if(inet_pton(AF_INET, cfg.sync_ip, &addr.sin_addr) != 1){
//         fprintf(stderr, "[rdma] cfg.sync_ip not IPv4\n");
//         goto out;
//     }

//     if (nty_connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
//         fprintf(stderr, "[sync] connect %s:9998 fail: %s\n", cfg.sync_ip, strerror(errno));
//         goto out;
//     }  


// #if 0
//     g_rdma.gid_index = pick_rocev2_gid_index(g_rdma.ctx, 1);

//     union ibv_gid local_gid;
//     if(ibv_query_gid(g_rdma.ctx, 1, g_rdma.gid_index, &local_gid) != 0){
//         perror("ibv query gid error"); goto out;
//     }

//     uint32_t local_psn;
//     if(getrandom(&local_psn, sizeof(local_psn), 0) != sizeof(local_psn)){
//         perror("getrandom error"); goto out;
//     }
//     local_psn &= 0xffffff;


//     struct { 
//         uint32_t qpn; 
//         uint32_t psn; 
//         union ibv_gid gid; 
//     }__attribute__((packed)) local_short;
//     local_short.gid = local_gid;
//     local_short.psn = htonl(local_psn);
//     local_short.qpn = htonl(g_rdma.qp->qp_num);

//     if(nty_send(fd, &local_short, sizeof(local_short), 0) != (ssize_t)sizeof(local_short)){
//         perror("nty send error"); goto out;
//     }

//     struct { 
//         uint32_t qpn; 
//         uint32_t psn; 
//         uint32_t rkey;
//         uint64_t vaddr; 
//         union ibv_gid gid; 
//     }__attribute__((packed)) remote_full;

        
//     if(nty_recv(fd, &remote_full, sizeof(remote_full), 0) != (ssize_t)sizeof(remote_full)){
//         perror("rdma nty recv error"); goto out;
//     }

//     g_slave.remote_qpn   = ntohl(remote_full.qpn);
//     g_slave.remote_psn   = ntohl(remote_full.psn);
//     g_slave.remote_rkey  = ntohl(remote_full.rkey);
//     g_slave.remote_vaddr = be64toh(remote_full.vaddr);
//     g_slave.remote_gid   = remote_full.gid;

//     if(rdma_modify(g_rdma.qp, local_psn, g_slave.remote_qpn, &g_slave.remote_gid, g_slave.remote_psn,
//                     g_rdma.gid_index) != 0){
//         fprintf(stderr, "modify fail\n"); goto out;
//     }

//     if(rdma_push_stream((uint32_t)sync_ret) < 0){
//         fprintf(stderr, "[master] push snapshot FAILED\n"); goto out;
//     }

//     // fprintf(stderr, "[RDMA-DONE] t=%llu\n", (unsigned long long)now_us());
// #else //sendfile

//     {
//         uint32_t total_net = htonl((uint32_t)sync_ret);
//         if (nty_send(fd, &total_net, sizeof(total_net), 0) != (ssize_t)sizeof(total_net)) {
//             fprintf(stderr, "[sync] send total fail\n");
//             goto out;
//         }
//     }


//     int pr = sendfile_push_stream((uint32_t)sync_ret);
//     if(pr < 0){
//         fprintf(stderr, "[master] push snapshot FAILED\n"); goto out;
//     }

//     nty_coroutine *sender_co;
//     if(nty_coroutine_create(&sender_co, repl_sender_func, NULL) != 0){
//         fprintf(stderr, "[repl] create sender failed\n");
//         goto out;
//     }
    

//     fprintf(stderr, "[push] size=%d B\n", sync_ret);

// #endif


//     fprintf(stderr, "master snapshot pushed ok\n");


//     rc = 0;
//     // g_slave_sr.fd = fd;
//     // rdma_syncing = 1;
//     // fwd_collecting = 1;

//     // if (g_slave_sr.len > g_slave_sr.off && !g_slave_sr.sending) {
//     //     g_slave_sr.sending = 1;
//     //     g_slave_sr.want = 0;
//     //     nty_coroutine *co;
//     //     nty_coroutine_create(&co, slave_send_func, NULL);
//     // }
    
// out:
//     free(mes);
//     if(rc < 0){
//         g_repl_collect = 0;
//         // rdma_syncing = 0;
//         // fwd_collecting = 0;
//         if (fd >= 0 && g_slave.fd == fd){ 
//             nty_close(fd);
//             g_slave.fd = -1;
//         }
//         // g_slave_sr.fd = -1;
//         // g_slave_sr.len = 0;
//         // g_slave_sr.off = 0;
//     }
//     handshaking = 0;
//     return;
    
// }
#endif


void sendfile_full_sync(void *arg){

    int rc = -1;
    int fd = -1;
    char *mes = NULL;
    size_t mes_size = 0;
    int sync_ret = 0;


    if (g_slave.fd >= 0) nty_close(g_slave.fd);
    g_slave.fd = -1;

    mes_size = 1024 * 1024;
    mes = malloc(mes_size);
    if(!mes){ perror("mes malloc"); goto out; }


#if EBPF
    g_slave_sr.fd  = -1;
    g_slave_sr.len = g_slave_sr.off = 0;
    fwd_collecting = 0;
    fwd_queueing   = 1;          // 沒有這行，master_forward_to_slave 會直接 return
    #else
    g_ring.send_off = g_ring.write_off;
    g_repl_collect  = 1;         // eBPF 模式下設成 1，ringbuf 沒人消費，塞滿後用戶端會被斷線
#endif


    sync_ret = repl_make_snapshot(&mes, &mes_size);
    if(sync_ret < 0){ fprintf(stderr, "sync message error\n"); goto out; }
    
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

    if(inet_pton(AF_INET, cfg.sync_ip, &addr.sin_addr) != 1){
        fprintf(stderr, "[sync] cfg.sync_ip not IPv4\n");
        goto out;
    }

    if (nty_connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[sync] connect %s:9998 fail: %s\n", cfg.sync_ip, strerror(errno));
        goto out;
    }  


    {
        uint32_t total_net = htonl((uint32_t)sync_ret);
        if (nty_send(fd, &total_net, sizeof(total_net), 0) != (ssize_t)sizeof(total_net)) {
            fprintf(stderr, "[sync] send total fail\n");
            goto out;
        }
    }


    int pr = sendfile_push_stream((uint32_t)sync_ret);
    if(pr < 0){
        fprintf(stderr, "[master] push snapshot FAILED\n"); goto out;
    }

#if EBPF
g_slave_sr.fd  = fd;
rdma_syncing   = 1;          // 沒有這行，slave_send_func 一進迴圈就退出
fwd_collecting = 1;
if (g_slave_sr.len > g_slave_sr.off && !g_slave_sr.sending) {
    g_slave_sr.sending = 1;
    g_slave_sr.want = 0;
    nty_coroutine *co;
    nty_coroutine_create(&co, slave_send_func, NULL);
}
#else

    nty_coroutine *sender_co;
    if (nty_coroutine_create(&sender_co, repl_sender_func, NULL) != 0) {
        fprintf(stderr, "[repl] create sender failed\n");
        goto out;
    }

#endif


    fprintf(stderr, "master snapshot pushed size=%d B ok\n", sync_ret);


    rc = 0;
    
out:
    free(mes);
    if(rc < 0){
#if EBPF
    rdma_syncing = fwd_collecting = fwd_queueing = 0;
    g_slave_sr.fd = -1;
    g_slave_sr.len = g_slave_sr.off = 0;
#else

        g_repl_collect = 0;

#endif

        if (fd >= 0 && g_slave.fd == fd){ 
            nty_close(fd);
            g_slave.fd = -1;
        }

    }
    handshaking = 0;
    return;
    
}


#if EBPF // ebpf
static void slave_send_func(void *arg)//send info in ebpf send 
{
    send_resp_t *sr = &g_slave_sr;

    size_t sendbuf_size = 1024 * 1024;

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
            if (chunk > sendbuf_size) chunk = sendbuf_size;

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

    if (data == NULL || len == 0) return;

    /*   1) fwd_collecting=1            sends
     *   2) handshaking=1 && collecting=0 wait
     *   3) neither                       no slave drop
     */

    if (!fwd_queueing && !fwd_collecting) return;
    // if (!is_write_cmd(data, len)) return;



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

    if (fwd_collecting && rdma_syncing && !g_slave_sr.sending) {
        g_slave_sr.sending = 1;
        g_slave_sr.want = 0;
        nty_coroutine *co;
        nty_coroutine_create(&co, slave_send_func, NULL);
        
    }

}
#endif

int kvs_sync(void){//recv SYNC 

    if (handshaking) return -1;
    handshaking = 1;

    nty_coroutine *co = NULL;
    fwd_collecting = 0;
    rdma_syncing = 0;
    fwd_queueing   = 0;
    if(nty_coroutine_create(&co, sendfile_full_sync, NULL) != 0){
        handshaking = 0;
        return -1;
    }
    return 0;
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
    buf = calloc(1, 64 * 1024);
    response = calloc(1, response_cap);
    

    if (!buf || !response) {
        nty_close(cli_fd);

        free(buf); 
        free(response);
        
        buf = NULL; 
        response = NULL;
        return ;
    }

#if EBPF
    send_resp_t *sr = calloc(1, sizeof(send_resp_t));;
    if(!sr) { free(buf); free(response); nty_close(cli_fd); return; }
    sr->fd = cli_fd;
#endif
    while(1){

        int recv_len = nty_recv(cli_fd, buf, 64 * 1024 - 1, 0);

        if (recv_len == 0) { 
#if EBPF
            resp_capture_poll(0); 
#endif
            break; 
        }

        if (recv_len < 0) {
#if EBPF
            resp_capture_poll(0);
#endif
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("nty_recv");
            break;
        }

        int length = kvs_protocol(cli_fd, buf, recv_len, &response, &response_cap);
#if EBPF
        resp_capture_poll(0);
#endif
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

        
        // static uint64_t last_trim_ms = 0;
        // struct timespec ts;
        // clock_gettime(CLOCK_MONOTONIC, &ts);
        // uint64_t cur_ms = (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
        // if (cur_ms - last_trim_ms >= 1000) {        
        //     last_trim_ms = cur_ms;
        //     malloc_trim(0);
        // }
        
    }

out:

#if EBPF
    if (sr) {
        sr->closed = 1;
        if (!sr->sending) {
            free(sr->buf); sr->buf = NULL;
            free(sr);
        }
        sr = NULL;
    }
#endif
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

    int listen_ret = listen(fd, 50);
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
    g_rdma = *g_m_rdma;

#if EBPF
    if (ringbuf_init() < 0) {
        fprintf(stderr, "ringbuf_init failed\n");
        return -1;
    }
#endif

    nty_coroutine_create(&co, accept_func, p);
    
    nty_schedule_run();
    return 0;
}