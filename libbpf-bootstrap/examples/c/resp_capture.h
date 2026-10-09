
#ifndef __RESP_CAPTURE_H__
#define __RESP_CAPTURE_H__

#define MAX_SIZE (64 * 1024)

typedef struct bpf_get_resp_info{
    __u32 len;
    char resp_info[MAX_SIZE];
}resp_info;

// int resp_capture(void);
// void resp_capture_reset(void);
// struct bpf_get_resp_info *resp_capture_read(void);

int  ringbuf_init(void);
int  resp_capture_poll(int max_records);
#endif