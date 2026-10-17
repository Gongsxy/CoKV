

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <ctype.h>

#include "../src/kvstore.h"
#include "../src/load_conf.h"


#define TESTCASE_CMD_TOTAL 100000   /* 5w + 5w */
#define TESTCASE_HALF      50000
#define MAX_MSG_LENGTH		1024
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)


const char *cmds[] = {
    "SET","MOD","RSET","RMOD","HSET","HMOD","SSET","SMOD"
};

const char *test_cmd[] = {
	"SET","GET","EXIST","MOD","GET","DEL","EXIST","MOD","GET","EXIST"
};

char *test_key_last = "last_set";
char *test_key = "Teacher";
char *test_value = "King";
char *test_value_mod = "Htj";

int send_msg(int connfd, char *msg, int length) {

	int res = send(connfd, msg, length, 0);
	if (res < 0) {
		perror("send");
		exit(1);
	}
	return res;
}

int recv_msg(int connfd, char *msg, int length) {

	int res = recv(connfd, msg, length, 0);
	if (res < 0) {
		perror("recv");
		exit(1);
	}
	return res;

}

int is_number(char *msg){//1 true 0 false 
	if(msg == NULL) return 0;
	int i = 0;
	int not_digit = 0;
	for(i = 0; msg[i] != '\0'; i++){
		if(!isdigit(msg[i])){
			return 0;
		}else{
			not_digit = 1;
			break;
		}
	}
	if(not_digit) return 0;
	return 1;
}

