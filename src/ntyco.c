



#include "../NtyCo/core/nty_coroutine.h"
#include <netinet/in.h>
#include "kvstore.h"

extern int kvs_protocol(char *msg, int length, char *response);


void read_func(void *arg){
    int cli_fd = *(int *)arg;
    free(arg);

    if(cli_fd < 0){
        perror("read arg error");
        return ;
    }
    
    char *buf = calloc(1, 1024 * 1024 * 5);
    char *response = calloc(1, 1024 * 1024 * 1);
    if (!buf || !response) {
        free(buf);
        free(response);
        nty_close(cli_fd);
        return;
    }

    while(1){
        memset(buf, 0, 1024 * 1024 * 5);
        memset(response, 0, 1024 * 1024 * 1);
printf("[RECV_CB] fd=%d\n", cli_fd);
        int recv_len = nty_recv(cli_fd, buf, 1024 * 1024 * 5, 0);
        if (recv_len <= 0) {
            break;
        }

        int length = kvs_protocol(buf, recv_len, response);
        if(length > 0){
            int ret = nty_send(cli_fd, response, length, 0);
printf("SAVE send ret=%d\n", ret);
        }
        
    }
    nty_close(cli_fd);
    free(buf);    
    free(response);
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
    
    struct sockaddr_in addr;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if(bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))){
        perror("bind error");
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
            continue;
        }

        int *arg = calloc(1, sizeof(int));
        *arg = client_fd;
        nty_coroutine *read_co;
        nty_coroutine_create(&read_co, read_func, arg);
    }
}

int ntyco_start(int port){

    nty_coroutine *co = NULL;
    int *p = calloc(1, sizeof(int));
    *p = port;

    nty_coroutine_create(&co, accept_func, p);

    nty_schedule_run();
    return 0;
}