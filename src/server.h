



#ifndef __SERVER_H__
#define __SERVER_H__

#define BUFFER_LENGTH		102400



typedef int (*RCALLBACK)(int fd);


extern struct resp_info *resp_list[128]; 

struct conn {
	int fd;
	int type;
	char rbuffer[BUFFER_LENGTH];
	int rlength;

	char wbuffer[BUFFER_LENGTH];
	int wlength;

	RCALLBACK send_callback;

	union {
		RCALLBACK recv_callback;
		RCALLBACK accept_callback;
	} r_action;

	int total_length;
	int status;
#if 1 // websocket
	char *payload;
	char mask[4];
#endif
};

extern struct conn *conn_list;

int http_request(struct conn *c);
int http_response(struct conn *c);

int ws_request(struct conn *c);
int ws_response(struct conn *c);



#endif