int send_num = 0;
void testcase(int connfd)
{
	int length = 0;
	int j = 0;
	int total_num = 0;

	char *send_mes = malloc(4096000);

	size_t cap = 4096000;
	size_t len = 0;

	for (total_num = 0; total_num < 50000; total_num++) {

		j = total_num % 10;
		
		char msg[4096] = {0};
		char tmp[4096] = {0};
		char tcopy[4096] = {0};

		char cmd_tmp[128] = {0};
		int idx = 0;

		// get cmd
		strcpy(tmp, "SET");
		tmp[strcspn(tmp, "\n")] = '\0';
		int c_len = strlen(tmp);
		memcpy(cmd_tmp, tmp, c_len);

		int cmd_size = snprintf(NULL, 0, "$%d\r\n%s\r\n", c_len, tmp);
		char *cmd = malloc(cmd_size + 1);
		sprintf(cmd, "$%d\r\n%s\r\n", c_len, tmp);
		memcpy(msg, cmd, strlen(cmd));
		idx++;
		memset(tmp, 0, 4096);

		// get key
		sprintf(tmp, "%s%d", test_key, total_num);
		strcat(tcopy, tmp);

		idx++;
		int k_len = strlen(tcopy);
		int key_size = snprintf(NULL, 0, "$%d\r\n%s\r\n", k_len, tcopy);
		char *key = malloc(key_size + 1);
		if (k_len == 0) {
			snprintf(key, key_size + 1, "$0\r\n\r\n");
		} else {
			sprintf(key, "$%d\r\n%s\r\n", k_len, tcopy);
		}
		strcat(msg, key);
		memset(tcopy, 0, 4096);

		// match cmd that need value
		int i = 0;
		int match = 0;
		for (i = 0; i < 8; i++) {
			if (strcmp(cmd_tmp, cmds[i]) == 0) {
				match = 1;
				break;
			}
		}

		if (match) {
			if (j == 3 || j == 7) {
				sprintf(tmp, "%s%d", test_value_mod, total_num);
			} else {
				sprintf(tmp, "%s%d", test_value, total_num);
			}

			strcat(tcopy, tmp);

			idx++;
			int v_len = strlen(tcopy);
			int value_size = snprintf(NULL, 0, "$%d\r\n%s\r\n", v_len, tcopy);
			char *value = malloc(value_size + 1);
			if (v_len == 0) {
				snprintf(value, value_size + 1, "$0\r\n\r\n");
			} else {
				sprintf(value, "$%d\r\n%s\r\n", v_len, tcopy);
			}
			strcat(msg, value);
			memset(tcopy, 0, 4096);
			free(value);
		}

		char final_send_mes[4096] = {0};

		// send request
		sprintf(final_send_mes, "*%d\r\n", idx);
		strcat(final_send_mes, msg);

		size_t need = strlen(final_send_mes);
		if (len + need + 1 > cap) {
			cap = cap * 2 + need;
			send_mes = realloc(send_mes, cap);
		}
		memcpy(send_mes + len, final_send_mes, need);
		len += need;
	}

	{
		int      n  = 0;
		char     rbuf[65536];
		struct timeval t0, t1;
		struct timeval rto = { 5, 0 };

		setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, &rto, sizeof(rto));

		length = (int)len;
		send_num += 50000;

		gettimeofday(&t0, NULL);
		send_msg(connfd, send_mes, length);

		while (n < 50000) {
			int     k;
			ssize_t r = recv_msg(connfd, rbuf, sizeof rbuf);
			if (r <= 0) break;
			for (k = 0; k < r; k++)
				if (rbuf[k] == '\n') n++;
		}
		gettimeofday(&t1, NULL);

		long cost_ms = TIME_SUB_MS(t1, t0);
		printf("cmd number: %d\nsuccessed: %d\ntime: %ld ms\nQPS: %.2f\n",
		       50000, n, cost_ms, cost_ms > 0 ? 50000 * 1000.0 / cost_ms : 0.0);
		fflush(stdout);
	}

	getchar();

	memset(send_mes, 0, len);
	len = 0;


	for (total_num = 50000; total_num < 100000; total_num++) {

		j = total_num % 10;

		char msg[4096] = {0};
		char tmp[4096] = {0};
		char tcopy[4096] = {0};

		char cmd_tmp[128] = {0};
		int idx = 0;

		// get cmd
		strcpy(tmp, "SET");
		tmp[strcspn(tmp, "\n")] = '\0';
		int c_len = strlen(tmp);
		memcpy(cmd_tmp, tmp, c_len);

		int cmd_size = snprintf(NULL, 0, "$%d\r\n%s\r\n", c_len, tmp);
		char *cmd = malloc(cmd_size + 1);
		sprintf(cmd, "$%d\r\n%s\r\n", c_len, tmp);
		memcpy(msg, cmd, strlen(cmd));
		idx++;
		memset(tmp, 0, 4096);

		// get key
		sprintf(tmp, "%s%d", test_key, total_num);
		strcat(tcopy, tmp);

		idx++;
		int k_len = strlen(tcopy);
		int key_size = snprintf(NULL, 0, "$%d\r\n%s\r\n", k_len, tcopy);
		char *key = malloc(key_size + 1);
		if (k_len == 0) {
			snprintf(key, key_size + 1, "$0\r\n\r\n");
		} else {
			sprintf(key, "$%d\r\n%s\r\n", k_len, tcopy);
		}
		strcat(msg, key);
		memset(tcopy, 0, 4096);

		// match cmd that need value
		int i = 0;
		int match = 0;
		for (i = 0; i < 8; i++) {
			if (strcmp(cmd_tmp, cmds[i]) == 0) {
				match = 1;
				break;
			}
		}

		if (match) {
			if (j == 3 || j == 7) {
				sprintf(tmp, "%s%d", test_value_mod, total_num);
			} else {
				sprintf(tmp, "%s%d", test_value, total_num);
			}

			strcat(tcopy, tmp);

			idx++;
			int v_len = strlen(tcopy);
			int value_size = snprintf(NULL, 0, "$%d\r\n%s\r\n", v_len, tcopy);
			char *value = malloc(value_size + 1);
			if (v_len == 0) {
				snprintf(value, value_size + 1, "$0\r\n\r\n");
			} else {
				sprintf(value, "$%d\r\n%s\r\n", v_len, tcopy);
			}
			strcat(msg, value);
			memset(tcopy, 0, 4096);
			free(value);
		}

		char final_send_mes[4096] = {0};

		// send request
		sprintf(final_send_mes, "*%d\r\n", idx);
		strcat(final_send_mes, msg);

		size_t need = strlen(final_send_mes);
		if (len + need + 1 > cap) {
			cap = cap * 2 + need;
			send_mes = realloc(send_mes, cap);
		}
		memcpy(send_mes + len, final_send_mes, need);
		len += need;
	}

	/* ---------------- 第二批：发送 + 收满 5w 条回复 + 打印 ---------------- */
	{
		int      n  = 0;
		char     rbuf[65536];
		struct timeval t0, t1;
		struct timeval rto = { 5, 0 };

		setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, &rto, sizeof(rto));

		length = (int)len;
		send_num += 50000;

		gettimeofday(&t0, NULL);
		send_msg(connfd, send_mes, length);

		while (n < 50000) {
			int     k;
			ssize_t r = recv_msg(connfd, rbuf, sizeof rbuf);
			if (r <= 0) break;
			for (k = 0; k < r; k++)
				if (rbuf[k] == '\n') n++;
		}
		gettimeofday(&t1, NULL);

		long cost_ms = TIME_SUB_MS(t1, t0);
		printf("cmd number: %d\nsuccessed: %d\ntime: %ld ms\nQPS: %.2f\n",
		       50000, n, cost_ms, cost_ms > 0 ? 50000 * 1000.0 / cost_ms : 0.0);
		fflush(stdout);
	}

	free(send_mes);
}


