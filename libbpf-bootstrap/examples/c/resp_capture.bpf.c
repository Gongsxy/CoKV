// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2020 Facebook */
#include "vmlinux.h"
// #include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>


#include "resp_capture.h"

#define MAX_SIZE  64 * 1024


char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} channel SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bpf_get_resp_info);
} resp_cap SEC(".maps");

SEC("uprobe//home/samba/kvstore/kvstore:kvs_protocol")


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
