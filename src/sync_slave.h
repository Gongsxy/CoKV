#ifndef SYNC_SLAVE_H
#define SYNC_SLAVE_H

/* 生产者(forwarder 的 BPF 线程)调用:拷一帧写命令入队,立即返回 */
int  kvs_sync_submit(const char *frame, int len);
/* 在 nty_schedule_run() 之前调用:建协程、初始化队列和唤醒通道 */
int  kvs_sync_start(const char *slave_ip, int slave_port);

#endif