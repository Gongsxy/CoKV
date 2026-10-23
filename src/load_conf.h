#ifndef LOAD_CONF_H
#define LOAD_CONF_H

typedef struct {
    char ip[64];
    int port;
    int show_response;
    int use_mempool;
    
    // app
    int full_save;
    int add_save;
    char storage_structure[64];
    // sync
    char sync_ip[10];
    int is_slave;
    int method;
} Config;

int load_config(const char *filename, Config *cfg);

#endif