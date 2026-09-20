
#ifndef __RESP_CAPTURE_H__
#define __RESP_CAPTURE_H__

#define TASK_COMM_LEN   16

struct bpf_get_resp_info{
    char resp_info[64 * 1024];
    int len;
    __u32 drop;
};

int resp_capture(void);
void resp_capture_reset(void);
struct bpf_get_resp_info *resp_capture_read(void);

#endif