// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2020 Facebook */
#include "vmlinux.h"
// #include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>


#include "resp_capture.h"


char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 16 * 1024 * 1024);

} resp_ringbuf  SEC(".maps");


SEC("uprobe//home/samba/kvstore/kvstore:kvs_protocol")
#if 0
int BPF_UPROBE(handle_kvs_protocol, void *a, char *msg, int length, void *b, void *c)
{	

	__u32 zero = 0;
	struct bpf_get_resp_info *info = bpf_map_lookup_elem(&resp_cap, &zero);
	if (!info) return 0;
	
	__u32 copy_len = (__u32)length;
    copy_len &= 0xFFFF;                     
    if (copy_len == 0) return 0;


	__u32 off = info->len;
	off &= 0xFFFF;

	if (off + copy_len > MAX_SIZE) {
        info->drop++;
        return 0;
    }

	// info->len = copy_len;
	long ret = bpf_probe_read_user(info->resp_info, copy_len, msg);
	if (ret < 0) {
        info->drop++;
        return 0;
    }

	info->len = off + copy_len;

	// bpf_perf_event_output(ctx, &channel, BPF_F_CURRENT_CPU, info, sizeof(*info));

    return 0;
}
#else
// int kvs_protocol(int fd, char *msg, int length, char **response, size_t *response_size)
// int kvs_split_token(char *msg, char *tokens[], int length, size_t *key_len, size_t *value_len, int *resp_total)
int BPF_UPROBE(handle_kvs_protocol, void *a, char *msg, int length, void *b, void *c)
{	
	if (length <= 0 || length > MAX_SIZE) return 0;
	
	__u32 copy_len = (__u32)length;
    copy_len &= 0xFFFF;
    if (copy_len == 0) return 0;


	char head[12];
	if (copy_len < 12) return 0;

    long r = bpf_probe_read_user(head, sizeof(head), msg);
    if (r < 0) return 0;

	resp_info *e = bpf_ringbuf_reserve(&resp_ringbuf, sizeof(resp_info), 0);
	if (e == NULL) {
		bpf_printk("KVS DROP reserve fail len=%u\n", copy_len);
		return 0;
	}

	e->len = (__u32)copy_len;

	long ret = bpf_probe_read_user(e->resp_info, copy_len, msg);
	if(ret < 0){bpf_ringbuf_discard(e, 0); return 0;}

	bpf_ringbuf_submit(e, 0);

#endif
	return 0;
}
