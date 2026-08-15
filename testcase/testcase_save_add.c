

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "mempool.h"
#include <ctype.h>

#include "../src/kvstore.h"
#include "../src/load_conf.h"

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
void testcase(int connfd){
	int length = 0;
	int j = 0;
	int total_num = 0;
	char *send_mes = malloc(4096000);
	// char *send_mes = malloc(4096);

	size_t cap = 4096000; 
    size_t len = 0;
    for(total_num = 0; total_num < 100000; total_num ++){
		
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
        // strcpy(tmp,test_cmd[j]);
		strcpy(tmp,"SET");
        tmp[strcspn(tmp, "\n")] = '\0';
        int c_len = strlen(tmp);
		memcpy(cmd_tmp, tmp, c_len);

		int cmd_size = snprintf(NULL,0,"$%d\r\n%s\r\n",c_len,tmp);
		char *cmd = malloc(cmd_size + 1);
		sprintf(cmd, "$%d\r\n%s\r\n",c_len, tmp);
		// printf("cmd:%s\n",cmd);
		memcpy(msg, cmd, strlen(cmd));
		msg[strlen(cmd)] = '\0';
		// length += strlen(cmd);
		idx++;
		memset(tmp, 0, 4096);

		// get key

		sprintf(tmp, "%s%d", test_key, total_num);

		strcat(tcopy, tmp);
        
		idx++;
        int k_len = strlen(tcopy);
		// length += k_len;
		int key_size = snprintf(NULL,0, "$%d\r\n%s\r\n",k_len,tcopy);
		char *key = malloc(key_size + 1);
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
				// strcpy(tmp, test_value_mod);
				sprintf(tmp, "%s%d", test_value_mod, total_num);
			}else{
				// strcpy(tmp, test_value);
				sprintf(tmp, "%s%d", test_value, total_num);
			}
				
			strcat(tcopy, tmp);

			idx++;
            int v_len = strlen(tcopy);
			// length += v_len;
			int value_size = snprintf(NULL,0,"$%d\r\n%s\r\n",v_len,tcopy);
			char *value = malloc(value_size + 1);
            
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
		// strcat(send_mes, final_send_mes);
		memcpy(send_mes + len, final_send_mes, need); 
        len += need;

		send_num = len;
		free(cmd);
		free(key);

		j++;
		
		
    }
	printf("len=%ld\n", strlen(send_mes));

	char *tmp = malloc(len + 32);
	length = strlen(send_mes);
	
	send_msg(connfd, send_mes, length);
}

void get_old_mes(int connfd){
	FILE *fp = fopen("../sync.log", "r");

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

	FILE *fp_tmp = fopen("../sync.log", "w");
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

	load_config("../config/testcase_add.conf", &cfg);

	int connfd = connect_tcpserver(cfg.ip, cfg.port);
	if(cfg.get_old_mes_flag == 1) get_old_mes(connfd);	
	if(cfg.mode == 1) testcase(connfd);
	if(cfg.full_save_flag == 1){
		char tmp[128] = {0};
		while(fgets(tmp, sizeof(tmp), stdin)){
			if(strcmp(tmp, "save\n") == 0) {
				send_msg(connfd, tmp, strlen(tmp));
                break;
            }
		}
	}
	
	fgetc(stdin);

	return 0;
	
}



