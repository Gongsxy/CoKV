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
} __g_rdma;
extern __g_rdma g_rdma;

typedef struct {
    int fd;
    uint32_t remote_qpn;
    uint32_t remote_psn;
    union ibv_gid remote_gid;
    uint64_t remote_vaddr;
    uint32_t remote_rkey;
} __g_slave;
extern __g_slave g_slave;

int rdma_init(void);
void rdma_modify(struct ibv_qp *qp, uint32_t local_psn, uint32_t remote_qpn, union ibv_gid *remote_gid, uint32_t remote_psn);
int rdma_notify_slave(uint32_t length);

#endif
