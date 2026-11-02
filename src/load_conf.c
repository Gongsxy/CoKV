

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "load_conf.h"

enum {
    server,
    app,
    sync
};

int load_config(const char *filename, Config *cfg){

    FILE *fp = fopen(filename, "r");
    if(!fp) return -1;
   
    strcpy(cfg->ip, "0.0.0.0");
    cfg->port = 2000;             

    int status = -1;
    char line[128] = {0};
    int i = 0;
    while(fgets(line, 127, fp)){
        
        line[strcspn(line, "\r\n")] = '\0';

        if(strcmp(line, "[server]") == 0){
            status = server;
            continue;
        }

        if(strcmp(line, "[app]") == 0){
            status = app;
            continue;
        }

        if(strcmp(line, "[SYNC]") == 0){
            status = sync;
            continue;
        }
        
        switch(status){
            case server:{
                char *mes = strtok(line, "=");
                if (!mes) break;

                char *k = mes + strlen(mes) - 1;
                while (k > mes && *k == ' ') *k-- = '\0';

                char *val = strtok(NULL, "=");
                if (!val) break;
                while (*val == ' ') val++;

                if(strcmp(mes, "ip") == 0){
                    strncpy(cfg->ip, val, sizeof(cfg->ip) - 1);
                    cfg->ip[sizeof(cfg->ip) - 1] = '\0';
                }
                if(strcmp(mes, "port") == 0){
                    cfg->port = atoi(val);
                }
                if(strcmp(mes, "show_response") == 0){
                    cfg->show_response = atoi(val);
                }
                if(strcmp(mes, "use_mempool") == 0){
                    cfg->use_mempool = atoi(val);
                }
                break;
            }
            case app:{
                char *mes = strtok(line, "=");
                if (!mes) break;    

                char *k = mes + strlen(mes) - 1;
                while (k > mes && *k == ' ') *k-- = '\0';

                char *val = strtok(NULL, "=");
                if (!val) break;
                while (*val == ' ') val++;

                if(strcmp(mes, "full_save") == 0){
                    cfg->full_save = atoi(val);
                }
                if(strcmp(mes, "add_save") == 0){
                    cfg->add_save = atoi(val);
                }
                if(strcmp(mes, "storage_structure") == 0){
                    strncpy(cfg->storage_structure, val, sizeof(cfg->storage_structure) - 1);
                    cfg->storage_structure[strcspn(cfg->storage_structure, "\r\n")] = '\0';
                }
                break;
            }
            case sync:{
                char *mes = strtok(line, "=");
                if (!mes) break;    

                char *k = mes + strlen(mes) - 1;
                while (k > mes && *k == ' ') *k-- = '\0';

                char *val = strtok(NULL, "=");
                if (!val) break;
                while (*val == ' ') val++;

                if(strcmp(mes, "sync_ip") == 0){
                    size_t n = strlen(val);
                    while (n > 0 && (val[n-1] == ' ' || val[n-1] == '\t' || val[n-1] == '\r' || val[n-1] == '\n'))
                        n--;
                    snprintf(cfg->sync_ip, sizeof(cfg->sync_ip), "%.*s", (int)n, val);
                }
                if(strcmp(mes, "is_slave") == 0){
                    cfg->is_slave = atoi(val);
                }
                
            }
        }
        memset(line, 0, 128);
    }
    fclose(fp);
    return 0;
}