void testcase_get(int connfd){
	int length = 0;
	int j = 0;
	int total_num = 0;
	char *send_mes = malloc(4096000);

	size_t cap = 4096000;
	size_t len = 0;
	for(total_num = 0; total_num < 100000; total_num ++){

		if(j == 10){
			j = 0;
		}
		char msg[4096] = {0};
		char tmp[4096] = {0};
		char tcopy[4096] = {0};

		char cmd_tmp[128] = {0};
		int idx = 0;

		// get cmd
		strcpy(tmp, "GET");                                  // ← 改：SET 换成 GET
		tmp[strcspn(tmp, "\n")] = '\0';
		int c_len = strlen(tmp);
		memcpy(cmd_tmp, tmp, c_len);

		int cmd_size = snprintf(NULL,0,"$%d\r\n%s\r\n",c_len,tmp);
		char *cmd = malloc(cmd_size + 1);
		sprintf(cmd, "$%d\r\n%s\r\n",c_len, tmp);
		memcpy(msg, cmd, strlen(cmd));
		msg[strlen(cmd)] = '\0';
		idx++;
		memset(tmp, 0, 4096);

		// get key
		sprintf(tmp, "%s%d", test_key, total_num);
		strcat(tcopy, tmp);

		idx++;
		int k_len = strlen(tcopy);
		int key_size = snprintf(NULL,0, "$%d\r\n%s\r\n",k_len,tcopy);
		char *key = malloc(key_size + 1);
		if(k_len == -1){
			snprintf(key, k_len, "$%d\r\n\r\n",k_len);
		}else if(k_len == 0){
			snprintf(key, k_len, "$%d\r\n",k_len);
		}else{
			sprintf(key, "$%d\r\n%s\r\n",k_len, tcopy);
		}
		strcat(msg, key);
		memset(tcopy, 0, 4096);

		// match cmd that need value：cmds[] 里没有 "GET"，所以 match 恒为 0，
		// 正好不会给 GET 拼 value，idx 就是 2
		int i = 0;
		int match = 0;
		for(i = 0; i < 8; i ++){
			if(strcmp(cmd_tmp, cmds[i]) == 0){
				match = 1;
				break;
			}
		}

		if(match){
			if(j == 3 || j == 7){
				sprintf(tmp, "%s%d", test_value_mod, total_num);
			}else{
				sprintf(tmp, "%s%d", test_value, total_num);
			}

			strcat(tcopy, tmp);

			idx++;
			int v_len = strlen(tcopy);
			int value_size = snprintf(NULL,0,"$%d\r\n%s\r\n",v_len,tcopy);
			char *value = malloc(value_size + 1);

			if(v_len == -1){
				snprintf(value, v_len, "$%d\r\n\r\n",v_len);
			}else if(v_len == 0){
				snprintf(value, v_len, "$%d\r\n",v_len);
			}else{
				sprintf(value, "$%d\r\n%s\r\n",v_len, tcopy);
			}
			strcat(msg, value);
			memset(tcopy, 0, 4096);
			free(value);
		}

		char final_send_mes[4096] = {0};

		// send request
		sprintf(final_send_mes, "*%d\r\n", idx);
		strcat(final_send_mes, msg);

		size_t need = strlen(final_send_mes);
		if (len + need + 1 > cap) {
			cap = cap * 2 + need;
			send_mes = realloc(send_mes, cap);
		}
		memcpy(send_mes + len, final_send_mes, need);
		len += need;

		send_num = len;
		free(cmd);
		free(key);

		j++;
	}
	printf("has send\n");

	struct timeval t0, t1; 

	length = strlen(send_mes);

	gettimeofday(&t0, NULL);    

	send_msg(connfd, send_mes, length);
	free(send_mes);

	struct timeval rto = { 5, 0 };
	setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, &rto, sizeof(rto));

	size_t rcap = 1024 * 1024;
	char *rbuf = malloc(rcap);
	size_t used = 0, pos = 0;

	int ok    = 0;      // 数据正确的条数
	int reply = 0;      // 已经收到的回复条数（也正好是第几个 key）

	while(reply < 100000){

		if(pos > 0){                                  // 把没解析完的挪到前面
			memmove(rbuf, rbuf + pos, used - pos);
			used -= pos;
			pos = 0;
		}
		if(used == rcap){
			rcap *= 2;
			rbuf = realloc(rbuf, rcap);
		}

		int n = recv(connfd, rbuf + used, rcap - used, 0);
		if(n <= 0){
			printf("recv stop n=%d, reply=%d\n", n, reply);
			break;
		}
		used += n;

		while(pos < used){

			if(rbuf[pos] != '$'){                     // -ERR / +OK 之类，跳过这一行
				char *nl = memchr(rbuf + pos, '\n', used - pos);
				if(nl == NULL) break;
				pos = (size_t)(nl - rbuf) + 1;
				reply++;
				continue;
			}

			char *nl = memchr(rbuf + pos, '\n', used - pos);
			if(nl == NULL) break;                     // 长度行还没收全
			size_t hdr = (size_t)(nl - (rbuf + pos)) + 1;
			int vlen = atoi(rbuf + pos + 1);          // "$-1\r\n" -> -1

			if(vlen < 0){                             // key 不存在
				pos += hdr;
				reply++;
				continue;
			}
			if(used - pos < hdr + (size_t)vlen + 2) break;   // 正文还没收全

			char *val = rbuf + pos + hdr;

			// 期望值：和 testcase 存进去的完全一样
			char exp_val[64] = {0};
			if(reply % 10 == 3 || reply % 10 == 7){
				sprintf(exp_val, "%s%d", test_value_mod, reply);
			}else{
				sprintf(exp_val, "%s%d", test_value, reply);
			}

			if((int)strlen(exp_val) == vlen && memcmp(val, exp_val, vlen) == 0){
				ok++;                                 // ← 数据对了就加一
			}

			pos += hdr + (size_t)vlen + 2;
			reply++;
		}
	}

	gettimeofday(&t1, NULL); 

	long   cost_ms = TIME_SUB_MS(t1, t0);
	double qps     = cost_ms > 0 ? (double)reply * 1000.0 / (double)cost_ms : 0.0;

	printf("result: has get: %d/ total: %d\n", ok, 100000);
	printf("time: %ld ms\nQPS: %.2f\n", cost_ms, qps);
	fflush(stdout);                            

	free(rbuf);
}



