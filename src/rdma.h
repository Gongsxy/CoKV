#ifndef __RDMA_H__
#define __RDMA_H__

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

typedef struct {
    struct ibv_context *ctx;
    struct ibv_cq *cq; 
    struct ibv_qp *qp;
    struct ibv_pd *pd;
    struct ibv_mr *mr;
    char *buf;
    size_t buf_size;
    int gid_index;
} __g_rdma;
extern __g_rdma g_rdma;
extern __g_rdma g_rdma_slave;

typedef struct {
    int fd;
    uint32_t remote_qpn;
    uint32_t remote_psn;
    union ibv_gid remote_gid;
    uint64_t remote_vaddr;
    uint32_t remote_rkey;
    int gid_index;
} __g_slave;
extern __g_slave g_slave;
extern __g_slave g_s_slave;

int rdma_init(__g_rdma *g_rdam);
int rdma_modify(struct ibv_qp *qp, uint32_t local_psn, uint32_t remote_qpn, union ibv_gid *remote_gid, uint32_t remote_psn, int gid_index);
int rdma_notify_slave(__g_slave *g_slave, uint32_t length);
int pick_rocev2_gid_index(struct ibv_context *ctx, int port);

#endif
