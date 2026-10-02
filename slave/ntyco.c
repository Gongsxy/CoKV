#include "../NtyCo/core/nty_coroutine.h"
#include <netinet/in.h>

#include <infiniband/verbs.h>
#include "load_conf.h"
#include "../src/rdma.h"
#include <sys/random.h>
#include <arpa/inet.h>
#include <endian.h> 

#define G_RMDA_BUF_SIZE 1024 * 1024 * 5

extern int full_save_to_resp(char *msg, uint32_t length, char *response);
extern int kvs_protocol(char *msg, int length, char *response);

Config cfg;

__g_rdma g_rdma_slave = {0};
__g_slave g_s_slave = {0};

uint32_t transfer_len = 0;

char *res = NULL;

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

    if (nty_connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("nty_connect"); nty_close(fd); return;
    }

    g_rdma_slave.gid_index = pick_rocev2_gid_index(g_rdma_slave.ctx, 1);

    union ibv_gid local_gid;
    if(ibv_query_gid(g_rdma_slave.ctx, 1, g_rdma_slave.gid_index, &local_gid) != 0){
        perror("ibv query gid error");
        return ;
    }


    uint32_t local_psn;
    ssize_t r_ret = getrandom(&local_psn, sizeof(local_psn), 0);
    local_psn &= 0xffffff;

    if (r_ret != sizeof(local_psn)) {
        perror("getrandom error"); 
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
        return ;
    }

    g_s_slave.remote_gid = remote_info.gid;
    g_s_slave.remote_psn = ntohl(remote_info.psn);
    g_s_slave.remote_qpn = ntohl(remote_info.qpn);

    rdma_modify(g_rdma_slave.qp, local_psn, g_s_slave.remote_qpn, &g_s_slave.remote_gid, g_s_slave.remote_psn, g_rdma_slave.gid_index);


    uint32_t recv_len_net;
    ssize_t mes_rv = nty_recv(fd, &recv_len_net, sizeof(recv_len_net),0);
    if(mes_rv != sizeof(recv_len_net)){
        perror("recv transfer length error");
        nty_close(fd);
        return ;
    }
    transfer_len = ntohl(recv_len_net);


    if(transfer_len != 0){
        char *resp_switch = calloc(1, 1024 * 1024 * 5);
        char *resp_out    = calloc(1, 5 * 1024 * 1024);
        int length = full_save_to_resp(g_rdma_slave.buf, transfer_len, resp_switch);
        if(length > 0){
            if(res == NULL){
                res = calloc(1, 1024 * 1024);
            }
            int recv_length = kvs_protocol(resp_switch, length, resp_out);
        }else{
            kvs_protocol(g_rdma_slave.buf, transfer_len, resp_switch);
        }
    }
    uint32_t ack = htonl(1);
    nty_send(fd, &ack, sizeof(ack), 0);
    // close(fd);
    // nty_close(fd);
    char *cbuf = calloc(1, 1024 * 1024 * 5);
    char *cres = calloc(1, 1024 * 1024);
    if (!cbuf || !cres) {
        free(cbuf); free(cres);
        nty_close(fd);
        return ;
    }


    while (1) {
        memset(cbuf, 0, 1024 * 1024 * 5);
        memset(cres, 0, 1024 * 1024);

        int n = nty_recv(fd, cbuf, 1024 * 1024 * 5, 0);  
        if (n <= 0) {
            printf("[slave] master gone, n=%d\n", n);
            break;
        }

        int rlen = kvs_protocol(cbuf, n, cres);
    }

    free(cbuf);
    free(cres);

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
    char *resp = calloc(1, 1024 * 1024);
    if (!buf || !resp) {
        free(buf);
        free(resp);
        nty_close(cli_fd);
        return;
    }

    while(1){
        memset(buf, 0, 1024 * 1024 * 5);
        memset(resp, 0, 1024 * 1024 * 1);

        int recv_len = nty_recv(cli_fd, buf, 1024 * 1024 * 5, 0);
        if (recv_len <= 0) {
            break;
        }
        int length = 0;
        length = kvs_protocol(buf, recv_len, resp);
        if (length > 0) nty_send(cli_fd, resp, length, 0);
    }

    nty_close(cli_fd);
    free(buf);    
    free(resp);
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

    srand(time(NULL));

    cfg = *server_cfg;

    nty_coroutine *co = NULL;
    int *p = calloc(1, sizeof(int));
    *p = server_cfg->port;

    g_rdma_slave = *g_m_rdma;


    nty_coroutine_create(&co, accept_func, p);
    nty_coroutine_create(&co, rdma_handshake, p);
    nty_schedule_run();

    return 0;
}
