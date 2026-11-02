
#include "rdma.h"
#include "stdio.h"
#include "stdlib.h"
#include "../NtyCo/core/nty_coroutine.h"
#include "load_conf.h"

#define RDMA_BUF_SIZE   1024 * 1024 * 10

#define ADD_FILENAME    "config/add.txt"
#define FULL_FILENAME   "config/full.txt"


int pick_rocev2_gid_index(struct ibv_context *ctx, int port) {
    struct ibv_port_attr pattr;
    if (ibv_query_port(ctx, port, &pattr) != 0) return -1;
    for (int i = 0; i < pattr.gid_tbl_len; i++) {
        union ibv_gid gid;
        if (ibv_query_gid(ctx, port, i, &gid) == 0 && gid.raw[10] == 0xff && gid.raw[11] == 0xff) {
            return i;
        }
    }
    return -1;
}

int rdma_init(__g_rdma *g_rdma){
    int dev_num = 0;
    struct ibv_device **dev = ibv_get_device_list(&dev_num);
    if(dev == NULL || dev_num == 0) {
        perror("get devices list error");
        return -1;
    }

    g_rdma->ctx = ibv_open_device(dev[0]);
    if(g_rdma->ctx == NULL){
        perror("open devices error");
        return -1;
    }
    ibv_free_device_list(dev);

    g_rdma->gid_index = pick_rocev2_gid_index(g_rdma->ctx, 1);

    g_rdma->pd = ibv_alloc_pd(g_rdma->ctx);
    if(g_rdma->pd == NULL){
        perror("pd alloc error");
        goto err_ctx;
    }

    g_rdma->cq = ibv_create_cq(g_rdma->ctx, 512, NULL, NULL, 0);
    if(g_rdma->cq == NULL){
        perror("cq create error");
        goto err_pd;
    }

    struct ibv_qp_init_attr attr = {0};
    attr.cap.max_recv_sge = 1;
    attr.cap.max_recv_wr = 128;
    attr.cap.max_send_sge = 1;
    attr.cap.max_send_wr = 128;
    attr.qp_type = IBV_QPT_RC;
    attr.recv_cq = g_rdma->cq;
    attr.send_cq = g_rdma->cq;

    g_rdma->qp = ibv_create_qp(g_rdma->pd, &attr);
    if(g_rdma->qp == NULL){
        perror("qp create error");
        goto err_cq;
    }

    g_rdma->buf = calloc(1, RDMA_BUF_SIZE);
    g_rdma->buf_size = RDMA_BUF_SIZE;
    if(g_rdma->buf == NULL){
        perror("buf calloc error");
        goto err_qp;
    }

    g_rdma->mr = ibv_reg_mr(g_rdma->pd, g_rdma->buf, g_rdma->buf_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);
    if(g_rdma->mr == NULL){
        perror("me reg error");
        goto err_buf;
    }

    return 0;

err_buf:
    free(g_rdma->buf);
err_qp:
    ibv_destroy_qp(g_rdma->qp);
err_cq:
    ibv_destroy_cq(g_rdma->cq);
err_pd:
    ibv_dealloc_pd(g_rdma->pd);
err_ctx:
    ibv_close_device(g_rdma->ctx);

    return -1;
}


/* ========== QP ×´Ì¬»ú×ª»» ========== */
static int modify_qp_to_init(struct ibv_qp *qp) {
    struct ibv_qp_attr attr = {0};
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = 1;
    attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
    int ret = ibv_modify_qp(qp, &attr,IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
    if (ret != 0) {
        fprintf(stderr, "[modify_qp_to_init] failed ret=%d\n", ret);
    }
    return ret;
}

static int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn,const union ibv_gid *remote_gid, uint32_t remote_psn, int gid_index) {
    struct ibv_qp_attr attr = {0};
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_1024;
    attr.dest_qp_num = remote_qpn;
    attr.rq_psn = remote_psn;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;

    attr.ah_attr.is_global = 1;
    attr.ah_attr.grh.dgid = *remote_gid;
    attr.ah_attr.grh.sgid_index = gid_index;
    attr.ah_attr.grh.hop_limit = 255;
    attr.ah_attr.grh.traffic_class = 0;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = 1;

    int ret = ibv_modify_qp(qp, &attr,IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
    if (ret != 0) {
        fprintf(stderr, "[modify_qp_to_rts] failed ret=%d\n", ret);
    }
    return ret;
}

static int modify_qp_to_rts(struct ibv_qp *qp, uint32_t local_psn) {
    struct ibv_qp_attr attr = {0};
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = local_psn;
    attr.timeout = 14;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.max_rd_atomic = 1;
    int ret =  ibv_modify_qp(qp, &attr,IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC);
    if (ret != 0) {
        fprintf(stderr, "[modify_qp_to_rts] failed ret=%d\n", ret);
    }
    return ret;
}

int rdma_modify(struct ibv_qp *qp, uint32_t local_psn, uint32_t remote_qpn, union ibv_gid *remote_gid, uint32_t remote_psn, int gid_index){

    modify_qp_to_init(qp);
    modify_qp_to_rtr(qp, remote_qpn, remote_gid, remote_psn, gid_index);
    modify_qp_to_rts(qp, local_psn);

    return 0;
}



int rdma_notify_slave(__g_slave *g_slave, uint32_t length){
    struct {
        uint32_t length;
    }__attribute__((packed)) notify;
    notify.length = htonl(length);

    ssize_t ret = nty_send(g_slave->fd, &notify, sizeof(notify), 0);
    return (ret == sizeof(notify)) ? 0 : -1;
}


