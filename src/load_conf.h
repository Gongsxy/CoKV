#ifndef LOAD_CONF_H
#define LOAD_CONF_H

typedef struct {
    char ip[64];
    int port;
    int mode;
    int full_save;
    int add_save;
    int full_save_flag;
    int get_old_mes_flag;
    int load_old_mes;
    int push_flag;

    // sync
    char sync_ip[10];
    int is_slave;
} Config;

int load_config(const char *filename, Config *cfg);

#endif