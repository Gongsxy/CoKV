

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
	int length = 0;
	int j = 0;
	int total_num = 0;
    for(total_num = 0; total_num < 40; total_num ++){
		
		if(j == 10){
			j = 0;
		}
		// printf("total_num: %d\n",total_num);
		char msg[4096] = {0};
        char tmp[4096] = {0};
		char tcopy[4096] = {0};


		char cmd_tmp[128] = {0};
		int exit_status = 0;
		int idx = 0;

		// get cmd
        strcpy(tmp,test_cmd[j]);
        tmp[strcspn(tmp, "\n")] = '\0';
        int c_len = strlen(tmp);
		memcpy(cmd_tmp, tmp, c_len);
		char *cmd = m_malloc(c_len + 1);
		sprintf(cmd, "$%d\r\n%s\r\n",c_len, tmp);
		// printf("cmd:%s\n",cmd);
		memcpy(msg, cmd, strlen(cmd));
		// length += strlen(cmd);
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
		// length += k_len;
		char *key = m_malloc(k_len * 2);
		if(k_len == -1){
			snprintf(key, k_len, "$%d\r\n\r\n",k_len);
		}else if(k_len == 0){
			snprintf(key, k_len, "$%d\r\n",k_len);
		}else{
			sprintf(key, "$%d\r\n%s\r\n",k_len, tcopy);
		}
		// length += strlen(key);
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
			if(j == 3 || j == 7){
				strcpy(tmp, test_value_mod);
			}else{
				strcpy(tmp, test_value);
			}
				
			strcat(tcopy, tmp);

			idx++;
            int v_len = strlen(tcopy);
			// length += v_len;
			char *value = m_malloc(v_len * 2);
            
			if(v_len == -1){
				snprintf(value, v_len, "$%d\r\n\r\n",v_len);
			}else if(v_len == 0){
				snprintf(value, v_len, "$%d\r\n",v_len);
			}else{
				sprintf(value, "$%d\r\n%s\r\n",v_len, tcopy);
			}
			// length += strlen(value);
			strcat(msg, value);
			memset(tcopy, 0, 4096);
			m_free(value);
        }
		

		char final_send_mes[4096] = {0};
		
		// send request

		sprintf(final_send_mes, "*%d\r\n", idx);
		strcat(final_send_mes, msg);
		strcat(send_mes, final_send_mes);

		m_free(cmd);
		m_free(key);

		j++;
		
		
    }
	// printf("send mes:%s\n",send_mes);
	// printf("strlen=%ld\n", strlen(send_mes));
	printf("send len=%ld\n",strlen(send_mes));
	char tmp[4096] = {0};
	length = strlen(send_mes);
	memcpy(tmp, send_mes, length);
	memset(send_mes,0, length);
	sprintf(send_mes, "%d\r\n",length);
	strcat(send_mes, tmp);
	length = strlen(send_mes);
	send_msg(connfd, send_mes, length);
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