void get_old_mes(int connfd){
	FILE *fp = fopen("config/sync.log", "r");

	char mes[1024] = {0};
	int i = 0;
	while(fgets(mes, 128, fp)){
		i++;
		if(i != 0 && i % 10000 == 0){
			printf("%s",mes);
		}
		memset(mes, 0, 1024);
	}	

	fclose(fp);

	FILE *fp_tmp = fopen("config/sync.log", "w");
	fclose(fp_tmp);

	// send_msg(connfd, mes, strlen(mes));
}

int connect_tcpserver(const char *ip, unsigned short port) {

	int connfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(struct sockaddr_in));

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(ip);
	server_addr.sin_port = htons(port);
	if (0 !=  connect(connfd, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in))) {
		perror("connect");
		return -1;
	}
	
	return connfd;
	
}


static Config cfg;
// eg:
// ./testcase 192.168.159.137 2000
int main(int argc, char *argv[]) {

	load_config("config/testcase_save.conf", &cfg);
	int connfd = connect_tcpserver(cfg.ip, cfg.port);
	
	if(strcmp(argv[1], "0") == 0) {
		send_msg(connfd, "*1\r\n$5\r\nCLEAR\r\n", 15);
	}else if(strcmp(argv[1], "1") == 0) {
		testcase(connfd);
	}else if(strcmp(argv[1], "2") == 0) {
		testcase_get(connfd);
		return 0;
	}
	
	fgetc(stdin);

	return 0;
	
}



#if 0
#include "../NtyCo/core/nty_coroutine.h"
#include <netinet/in.h>

#include "kvstore.h"
#include <infiniband/verbs.h>
#include "load_conf.h"
#include "../src/rdma.h"
#include <sys/random.h>
#include <arpa/inet.h>
#include <endian.h> 
#include <malloc.h>

#define G_RMDA_BUF_SIZE 1024 * 1024 * 5

extern int full_save_to_resp(char *msg, uint32_t length, char *response);
extern int kvs_protocol(char *msg, int length, char *response);

Config cfg;

__g_rdma g_rdma_slave = {0};
__g_slave g_s_slave = {0};

