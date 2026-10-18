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
	__uint(max_entries, 32 * 1024 * 1024);

} resp_ringbuf  SEC(".maps");


SEC("uprobe//home/topeet/Desktop/test/kvstore/kvstore:kvs_protocol")

// int kvs_protocol(int fd, char *msg, int length, char **response, size_t *response_size)
// int kvs_split_token(char *msg, char *tokens[], int length, size_t *key_len, size_t *value_len, int *resp_total)
int BPF_UPROBE(handle_kvs_protocol, void *a, char *msg, int length, void *b, void *c)
{	
	if (length <= 0 || length > MAX_SIZE) return 0;
	
	__u32 copy_len = (__u32)length;
	copy_len &= MAX_SIZE - 1; 
	if (copy_len == 0) copy_len = MAX_SIZE; 
    __u32 rec_len  = sizeof(__u32) + copy_len;
	
	
	void *e = bpf_ringbuf_reserve(&resp_ringbuf, sizeof(resp_info), 0);
	if (e == NULL) {
		bpf_printk("KVS DROP reserve fail len=%u\n", copy_len);
		return 0;
	}

	*(__u32 *)e = copy_len;
	long ret = bpf_probe_read_user(e + sizeof(__u32), copy_len, msg);
	if(ret < 0){bpf_ringbuf_discard(e, 0); return 0;}

	bpf_ringbuf_submit(e, 0);


	return 0;
}
