
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <time.h>

#include "../NtyCo/core/nty_coroutine.h"
#include "sync_slave.h"

#define SYNC_QUEUE_CAP  4096
#define FRAME_MAX       8192

struct frame {
    uint32_t len;
    char     data[];
};
static struct frame *q[SYNC_QUEUE_CAP];
static int          q_head, q_tail;
static pthread_mutex_t q_mtx = PTHREAD_MUTEX_INITIALIZER;

static time_t conn_cooldown_until;

/* socketpair 当 eventfd 用:生产者戳一下,消费者协程在 nty_recv 上被唤醒 */
static int wake_r = -1, wake_w = -1;


/* ---------- slave 连接状态(只有协程碰,无锁) ---------- */

static int slave_fd = -1;
static unsigned long slave_gen;
static struct sockaddr_in slave_addr;
static unsigned long stat_fwd, stat_reconn, stat_drop;


int kvs_sync_submit(const char *data, int len){

    if(data == NULL || len <= 0 || len > FRAME_MAX) return -1;

    struct frame *fr = calloc(1, sizeof(struct frame) + len);
    if(fr == NULL) return -1;
    fr->len = len;
    memcpy(fr->data, data, len);

    pthread_mutex_lock(&q_mtx);
    if(q_tail - q_head >= SYNC_QUEUE_CAP){
        pthread_mutex_unlock(&q_mtx);
        free(fr);
        stat_drop++;
        return -1;
    }
    q[q_tail % SYNC_QUEUE_CAP] = fr;
    q_tail++;
    pthread_mutex_unlock(&q_mtx);
    send(wake_w, "x", 1, 0);
    return 0;
}

static struct frame *queue_pop(void){
    pthread_mutex_lock(&q_mtx);
    struct frame *fr = NULL;
    if(q_tail > q_head){
        fr = q[q_head % SYNC_QUEUE_CAP];
        q_head++;
    }
    pthread_mutex_unlock(&q_mtx);

    return fr;
}

static int slave_connect_co(void)
{
    if(slave_fd >= 0) nty_close(slave_fd);

    slave_fd = nty_socket(AF_INET, SOCK_STREAM, 0);
    if(slave_fd < 0) return -1;

    if(connect(slave_fd, (struct sockaddr *)&slave_addr, sizeof(slave_addr)) < 0){
        perror("[sync] connect");
        nty_close(slave_fd);
        slave_fd = -1;
        return -1;
    }
    stat_reconn++;
    slave_gen++;

    return 0;
}

static int slave_send_co(const char *buf, int len){
    if(buf == NULL || len == 0) return -1;
    if(time(NULL) < conn_cooldown_until) return -1;
    for(int retry = 0; retry < 2; retry ++){
        if(slave_fd < 0){
            if(slave_connect_co() < 0){
                continue;
            }
        }

        int off = 0;
        while(off < len){
            ssize_t n = nty_send(slave_fd, buf + off, len - off, MSG_NOSIGNAL);
            if(n <= 0){
                nty_close(slave_fd);
                slave_fd = -1;
                break;
            }
            off += n;
        }

        if(off == len) return 0;
    }
    conn_cooldown_until = time(NULL) + 1;
    return -1;
}

static void slave_writer(void *arg)
{   
    char wake_buf[64] = {0};
    while(1){
        struct frame *fr = queue_pop();
        if(!fr){
            nty_recv(wake_r, wake_buf, sizeof(wake_buf), 0);
            continue;
        }
        if(slave_send_co(fr->data, fr->len) == 0) {
            stat_fwd++;
        }
        else {
            stat_drop++;
        }
        free(fr);
    }
}

static void slave_reader(void *arg)
{
    char buf[4096] = {0};
    int fd = -1;
    unsigned long gen = 0;
    while(1){
        // if (slave_fd < 0) { usleep(100); continue; }
        if(fd != slave_fd || gen != slave_gen){
            fd = slave_fd;
            gen = slave_gen;
        }

        ssize_t n = nty_recv(fd, buf, sizeof(buf), 0);
        if(n <= 0){
            fd = -1;
            usleep(100);
        }
    }

}

int kvs_sync_start(const char *ip, int port){

    if(ip == NULL || port <= 0) return -1;

    int sp[2];
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) return -1;

    wake_r = sp[0]; wake_w = sp[1];

    int fl = fcntl(wake_w, F_GETFL, 0);
    fcntl(wake_w, F_SETFL, fl | O_NONBLOCK);

    memset(&slave_addr, 0, sizeof(slave_addr));
    slave_addr.sin_family = AF_INET;
    slave_addr.sin_port = htons(port);
    if(inet_pton(AF_INET, ip, &slave_addr.sin_addr) != 1) return -1;

    signal(SIGPIPE, SIG_IGN);

    nty_coroutine *co;
    nty_coroutine_create(&co, slave_reader, NULL);
    nty_coroutine_create(&co, slave_writer, NULL);

    return 0;
}