uint32_t transfer_len = 0;

char *res = NULL;
void rdma_handshake(void *arg)
{
    int fd = nty_socket(AF_INET, SOCK_STREAM, 0);
    // int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0){
        perror("nty socket error");
        return ;
    }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9998);
    addr.sin_addr.s_addr = INADDR_ANY;
    inet_pton(AF_INET, cfg.ip, &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("nty_connect"); nty_close(fd); return;
    }

    g_rdma_slave.gid_index = pick_rocev2_gid_index(g_rdma_slave.ctx, 1);

    union ibv_gid local_gid;
    if(ibv_query_gid(g_rdma_slave.ctx, 1, g_rdma_slave.gid_index, &local_gid) != 0){
        perror("ibv query gid error");
        return ;
    }


    uint32_t local_psn;
    ssize_t r_ret = getrandom(&local_psn, sizeof(local_psn), 0);
    local_psn &= 0xffffff;

    if (r_ret != sizeof(local_psn)) {
        perror("getrandom error"); 
        return ;
    }

    struct {
        uint32_t qpn;
        uint32_t psn;
        uint32_t rkey;
        uint64_t vaddr;
        union ibv_gid gid;
    }__attribute__((packed)) local_info;

    local_info.gid = local_gid;
    local_info.psn = htonl(local_psn);
    local_info.qpn = htonl(g_rdma_slave.qp->qp_num);
    local_info.rkey = htonl(g_rdma_slave.mr->rkey);
    local_info.vaddr = htobe64((uint64_t)g_rdma_slave.buf);

    if(nty_send(fd, &local_info, sizeof(local_info), 0) != sizeof(local_info)){
        perror("nty send error");
        return ;
    }

    struct {
        uint32_t qpn;
        uint32_t psn;
        union ibv_gid gid;
    }__attribute__((packed)) remote_info;

    ssize_t nr = nty_recv(fd, &remote_info, sizeof(remote_info), 0);
    if(nr != sizeof(remote_info)){
        perror("rdma nty recv error");
        return ;
    }

    g_s_slave.remote_gid = remote_info.gid;
    g_s_slave.remote_psn = ntohl(remote_info.psn);
    g_s_slave.remote_qpn = ntohl(remote_info.qpn);

    rdma_modify(g_rdma_slave.qp, local_psn, g_s_slave.remote_qpn, &g_s_slave.remote_gid, g_s_slave.remote_psn, g_rdma_slave.gid_index);


    uint32_t recv_len_net;
    ssize_t mes_rv = nty_recv(fd, &recv_len_net, sizeof(recv_len_net),0);
    if(mes_rv != sizeof(recv_len_net)){
        perror("recv transfer length error");
        close(fd);
        return ;
    }
    transfer_len = ntohl(recv_len_net);


    if(transfer_len != 0){
        char *resp_switch = calloc(1, 1024 * 1024 * 5);
        char *resp_out    = calloc(1, 5 * 1024 * 1024);
        int length = full_save_to_resp(g_rdma_slave.buf, transfer_len, resp_switch);
        if(length > 0){
            if(res == NULL){
                res = calloc(1, 1024 * 1024);
            }
            int recv_length = kvs_protocol(resp_switch, length, resp_out);
        }else{
            kvs_protocol(g_rdma_slave.buf, transfer_len, resp_switch);
        }
        free(resp_switch); 
        free(resp_out);
    }
    uint32_t ack = htonl(1);
    nty_send(fd, &ack, sizeof(ack), 0);
    // close(fd);
    // nty_close(fd);
    char *cbuf = calloc(1, 1024 * 1024 * 5);
    char *cres = calloc(1, 1024 * 1024);
    if (!cbuf || !cres) {
        free(cbuf); free(cres);
        close(fd);
        return ;
    }


    while (1) {
        memset(cbuf, 0, 1024 * 1024 * 5);
        memset(cres, 0, 1024 * 1024);

        int n = nty_recv(fd, cbuf, 1024 * 1024 * 5, 0);  
        if (n <= 0) {
            printf("[slave] master gone, n=%d\n", n);
            break;
        }

        int rlen = kvs_protocol(cbuf, n, cres);
    }

    free(cbuf);
    free(cres);

    return ;
}


typedef struct {
    int    fd;
    char  *buf;        /* 队列 */
    size_t len;        /* 有效字节数 */
    size_t off;        /* 已发出偏移 */
    int    sending;    /* 是否已有发送协程在跑（保证同一时刻只有一个写者） */
    int    closed;     /* 读端已结束 */
} send_resp_t;



