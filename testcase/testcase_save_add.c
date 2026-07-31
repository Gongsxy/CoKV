

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "mempool.h"
#include <ctype.h>

#define MAX_MSG_LENGTH		1024
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)


const char *cmds[] = {
    "SET","MOD","RSET","RMOD","HSET","HMOD","SSET","SMOD"
};

const char *test_cmd[] = {
	"SET","GET","EXIST","MOD","GET","DEL","EXIST","MOD","GET","EXIST"
};

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

int num = 0;
char send_mes[4096] = {0};
void testcase(int connfd){
	int j = 0;
	int total_num = 0;
    for(total_num = 0; total_num < 10; total_num ++){
		// printf("total_num: %d\n",total_num);
		char msg[4096] = {0};
        char tmp[4096] = {0};
		char tcopy[4096] = {0};
		char cmd_tmp[128] = {0};
		int exit_status = 0;
		int idx = 0;

		// get cmd
        strcpy(tmp,test_cmd[total_num]);
        tmp[strcspn(tmp, "\n")] = '\0';
        int c_len = strlen(tmp);
		memcpy(cmd_tmp, tmp, c_len);
		char *cmd = m_malloc(c_len + 1);
		sprintf(cmd, "$%d\r\n%s\r\n",c_len, tmp);
		// printf("cmd:%s\n",cmd);
		memcpy(msg, cmd, strlen(cmd));
		idx++;
		memset(tmp, 0, 4096);

		// get key
		
        strcpy(tmp, test_key);

        if(strcmp(tmp, "q\n") == 0) {
            break;
        }
		strcat(tcopy, tmp);
        
		idx++;
        int k_len = strlen(tcopy);
		char *key = m_malloc(k_len * 2);
		if(k_len == -1){
			snprintf(key, k_len, "$%d\r\n\r\n",k_len);
		}else if(k_len == 0){
			snprintf(key, k_len, "$%d\r\n",k_len);
		}else{
			sprintf(key, "$%d\r\n%s\r\n",k_len, tcopy);
		}

		strcat(msg, key);
		memset(tcopy, 0, 4096);
		
		// match cmd that need value
        int i = 0;
        int match = 0;
        for(i = 0; i < 8; i ++){
			
            if(strcmp(cmd_tmp, cmds[i]) == 0){
                match = 1;
                break;
            }
        } 
		
        if(match){
			if(total_num == 3 || total_num == 7){
				strcpy(tmp, test_value_mod);
			}else{
				strcpy(tmp, test_value);
			}
			if(strcmp(tmp, "q\n") == 0) {
				break;
			}
				
			strcat(tcopy, tmp);

			idx++;
            int v_len = strlen(tcopy);
			char *value = m_malloc(v_len * 2);
            
			if(v_len == -1){
				snprintf(value, v_len, "$%d\r\n\r\n",v_len);
			}else if(v_len == 0){
				snprintf(value, v_len, "$%d\r\n",v_len);
			}else{
				sprintf(value, "$%d\r\n%s\r\n",v_len, tcopy);
			}
			strcat(msg, value);
			memset(tcopy, 0, 4096);
			m_free(value);
        }
		

		int length = strlen(msg);
		char final_send_mes[4096];
		

		
		// send request

		sprintf(final_send_mes, "*%d\r\n", idx);
		strcat(final_send_mes, msg);
		strcat(send_mes, final_send_mes);


        // send_msg(connfd, final_send_mes, strlen(final_send_mes));

		m_free(cmd);
		m_free(key);
		// printf("Send_Mes %d:%s\n",total_num, send_mes);

		// quit
		// printf("Press \"quit\" to exit, other to continue\n");
		// while(fgets(tmp, sizeof(tmp),stdin)){
		// 	if(strcmp(tmp, "quit\n") == 0){
		// 		exit_status = 1;
		// 		break;
		// 	}else {
		// 		memset(tmp, 0, 1024);
		// 		break;
		// 	}
		// }
		// if(exit_status == 1){
		// 	break;
		// }
		// i++;
    }
	printf("send mes:%s\n",send_mes);

	send_msg(connfd, send_mes, strlen(send_mes));
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


// void array_testcase(int connfd) {

// 	testcase(connfd, "SET Teacher King", "OK\r\n", "SET-Teacher");
// 	testcase(connfd, "GET Teacher", "King\r\n", "GET-Teacher");
// 	testcase(connfd, "MOD Teacher Darren", "OK\r\n", "MOD-Teacher");
// 	testcase(connfd, "GET Teacher", "Darren\r\n", "GET-Teacher");
// 	testcase(connfd, "EXIST Teacher", "EXIST\r\n", "GET-Teacher");
// 	testcase(connfd, "DEL Teacher", "OK\r\n", "DEL-Teacher");
// 	testcase(connfd, "GET Teacher", "NO EXIST\r\n", "GET-Teacher");
// 	testcase(connfd, "MOD Teacher KING", "NO EXIST\r\n", "MOD-Teacher");
// 	testcase(connfd, "EXIST Teacher", "NO EXIST\r\n", "GET-Teacher");

// }

// void array_testcase_1w(int connfd) {

// 	int count = 10000;
// 	int i = 0;

// 	struct timeval tv_begin;
// 	gettimeofday(&tv_begin, NULL);

// 	for (i = 0;i < count;i ++) {

// 		testcase(connfd, "SET Teacher King", "OK\r\n", "SET-Teacher");
// 		testcase(connfd, "GET Teacher", "King\r\n", "GET-Teacher");
// 		testcase(connfd, "MOD Teacher Darren", "OK\r\n", "MOD-Teacher");
// 		testcase(connfd, "GET Teacher", "Darren\r\n", "GET-Teacher");
// 		testcase(connfd, "EXIST Teacher", "EXIST\r\n", "GET-Teacher");
// 		testcase(connfd, "DEL Teacher", "OK\r\n", "DEL-Teacher");
// 		testcase(connfd, "GET Teacher", "NO EXIST\r\n", "GET-Teacher");
// 		testcase(connfd, "MOD Teacher KING", "NO EXIST\r\n", "MOD-Teacher");
// 		testcase(connfd, "EXIST Teacher", "NO EXIST\r\n", "GET-Teacher");

// 	}

// 	struct timeval tv_end;
// 	gettimeofday(&tv_end, NULL);

// 	int time_used = TIME_SUB_MS(tv_end, tv_begin); // ms

// 	printf("array testcase --> time_used: %d, qps: %d\n", time_used, 90000 * 1000 / time_used);

// }


// void rbtree_testcase(int connfd) {

// 	testcase(connfd, "RSET Teacher King", "OK\r\n", "RSET-Teacher");
// 	testcase(connfd, "RGET Teacher", "King\r\n", "RGET-King-Teacher");
// 	testcase(connfd, "RMOD Teacher Darren", "OK\r\n", "RMOD-D-Teacher");
// 	testcase(connfd, "RGET Teacher", "Darren\r\n", "RGET-Darren-Teacher");
// 	testcase(connfd, "REXIST Teacher", "EXIST\r\n", "REXIST-Teacher");
// 	testcase(connfd, "RDEL Teacher", "OK\r\n", "RDEL-Teacher");
// 	testcase(connfd, "RGET Teacher", "NO EXIST\r\n", "RGET-K-Teacher");
// 	testcase(connfd, "RMOD Teacher KING", "NO EXIST\r\n", "RMOD-K-Teacher");
// 	testcase(connfd, "REXIST Teacher", "NO EXIST\r\n", "REXIST-Teacher");

// }

// void rbtree_testcase_1w(int connfd) {

// 	int count = 10000;
// 	int i = 0;

// 	struct timeval tv_begin;
// 	gettimeofday(&tv_begin, NULL);

// 	for (i = 0;i < count;i ++) {

// 		testcase(connfd, "RSET Teacher King", "OK\r\n", "RSET-Teacher");
// 		testcase(connfd, "RGET Teacher", "King\r\n", "RGET-King-Teacher");
// 		testcase(connfd, "RMOD Teacher Darren", "OK\r\n", "RMOD-D-Teacher");
// 		testcase(connfd, "RGET Teacher", "Darren\r\n", "RGET-Darren-Teacher");
// 		testcase(connfd, "REXIST Teacher", "EXIST\r\n", "REXIST-Teacher");
// 		testcase(connfd, "RDEL Teacher", "OK\r\n", "RDEL-Teacher");
// 		testcase(connfd, "RGET Teacher", "NO EXIST\r\n", "RGET-K-Teacher");
// 		testcase(connfd, "RMOD Teacher KING", "NO EXIST\r\n", "RMOD-K-Teacher");
// 		testcase(connfd, "REXIST Teacher", "NO EXIST\r\n", "REXIST-Teacher");

// 	}

// 	struct timeval tv_end;
// 	gettimeofday(&tv_end, NULL);

// 	int time_used = TIME_SUB_MS(tv_end, tv_begin); // ms

// 	printf("rbtree testcase --> time_used: %d, qps: %d\n", time_used, 90000 * 1000 / time_used);
	
// }


// void rbtree_testcase_3w(int connfd) {

// 	int count = 10000;
// 	int i = 0;

// 	struct timeval tv_begin;
// 	gettimeofday(&tv_begin, NULL);

// 	for (i = 0;i < count;i ++) {

// 		char cmd[128] = {0};
// 		snprintf(cmd, 128, "RSET Teacher%d King%d", i, i);
// 		testcase(connfd, cmd, "OK\r\n", "RSET-Teacher");
// 	}

// 	for (i = 0;i < count;i ++) {

// 		char cmd[128] = {0};
// 		snprintf(cmd, 128, "RGET Teacher%d", i);

// 		char result[128] = {0};
// 		snprintf(result, 128, "King%d\r\n", i);
		
// 		testcase(connfd, cmd, result, "RGET-King-Teacher");
// 	}

// 	for (i = 0;i < count;i ++) {

// 		char cmd[128] = {0};
// 		snprintf(cmd, 128, "RMOD Teacher%d King%d", i, i);
// 		testcase(connfd, cmd, "OK\r\n", "RGET-King-Teacher");
// 	}

// 	struct timeval tv_end;
// 	gettimeofday(&tv_end, NULL);

// 	int time_used = TIME_SUB_MS(tv_end, tv_begin); // ms

// 	printf("rbtree testcase --> time_used: %d, qps: %d\n", time_used, 30000 * 1000 / time_used);

// }

// void hash_testcase(int connfd) {

// 	testcase(connfd, "HSET Teacher King", "OK\r\n", "HSET-Teacher");
// 	testcase(connfd, "HGET Teacher", "King\r\n", "HGET-King-Teacher");
// 	testcase(connfd, "HMOD Teacher Darren", "OK\r\n", "HMOD-D-Teacher");
// 	testcase(connfd, "HGET Teacher", "Darren\r\n", "HGET-Darren-Teacher");
// 	testcase(connfd, "HEXIST Teacher", "EXIST\r\n", "HEXIST-Teacher");
// 	testcase(connfd, "HDEL Teacher", "OK\r\n", "HDEL-Teacher");
// 	testcase(connfd, "HGET Teacher", "NO EXIST\r\n", "HGET-K-Teacher");
// 	testcase(connfd, "HMOD Teacher KING", "NO EXIST\r\n", "HMOD-K-Teacher");
// 	testcase(connfd, "HEXIST Teacher", "NO EXIST\r\n", "HEXIST-Teacher");

// }

// void skiplist_testcase(int connfd) {

// 	testcase(connfd, "SSET Teacher King", "OK\r\n", "SSET-Teacher");
// 	testcase(connfd, "SGET Teacher", "King\r\n", "SGET-King-Teacher");
// 	testcase(connfd, "SMOD Teacher Darren", "OK\r\n", "SMOD-D-Teacher");
// 	testcase(connfd, "SGET Teacher", "Darren\r\n", "SGET-Darren-Teacher");
// 	testcase(connfd, "SEXIST Teacher", "EXIST\r\n", "SEXIST-Teacher");
// 	testcase(connfd, "SDEL Teacher", "OK\r\n", "SDEL-Teacher");
// 	testcase(connfd, "SGET Teacher", "NO EXIST\r\n", "SGET-K-Teacher");
// 	testcase(connfd, "SMOD Teacher KING", "NO EXIST\r\n", "SMOD-K-Teacher");
// 	testcase(connfd, "SEXIST Teacher", "NO EXIST\r\n", "SEXIST-Teacher");
//     testcase(connfd);
// }

// eg:
// ./testcase 192.168.159.137 2000
int main(int argc, char *argv[]) {

	if (argc != 3) {
		printf("arg error\n");
		return -1;
	}

	char *ip = argv[1];
	int port = atoi(argv[2]);


	int connfd = connect_tcpserver(ip, port);

	testcase(connfd);

	return 0;
	
}



