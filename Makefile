CC = gcc

SRC_DIR = src
SRC_SLAVE = slave
SRC_NtyCo = NtyCo/core
OBJ_DIR = build
BIN = kvstore 
OUTPUT_DIR = ./build

SOURCES = $(wildcard $(SRC_DIR)/*.c)
SOURCES_SLAVE = $(wildcard $(SRC_SLAVE)/*.c)
SOURCES_Ntyco = $(wildcard $(SRC_NtyCo)/*.c)
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
TARGET = kvstore

TARGET_ADD = testcase_add
TARGET_FULL = testcase_full
TARGET_SLAVE = testcase_slave
TARGET_SAVE = testcase_save

TARGET_SERVER_SLAVE = kvstore_slave

SRC_ADD = ./testcase/testcase_save_add.c  src/mempool.c src/get_mes.c src/load_conf.c
SRC_FULL = ./testcase/testcase_save_full.c  src/mempool.c src/get_mes.c src/load_conf.c
SRC_T_SLAVE = ./testcase/testcase_save_slave.c  src/mempool.c src/get_mes.c src/load_conf.c
SRC_SAVE = ./testcase/testcase_save.c  src/mempool.c src/get_mes.c src/load_conf.c
JEM = ./testcase/testcase_stdin.c src/mempool.c

# SRC_SERVER_SLAVE = slave/

all:
	$(CC) -g -I$(SRC_DIR) -o $(TARGET) $(SOURCES) $(SOURCES_Ntyco) -luring -pthread -ldl

testcase_add:
	$(CC) -o $(TARGET_ADD) $(SRC_ADD)

testcase_full:
	$(CC) -o $(TARGET_FULL) $(SRC_FULL)

testcase_slave:
	$(CC) -g -o $(TARGET_SLAVE) $(SRC_T_SLAVE)
	
testcase_save:
	$(CC) -g -o $(TARGET_SAVE) $(SRC_SAVE)


server_slave:
	$(CC) -I$(SRC_SLAVE) -o $(TARGET_SERVER_SLAVE) $(SOURCES_SLAVE) -luring
# TARGET = kvstore
# -mcmodel=medium
# $(JEM)
# SRC = kvstore.c kvs_array.c hash.c rbtree.c skiptable.c reactor.c kvs_txt.c proactor.c kvs_push.c mempool.c testcase/get_mes.c -luring 
# JEM = -L/usr/local/lib -ljemalloc 
# all:
# 	$(CC) -g -o build/$(TARGET) $(SRC)  
# # b:
# # 	$(CC) -o $(TARGET_B) $(SRC_B) 
clean:
	rm -rf $(TARGET) $(TARGET_ADD) $(TARGET_FULL) $(TARGET_SLAVE) $(TARGET_S) $(TARGET_SAVE)
