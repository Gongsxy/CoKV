





#include "../NtyCo/core/nty_coroutine.h"
#include <netinet/in.h>
#include <sys/random.h>

#include "kvstore.h"
#include "rdma.h"
#include "load_conf.h"


#define ADD_FILENAME    "config/add.txt"
#define FULL_FILENAME   "config/full.txt"

static Config cfg = {0};

__g_rdma g_rdma = {0};
__g_slave g_slave = {0};

extern int kvs_protocol(char *msg, int length, char *response);

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

size_t sync_mes_to_rdma(void){
printf("[sync] enter\n");

    int fd = load_save_mes();
    if(fd <= 0){
        printf("don't choose load message that has saved\n");
        return 0;
    }

    off_t file_sz = lseek(fd, 0, SEEK_END);
printf("[sync] file_sz=%ld buf=%p buf_size=%zu\n",
                (long)file_sz, (void*)g_rdma.buf, g_rdma.buf_size);
    if(file_sz == (off_t)-1){
        perror("lseek end");
        close(fd);
        return -1;
    }

    lseek(fd, 0, SEEK_SET);
    if (file_sz <= 0){
        printf("snapshot empty, nothing to sync\n");
        return 0;
    }
    if ((size_t)file_sz > g_rdma.buf_size){
        fprintf(stderr,"快照大小%ld超过rdma buf上限%zu\n", (long)file_sz, g_rdma.buf_size);
        return -1;
    }

    // 从磁盘读到master的rdma注册内存g_rdma.buf
    size_t total_read = 0;
    off_t off = 0;
    while(total_read < (size_t)file_sz){
        ssize_t ret = pread(fd, g_rdma.buf + total_read, (size_t)file_sz - total_read, off);
        if(ret < 0){close(fd); perror("pread"); return -1;}
        if(ret == 0) break; // eof
        total_read += ret;
        
    }
    if (total_read != file_sz){
        perror("read snapshot fail");
        return -1;
    }

    close(fd);
printf("[sync] read done %zu\n", total_read);
    return total_read;
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

    printf("[master] ctx=%p qp=%p mr=%p gid_index=%d\n",
                (void*)g_rdma.ctx, (void*)g_rdma.qp, (void*)g_rdma.mr, g_rdma.gid_index);

    int fd = nty_accept(listen_fd, (struct sockaddr *)&addr, &len);
printf("[master] slave connected\n");
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
printf("[master] local_info sent\n");
    if(rdma_modify(g_rdma.qp, local_psn, g_slave.remote_qpn, &g_slave.remote_gid, g_slave.remote_psn, g_rdma.gid_index) != 0){ fprintf(stderr, "modify fail\n"); return ; }
     printf("[master] modify ok\n");

    int sync_ret = sync_mes_to_rdma();

    if(sync_ret < 0){ perror("sync message error"); return ; }
    printf("[master] snapshot %d bytes, posting write\n", sync_ret);

    if(sync_ret > 0 && rdma_send_mes_to_slave((uint32_t)sync_ret) < 0){
        fprintf(stderr, "rdma write fail\n"); 
    }
    printf("[master] write completed\n");

    if(rdma_notify_slave(&g_slave, (uint32_t)sync_ret) < 0){
        fprintf(stderr, "notify fail\n"); return ;
    }
    printf("[master] notify sent\n");

    if(sync_ret < 0){
        perror("sync mes to slave error");
        return ;
    }

    char ack[4];
    nty_recv(fd, ack, 4, 0);
printf("[master] sync done, %d bytes\n", sync_ret);
    nty_close(fd);
    nty_close(listen_fd);

    return ;
}


void read_func(void *arg){
    int cli_fd = *(int *)arg;
    free(arg);

    if(cli_fd < 0){
        perror("read arg error");
        return ;
    }
    
    char *buf = calloc(1, 1024 * 1024 * 5);
    char *response = calloc(1, 1024 * 1024 * 1);
    if (!buf || !response) {
        free(buf);
        free(response);
        nty_close(cli_fd);
        return;
    }

    while(1){
        memset(buf, 0, 1024 * 1024 * 5);
        memset(response, 0, 1024 * 1024 * 1);
printf("[RECV_CB] fd=%d\n", cli_fd);
        int recv_len = nty_recv(cli_fd, buf, 1024 * 1024 * 5, 0);
        if (recv_len <= 0) {
            break;
        }

        int length = kvs_protocol(buf, recv_len, response);
        if(length > 0){
            int ret = nty_send(cli_fd, response, length, 0);
printf("SAVE send ret=%d\n", ret);
        }
        
    }
    nty_close(cli_fd);
    free(buf);    
    free(response);
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
            continue;
        }

        int *arg = calloc(1, sizeof(int));
        *arg = client_fd;
        nty_coroutine *read_co;
        nty_coroutine_create(&read_co, read_func, arg);
    }
}


int ntyco_start(Config *server_cfg, __g_rdma *g_m_rdma){
    cfg = *server_cfg;
    nty_coroutine *co = NULL;
    int *p = calloc(1, sizeof(int));
    *p = cfg.port;

    g_rdma = *g_m_rdma;

    nty_coroutine_create(&co, accept_func, p);
    nty_coroutine_create(&co, rdma_handshake, p);
    nty_schedule_run();
    return 0;
}