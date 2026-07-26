CC = gcc

TARGET = kvstore

$(JEM)
SRC = kvstore.c kvs_array.c hash.c rbtree.c skiptable.c reactor.c kvs_txt.c proactor.c kvs_push.c mempool.c -luring
JEM = -L/usr/local/lib -ljemalloc 
all:
	$(CC) -o $(TARGET) $(SRC) 
# b:
# 	$(CC) -o $(TARGET_B) $(SRC_B) 
clean:
	rm -rf $(TARGET) $(TARGET_B)
