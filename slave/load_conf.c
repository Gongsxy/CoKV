

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
    cfg->mode = 1;

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

                if(strcmp(mes, "mode") == 0){
                    cfg->mode = atoi(val);
                }
                if(strcmp(mes, "full_save") == 0){
                    cfg->full_save = atoi(val);
                }
                if(strcmp(mes, "add_save") == 0){
                    cfg->add_save = atoi(val);
                }
                if(strcmp(mes, "full_save_flag") == 0){
                    cfg->full_save_flag = atoi(val);
                }
                if(strcmp(mes, "get_old_mes_flag") == 0){
                    cfg->get_old_mes_flag = atoi(val);
                }
                if(strcmp(mes, "load_old_mes") == 0){
                    cfg->load_old_mes = atoi(val);
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
                    strncpy(cfg->sync_ip, val, strlen(val));
                    cfg->sync_ip[strlen(val)] = '\0';
                    i++;
                }
                if(strcmp(mes, "push_flag") == 0){
                    cfg->push_flag = atoi(val);
                }
                if(strcmp(mes, "is_salve") == 0){
                    cfg->is_slave = atoi(val);
                }
                if(strcmp(mes, "push_save_flag") == 0){
                    cfg->push_save_flag = atoi(val);
                }
                
            }
        }
        memset(line, 0, 128);
    }
    fclose(fp);
    return 0;
}