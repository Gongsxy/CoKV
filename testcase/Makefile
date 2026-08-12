CC = gcc

SRC_DIR = ../src
OBJ_DIR = build

TARGET = testcase_save_add
TARGET_S = testcase_stdin

SOURCES = $(wildcard $(SRC_DIR)/*.c)

$(JEM)
SRC = testcase_save_add.c  mempool.c ../src/get_mes.c ../src/load_conf.c
JEM = testcase_stdin.c mempool.c 
LDFLAGS = -L .
LDLIBS = -ltestcase
auto:
	$(CC) -o $(TARGET) $(SRC)

stdin:
	$(CC) -o $(TARGET_S) $(JEM) 
# b:
# 	$(CC) -o $(TARGET_B) $(SRC_B) 
clean:
	rm -rf $(TARGET) $(TARGET_B)