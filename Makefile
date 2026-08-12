CC = gcc

SRC_DIR = src
OBJ_DIR = build
BIN = kvstore 
OUTPUT_DIR = ./build

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
TARGET = kvstore
all:
	$(CC) -g -I$(SRC_DIR) -o $(TARGET) $(SOURCES) -luring

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
	rm -rf $(TARGET)
