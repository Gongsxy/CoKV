




#include <sys/socket.h>
#include <pthread.h>
#include <poll.h>
#include <sys/epoll.h>
#include <errno.h>



#include "server.h"
#include "kvstore.h"
#include "load_conf.h"

#define CONNECTION_SIZE			1024 // 1024 * 1024

#define MAX_PORTS			20

static Config cfg;

#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

extern int kvs_protocol(char *msg, int length, char *response);
// typedef int(*msg_handle)(char *msg, int length, char *response);
static msg_handle r_handle;

// int resquest(struct conn *c){
// 	c->wlength = r_handle(c->rbuffer,c->rlength,c->wbuffer);
// }

int resquest(struct conn *c){
	c->wlength = kvs_protocol(c->rbuffer, c->rlength, c->wbuffer);
	return 0;
}

int accept_cb(int fd);
int recv_cb(int fd);
int send_cb(int fd);



int epfd = 0;
struct timeval begin;

struct fd_info *fd_list = NULL;

struct conn *conn_list = NULL;
// fd
// struct resp_info *resp_list[128] = {0};

int set_event(int fd, int event, int flag) {

	if (flag) {  // non-zero add

		struct epoll_event ev;
		ev.events = event;
		ev.data.fd = fd;
		epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

	} else {  // zero mod

		struct epoll_event ev;
		ev.events = event;
		ev.data.fd = fd;
		epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
		
	}
	

}


int event_register(int fd, int event) {

	if (fd < 0) return -1;

	conn_list[fd].fd = fd;
	conn_list[fd].r_action.recv_callback = recv_cb;
	conn_list[fd].send_callback = send_cb;
		   
	memset(conn_list[fd].rbuffer, 0, BUFFER_LENGTH);
	conn_list[fd].rlength = 0;

	memset(conn_list[fd].wbuffer, 0, BUFFER_LENGTH);
	conn_list[fd].wlength = 0;

	set_event(fd, event, 1);
}


int fd_count = 0;
// listenfd(sockfd) --> EPOLLIN --> accept_cb
int accept_cb(int fd) {

	struct sockaddr_in  clientaddr;
	socklen_t len = sizeof(clientaddr);

	int clientfd = accept(fd, (struct sockaddr*)&clientaddr, &len);
	//printf("accept finshed: %d\n", clientfd);
	char ip[128] = {0};
	memset(ip, 0, sizeof(ip));
	// check_fd_to_ip(clientfd, ip);
	// if(strcmp(ip,cfg.sync_ip) == 0){
	// 	fd_list[fd_count].fd = clientfd;
	// 	strcpy(fd_list[fd_count].ip, ip);
	// 	fd_count++;
		
	// 	// printf("save fd=%d\n",clientfd);
	// }
	// if(strcmp(ip,cfg.sync_ip) == 0){
	// 	fd_list[fd_count].fd = clientfd;
	// 	strcpy(fd_list[fd_count].ip, ip);
	// 	fd_count++;
	// 	// printf("save fd=%d\n",clientfd);
	// }
	if (clientfd < 0) {
		printf("accept errno: %d --> %s\n", errno, strerror(errno));
		return -1;
	}
	
	event_register(clientfd, EPOLLIN);  // | EPOLLET

	if ((clientfd % 1000) == 0) {

		struct timeval current;
		gettimeofday(&current, NULL);

		int time_used = TIME_SUB_MS(current, begin);
		memcpy(&begin, &current, sizeof(struct timeval));
		

		printf("accept finshed: %d, time_used: %d\n", clientfd, time_used);

	}

	return 0;
}


int recv_cb(int fd) {
	memset(conn_list[fd].rbuffer, 0, BUFFER_LENGTH );
	int count = recv(fd, conn_list[fd].rbuffer, BUFFER_LENGTH - 1, 0);
	if (count == 0) { // disconnect
		// printf("client disconnect: %d\n", fd);
		close(fd);

		epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL); // unfinished

		return 0;
	} else if (count < 0) { // 

		printf("count: %d, errno: %d, %s\n", count, errno, strerror(errno));
		close(fd);
		epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);

		return 0;
	}

	conn_list[fd].rlength = count;

	if(count > 0){
    	conn_list[fd].rbuffer[count]='\0';
	}

	resquest(&conn_list[fd]);

	set_event(fd, EPOLLOUT, 0);

	return count;
}


int j = 0;
int send_cb(int fd) {

	int count = 0;

	if (conn_list[fd].wlength != 0 ) {
		
		count = send(fd, conn_list[fd].wbuffer, conn_list[fd].wlength, 0);
	}
	
	char msg[TXT_LENGTH] = {0};
	memset(msg, 0, TXT_LENGTH);

	int i = 0;
	for(i = 0; i < fd_count; i++){

		int length = kvs_push_get(msg);
		count = send(fd_list[i].fd, msg, length, 0);
	}

	set_event(fd, EPOLLIN, 0);

	//set_event(fd, EPOLLOUT, 0);
	kvs_push_clear();
	 
	return count;
}



int init_server(unsigned short port) {

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in servaddr;
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0
	servaddr.sin_port = htons(port); // 0-1023, 

	if (-1 == bind(sockfd, (struct sockaddr*)&servaddr, sizeof(struct sockaddr))) {
		printf("bind failed: %s\n", strerror(errno));
	}

	listen(sockfd, 10);
	//printf("listen finshed: %d\n", sockfd); // 3 

	return sockfd;

}


int check_fd_to_ip(int fd, char *ip){
	struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    memset(&addr, 0, sizeof(addr));

    if(getpeername(fd, (struct sockaddr *)&addr, &len) < 0){
        return -1;
    }
	strcpy(ip, inet_ntoa(addr.sin_addr));
	return 0;
}


int reactor_start(unsigned short port, msg_handle hand) {

	// fd_list = m_malloc(sizeof(struct fd_info));
	fd_list = malloc(sizeof(struct fd_info) * 512);
	conn_list = malloc(sizeof(struct conn) * 512);

	memset(fd_list,0,sizeof(struct fd_info) * 512);
	memset(conn_list,0,sizeof(struct conn) * 512);

	// unsigned short port = 2000;
	r_handle = hand;

	epfd = epoll_create(1);

	load_config("config/sync.conf", &cfg);

	int i = 0;

	for (i = 0;i < MAX_PORTS;i ++) {
		
		int sockfd = init_server(port + i);
		
		conn_list[sockfd].fd = sockfd;
		conn_list[sockfd].r_action.recv_callback = accept_cb;
		
		set_event(sockfd, EPOLLIN, 1);
	}


	if (cfg.is_slave == 1) {
        int master_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(cfg.port);
        inet_pton(AF_INET, cfg.ip, &addr.sin_addr);

        if (connect(master_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {    

            event_register(master_fd, EPOLLIN);
            conn_list[master_fd].r_action.recv_callback = recv_cb;  
            
			send(master_fd, "SYNC\n", 5, 0);


        } else {
            printf("Connect to master failed: %s\n", strerror(errno));
            close(master_fd);
        }
    }


	gettimeofday(&begin, NULL);

	while (1) { // mainloop

		struct epoll_event events[1024] = {0};
		int nready = epoll_wait(epfd, events, 1024, -1);
		
		int i = 0;
		for (i = 0;i < nready;i ++) {

			int connfd = events[i].data.fd;

			if (events[i].events & EPOLLIN) {
				conn_list[connfd].r_action.recv_callback(connfd);
			} 

			if (events[i].events & EPOLLOUT) {
				conn_list[connfd].send_callback(connfd);
			}
		}	

	}
	free(fd_list);
	free(conn_list);
}