static int send_resp_append(send_resp_t *s, char *msg, int length){

    if(s == NULL || msg == NULL || length == 0) return -1;

    void *p = realloc(s->buf, s->len + length);
    if(p == NULL) return -2;
    s->buf = p;

    memcpy(s->buf + s->len, msg, length);
    s->len += length;

    return 0;
}


void send_func(void *arg){

    send_resp_t *sr = arg;

    // while(sr->off < sr->len){
    //     ssize_t n = nty_send(sr->fd, sr->buf + sr->off, sr->len - sr->off, 0);
    //     if(n > 0) {sr->off += (size_t)n; continue;}
    //     if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
    //         continue;
    //     }
    //     break;
    // }

    ssize_t n = nty_send(sr->fd, sr->buf + sr->off, sr->len - sr->off, 0);
    if (n > 0) sr->off += (size_t)n;
    if(sr->off >= sr->len) {sr->off = 0; sr->len = 0;}
    sr->sending = 0;

    if (sr->closed && sr->len == 0) {
        nty_close(sr->fd);
        free(sr->buf);
        free(sr);
    }
    // nty_send(send_resp[cli_fd]->fd, send_resp[cli_fd]->buf, send_resp[cli_fd]->len, 0);
}


void read_func(void *arg){

    int cli_fd = *(int *)arg;
    free(arg);

    if(cli_fd < 0){
        perror("read arg error");
        return ;
    }

    char *buf = calloc(1, 1024 * 64);
    char *resp = calloc(1, 1024 * 1024);
    if (!buf || !resp) {
        free(buf);
        free(resp);
        nty_close(cli_fd);
        return;
    }
    
    send_resp_t *sr = calloc(1, sizeof(send_resp_t));;
    if(!sr) { free(buf); free(resp); nty_close(cli_fd); return; }
    sr->fd = cli_fd;

    while(1){

        int recv_len = nty_recv(cli_fd, buf, 64 * 1024 - 1, 0);

        if (recv_len == 0) break;
        if (recv_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            fprintf(stderr, "[read] fd=%d recv_len=%d errno=%d(%s)\n",
                    cli_fd, recv_len, errno, strerror(errno));
            break;
        }
        
        int length = 0;

        length = kvs_protocol(buf, recv_len, resp);

        if (length > 0) {

            nty_coroutine *send_co;

            send_resp_append(sr, resp, length);
            
            if(!sr->sending){
                sr->sending = 1;
                nty_coroutine *send_co;
                nty_coroutine_create(&send_co, send_func, sr);
            }
        }

        static uint64_t last_trim_ms = 0;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_ms = (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
        if (now_ms - last_trim_ms >= 1000) {
            last_trim_ms = now_ms;
            malloc_trim(0);
        }
    }
    sr->closed = 1;
    if (!sr->sending) { 
        nty_close(sr->fd);
        free(sr->buf);
        free(sr);
    }

    malloc_trim(0);
    free(buf);    
    free(resp);
}


void accept_func(void *arg){

    int port = *(int *)arg;
    free(arg);
    if(!port) return ;
    int fd = nty_socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0){
        perror("nty_socket error");
        return ;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if(bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))){
        perror("bind2 error");
        nty_close(fd);
        return;
    }

    int listen_ret = listen(fd, 10);
    if(listen_ret < 0){
        perror("listen error");
        nty_close(fd);
        return ;
    }

    while(1){
        socklen_t len = sizeof(struct sockaddr_in);
        int client_fd = nty_accept(fd, (struct sockaddr *)&addr, &len);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue; 
            if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS) continue; 
            if (errno == EBADF || errno == EINVAL) break;
                perror("nty_accept");
                continue;
        }

        int *arg = calloc(1, sizeof(int));
        *arg = client_fd;
        if (!arg) { nty_close(client_fd); continue; }
        nty_coroutine *read_co;
        nty_coroutine_create(&read_co, read_func, arg);
    }
}


int ntyco_start(Config *server_cfg, __g_rdma *g_m_rdma){

    srand(time(NULL));

    cfg = *server_cfg;

    nty_coroutine *co = NULL;
    int *p = calloc(1, sizeof(int));
    *p = server_cfg->port;

    g_rdma_slave = *g_m_rdma;


    nty_coroutine_create(&co, accept_func, p);
    nty_coroutine_create(&co, rdma_handshake, p);
    nty_schedule_run();

    return 0;
}
#else 
#endif