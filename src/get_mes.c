
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int get_mes(){

    FILE *fp = fopen("sync.log", "r");
    printf("open");
    if(fp == NULL) return -1;
	FILE *fp_write = fopen("testcase/test.txt", "a+");
    if(fp_write == NULL) {
        fclose(fp);
        return -2;
    }
    printf("open success\n");

    char buf[512] = {0};
    char cmd[256] = {0};
    char key[256] = {0};
    char value[256] = {0};
    int has_cmd = 0, has_key = 0, has_value = 0;
    while(fgets(buf,sizeof(buf),fp)){
        if(strncmp(buf, "cmd:",4) == 0){
            sscanf(buf,"cmd:%255[^\r\n]",cmd);
            has_cmd = 1;
        }else if(strncmp(buf, "key:",4) == 0){
            sscanf(buf,"key:%255[^\r\n]",key);
            has_key = 1;
        }else if(strncmp(buf, "value:",6) == 0){
            sscanf(buf,"value:%255[^\r\n]",value);
            has_value = 1;
        }

        if(has_cmd && has_key && has_value){
            fprintf(fp_write, "cmd:%s\r\nkey:%s\r\nvalue:%s\r\n", cmd, key, value);
            fflush(fp_write);
            cmd[0] = key[0] = value[0] = '\0';
            has_cmd = has_key = has_value = 0;
        }
    }
	fclose(fp);
    fclose(fp_write);

}