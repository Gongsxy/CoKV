// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2020 Facebook */
#include <stdio.h>
#include <unistd.h>
#include <sys/resource.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <pthread.h>

#include ".output/resp_capture.skel.h"
#include "resp_capture.h"

void master_forward_to_slave(const char *data, unsigned len);



static struct resp_capture_bpf *skel = NULL;
struct perf_buffer *pb = NULL;

int cap_handle_info(void *ctx, void *data, size_t size){

	// fprintf(stderr, "[ringbuf] callback size=%zu\n", size);


	static unsigned long long bad_size = 0;
    if (data == NULL || size < sizeof(resp_info)) {
        if (++bad_size <= 5)
            fprintf(stderr, "[rb] BAD size=%zu expect=%zu\n", size, sizeof(resp_info));
        return 0;
    }

    resp_info *e = data;

    if (e->len == 0 || e->len > MAX_SIZE) return 0;




	// static unsigned long long rb_cnt = 0, rb_bytes = 0;
    // rb_cnt++; rb_bytes += e->len;
    // if (rb_cnt <= 20 || (rb_cnt % 200) == 0)
    //     fprintf(stderr, "[rb] #%llu  len=%u bytes=%llu\n",
    //             rb_cnt, e->len, rb_bytes);



    master_forward_to_slave(e->resp_info, e->len);

    return 0;
}
void cap_lost_info(void *ctx, int cpu, __u64 cnt){

	printf("lost enter\n");

}


static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	// return 0;
	return vfprintf(stderr, format, args);
}

#if 0
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
    int ret = bpf_map_update_elem(g_map_fd, &zero, &empty, BPF_ANY);

    if (ret < 0) {
        perror("bpf_map_update_elem");
    }
}

struct bpf_get_resp_info *resp_capture_read(void){

	static struct bpf_get_resp_info tmp;
	__u32 zero = 0;
	
	if (g_map_fd < 0) return NULL;

	int ret = bpf_map_lookup_elem(g_map_fd, &zero, &tmp);
    if (ret < 0) {
        perror("bpf_map_lookup_elem");
        return NULL;
    }

	if(tmp.len == 0) return NULL;

    return &tmp;
}
#endif

static struct ring_buffer *g_rb = NULL;

int ringbuf_init(void){
	
	libbpf_set_print(libbpf_print_fn);

	skel = resp_capture_bpf__open_and_load();
	if (!skel) {
        fprintf(stderr, "open_and_load failed\n");
        return -1;
    }

	int map_fd = bpf_map__fd(skel->maps.resp_ringbuf);
	if (map_fd < 0) {
		fprintf(stderr, "invalid resp_ringbuf fd\n");
		goto fail;
	}

	int err = resp_capture_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "BPF attach failed: %d\n", err);
        goto fail;
    }

	struct ring_buffer_opts opts = {
        .sz = 64 * 1024 * 1024,
    };

	g_rb = ring_buffer__new(map_fd, cap_handle_info, &opts, NULL);
	if (g_rb == NULL) {
        fprintf(stderr, "ring_buffer__new failed\n");
        goto fail;
    }

	fprintf(stderr, "[size] userspace sizeof(resp_info)=%zu\n", sizeof(resp_info));

	return 0;
fail:
    if (g_rb) {
        ring_buffer__free(g_rb);
        g_rb = NULL;
    }

    resp_capture_bpf__destroy(skel);
    skel = NULL;
    return -1;
}

int resp_capture_poll(int max_records){
	if (!g_rb) return 0;

	int total = 0;
	while(1){
		int n = ring_buffer__poll(g_rb, 0);
		if (n < 0) {
            if (n == -EINTR) continue;
            fprintf(stderr, "[rb] poll error %d\n", n);
            return n;
        }
		if (n == 0) break;
		total += n;
		if (total >= 8192) break;
	}

	return total;
}
