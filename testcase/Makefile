CC = gcc

SRC_DIR = ../src
OBJ_DIR = build

TARGET_ADD = testcase_save_add
TARGET_FULL = testcase_save_full
TARGET_S = testcase_stdin

SOURCES = $(wildcard $(SRC_DIR)/*.c)

$(JEM)
SRC_ADD = testcase_save_add.c  mempool.c ../src/get_mes.c ../src/load_conf.c
SRC_FULL = testcase_save_full.c  mempool.c ../src/get_mes.c ../src/load_conf.c
JEM = testcase_stdin.c mempool.c 
LDFLAGS = -L .
LDLIBS = -ltestcase
add:
	$(CC) -o $(TARGET_ADD) $(SRC_ADD)

full:
	$(CC) -o $(TARGET_FULL) $(SRC_FULL)

stdin:
	$(CC) -o $(TARGET_S) $(JEM) 
# b:
# 	$(CC) -o $(TARGET_B) $(SRC_B) 
clean:
	rm -rf $(TARGET_ADD) $(TARGET_FULL)