CC = gcc
CLANG   = clang-14
BPFTOOL = bpftool


SRC_DIR = src
SRC_STORAGE = storage
SRC_SLAVE = slave
SRC_NtyCo = NtyCo/core
OBJ_DIR = build
BIN = kvstore 
OUTPUT_DIR = ./build




SOURCES = $(wildcard $(SRC_DIR)/*.c)
SOURCES_STORAGE = $(wildcard $(SRC_STORAGE)/*.c)
SOURCES_SLAVE = $(wildcard $(SRC_SLAVE)/*.c)
SOURCES_Ntyco = $(wildcard $(SRC_NtyCo)/*.c)

SOURCES_T := $(filter-out $(SRC_DIR)/proactor.c, $(SOURCES))
SOURCES_T_SLAVE := $(filter-out $ $(SRC_SLAVE)/rbtree.c $(SRC_SLAVE)/kvs_push.c $(SRC_SLAVE)/reactor_b.c $(SRC_SLAVE)/proactor.c, $(SOURCES_SLAVE))
SOURCES_TEST := $(filter-out $ $(SRC_DIR)/ntyco.c $(SRC_DIR)/proactor.c $(SRC_DIR)/reactor.c,  $(SOURCES))



OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
TARGET = kvstore

TARGET_ADD = testcase_add
TARGET_FULL = testcase_full
TARGET_SLAVE = testcase_slave
TARGET_SAVE = testcase_characters

TARGET_SERVER_SLAVE = kvstore_slave

SRC_ADD = ./testcase/testcase_save_add.c  src/mem_pool.c  src/load_conf.c
SRC_FULL = ./testcase/testcase_save_full.c  src/mem_pool.c  src/load_conf.c
SRC_T_SLAVE = ./testcase/testcase_save_slave.c  src/mem_pool.c  src/load_conf.c
SRC_SAVE = ./testcase/testcase_characters.c  src/mem_pool.c  src/load_conf.c
JEM = ./testcase/testcase_stdin.c src/mem_pool.c

# SRC_SERVER_SLAVE = slave/

CFLAGS = -g -O2 -Wall -I$(SRC_DIR) -I$(SRC_STORAGE) $(EXTRA)

# -ljemalloc
#libbpf-bootstrap/examples/c/.output/resp_capture.o libbpf-bootstrap/examples/c/.output/libbpf.a

USE_JEMALLOC := $(shell grep -qE '^\#define[[:space:]]+USE_JEMALLOC[[:space:]]+1' $(SRC_STORAGE)/alloc_conf.h && echo 1)

LIB = -luring -ldl -lelf -lz -lpthread -libverbs -lsodium
ifeq ($(USE_JEMALLOC),1)
LIB += -ljemalloc
endif

all:
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES_T) $(SOURCES_STORAGE) libbpf-bootstrap/examples/c/.output/resp_capture.o libbpf-bootstrap/examples/c/.output/libbpf.a $(SOURCES_Ntyco) $(LIB)


testcase_add:
	$(CC) -o $(TARGET_ADD) $(SRC_ADD)

testcase_full:
	$(CC) -o $(TARGET_FULL) $(SRC_FULL)

testcase_slave:
	$(CC) -g -o $(TARGET_SLAVE) $(SRC_T_SLAVE)
	
testcase_characters:
	$(CC) -g -o $(TARGET_SAVE) $(SRC_SAVE) -lhiredis


server_slave:
	$(CC) -I$(SRC_SLAVE) -I$(SRC_STORAGE) -o $(TARGET_SERVER_SLAVE) $(SOURCES_STORAGE) $(SOURCES_T_SLAVE) $(SOURCES_Ntyco) src/rdma.c -luring -libverbs -pthread -ldl -lsodium -lz $(if $(filter 1,$(USE_JEMALLOC)),-ljemalloc)

# $(SOURCES_TEST)
test:
	$(CC) -g -o test src/test.c src/sync_slave.c $(SOURCES_Ntyco) -libverbs -luring -pthread -ldl


clean:
	rm -rf $(TARGET) $(TARGET_ADD) $(TARGET_FULL) $(TARGET_SLAVE) $(TARGET_SERVER_SLAVE) $(TARGET_SAVE) test
