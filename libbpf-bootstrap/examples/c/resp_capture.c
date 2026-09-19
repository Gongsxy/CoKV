// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2020 Facebook */
#include <stdio.h>
#include <unistd.h>
#include <sys/resource.h>
#include <bpf/libbpf.h>
// #include <bpf/bpf.h>
#include <pthread.h>

#include "resp_capture.skel.h"
#include "resp_capture.h"

static struct resp_capture_bpf *skel = NULL;
struct perf_buffer *pb = NULL;

static int g_map_fd = -1;

void cap_handle_info(void *ctx, int cpu, void *data, __u32 size){
	// printf("recv enter\n");
	// struct bpf_get_resp_info *info = data;
	// printf("mes:%s,len:%d\n", info->resp_info, info->len);

	fprintf(stderr, "enter size=%u\n", size);
    struct bpf_get_resp_info *info = data;
    fprintf(stderr, "len=%d mes=%.*s\n", info->len, info->len, info->resp_info);

}
void cap_lost_info(void *ctx, int cpu, __u64 cnt){

	printf("lost enter\n");

}

// static void *perf_buffer_poll(void *arg)
// {	
// 	struct perf_buffer *pb = arg;

// 	while(1){

// 		int n = perf_buffer__poll(pb, 1000);
// 		if (n < 0) {
//             fprintf(stderr, "[resp_capture] perf_buffer__poll failed: %d\n", n);
//             break;
//         }
// 	}
// 	return NULL;
// }

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	return 0;
	// return vfprintf(stderr, format, args);
}

int resp_capture(void)
{
	libbpf_set_print(libbpf_print_fn);

	int err;

	/* Set up libbpf errors and debug info callback */
	libbpf_set_print(libbpf_print_fn);

	/* Open BPF application */
	skel = resp_capture_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}


	/* Attach tracepoint handler */
	err = resp_capture_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}

	g_map_fd = bpf_map__fd(skel->maps.resp_cap);



	return 0;
cleanup:
	if (pb) { perf_buffer__free(pb); pb = NULL; }
    if (skel) { resp_capture_bpf__destroy(skel); skel = NULL; }
    return err ? err : -1; 
}

void resp_capture_reset(void)
{
    static struct bpf_get_resp_info empty;
    __u32 zero = 0;

    if (g_map_fd < 0) return;
    bpf_map__update_elem(skel->maps.resp_cap, &zero, sizeof(zero), &empty, sizeof(empty), BPF_ANY);
}

struct bpf_get_resp_info *resp_capture_read(void){

	static struct bpf_get_resp_info tmp;
	__u32 zero = 0;
	
	if (g_map_fd < 0) return NULL;

	bpf_map__lookup_elem(skel->maps.resp_cap, &zero, sizeof(zero), &tmp, sizeof(tmp), 0);
	if(tmp.len == 0) return NULL;

    return &tmp;
}
