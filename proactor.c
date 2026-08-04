


#include "kvstore.h"

// #include "kvstore.h"

#define EVENT_ACCEPT   	0
#define EVENT_READ		1
#define EVENT_WRITE		2

extern int kvs_protocol(char *msg, int length, char *response);
typedef int(*msg_handle) (char *msg, int length, char *response);

struct conn_info {
	int fd;
	int event;
};


int pinit_server(unsigned short port) {	

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);	
	struct sockaddr_in serveraddr;	
	memset(&serveraddr, 0, sizeof(struct sockaddr_in));	
	serveraddr.sin_family = AF_INET;	
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);	
	serveraddr.sin_port = htons(port);	

	if (-1 == bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(struct sockaddr))) {		
		perror("bind");		
		return -1;	
	}	

	listen(sockfd, 10);
	
	return sockfd;
}



#define ENTRIES_LENGTH		1024
#define BUFFER_LENGTH		1024

int set_event_recv(struct io_uring *ring, int sockfd,
				      void *buf, size_t len, int flags) {

	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);

	struct conn_info accept_info = {
		.fd = sockfd,
		.event = EVENT_READ,
	};
	
	io_uring_prep_recv(sqe, sockfd, buf, len, flags);
	memcpy(&sqe->user_data, &accept_info, sizeof(struct conn_info));

}


int set_event_send(struct io_uring *ring, int sockfd,
				      void *buf, size_t len, int flags) {

	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);

	struct conn_info accept_info = {
		.fd = sockfd,
		.event = EVENT_WRITE,
	};
	
	io_uring_prep_send(sqe, sockfd, buf, len, flags);
	memcpy(&sqe->user_data, &accept_info, sizeof(struct conn_info));

}



int set_event_accept(struct io_uring *ring, int sockfd, struct sockaddr *addr,
					socklen_t *addrlen, int flags) {

	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);

	struct conn_info accept_info = {
		.fd = sockfd,
		.event = EVENT_ACCEPT,
	};
	
	io_uring_prep_accept(sqe, sockfd, (struct sockaddr*)addr, addrlen, flags);
	memcpy(&sqe->user_data, &accept_info, sizeof(struct conn_info));

}

// typedef int(*msg_handle) (char *msg, int length, char *response);
static msg_handle kvs_handle;
int total_length = 0;


int proactor_start(unsigned short port, msg_handle handle) {

	// unsigned short port = 2000;
	int sockfd = pinit_server(port);
	kvs_handle = handle;

	struct io_uring_params params;
	memset(&params, 0, sizeof(params));

	struct io_uring ring;
	io_uring_queue_init_params(ENTRIES_LENGTH, &ring, &params);

	
#if 0
	struct sockaddr_in clientaddr;	
	socklen_t len = sizeof(clientaddr);
	accept(sockfd, (struct sockaddr*)&clientaddr, &len);
#else

	struct sockaddr_in clientaddr;	
	socklen_t len = sizeof(clientaddr);
	set_event_accept(&ring, sockfd, (struct sockaddr*)&clientaddr, &len, 0);
	
#endif

	char buffer[BUFFER_LENGTH] = {0};

	char response[1024] = {0};

	int fd_count = 0;
	while (1) {

		io_uring_submit(&ring);


		struct io_uring_cqe *cqe;
		io_uring_wait_cqe(&ring, &cqe);

		struct io_uring_cqe *cqes[128];
		int nready = io_uring_peek_batch_cqe(&ring, cqes, 128);  // epoll_wait

		int i = 0;

		for (i = 0;i < nready;i ++) {

			struct io_uring_cqe *entries = cqes[i];
			struct conn_info result;
			memcpy(&result, &entries->user_data, sizeof(struct conn_info));

			if (result.event == EVENT_ACCEPT) {

				set_event_accept(&ring, sockfd, (struct sockaddr*)&clientaddr, &len, 0);
				//printf("set_event_accept\n"); //

				int connfd = entries->res;

				char ip[128] = {0};
				memset(ip, 0, sizeof(ip));
				check_fd_to_ip(connfd, ip);
				if(strcmp(ip,"192.168.159.129") == 0){
					fd_list[fd_count].fd = connfd;
					strcpy(fd_list[fd_count].ip, ip);
					fd_count++;

				}
				if(strcmp(ip,"192.168.159.136") == 0){
					fd_list[fd_count].fd = connfd;
					strcpy(fd_list[fd_count].ip, ip);
					fd_count++;
				}

				set_event_recv(&ring, connfd, buffer, BUFFER_LENGTH, 0);

				
			} else if (result.event == EVENT_READ) {  //

				int ret = entries->res;

				if (ret == 0) {
					close(result.fd);
				} else if (ret > 0) {
					buffer[ret] = '\0';
					
					// printf("set_event_recv ret: %d, %s\n", ret, buffer); //

					//int kvs_protocol(char *msg, int length, char *response);
					ret = kvs_handle(buffer, ret, response);

					char msg[TXT_LENGTH] = {0};
					// memset(msg, 0, TXT_LENGTH);

					// for(i = 0; i < fd_count; i++){
					// 	memset(msg, 0, TXT_LENGTH);
					// 	int length = kvs_push_get(msg);
					// 	set_event_send(&ring, fd_list[i].fd, msg, length, 0);
					// 	printf("1%d, %s\n",fd_list[i].fd, msg);
					// }
					for(i = 0; i < fd_count; i++){
						memset(fd_list[i].info, 0, 1024);
						int length = kvs_push_get(fd_list[i].info);
						set_event_send(&ring, fd_list[i].fd, fd_list[i].info, length, 0);
					}
					
					
					set_event_send(&ring, result.fd, response, ret, 0);
					
				}
			}  else if (result.event == EVENT_WRITE) {
  //

				int ret = entries->res;
				//printf("set_event_send ret: %d, %s\n", ret, buffer);

				set_event_recv(&ring, result.fd, buffer, BUFFER_LENGTH, 0);
				
			}
			kvs_push_clear();
		}

		io_uring_cq_advance(&ring, nready);
		// kvs_push_clear();		
		
	}

}


