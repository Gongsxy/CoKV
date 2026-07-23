CC = gcc

TARGET = kvstore

TARGET_B = kvstore_b

$(JEM)
SRC = kvstore.c kvs_array.c hash.c rbtree.c skiptable.c reactor.c kvs_txt.c proactor.c kvs_push.c -luring 
JEM = -L/usr/local/lib -ljemalloc 
SRC_B = kvstore_b.c kvs_array.c hash.c rbtree.c skiptable.c reactor_b.c kvs_txt_b.c  kvs_push.c -luring 
all:
	$(CC) -o $(TARGET) $(SRC) 
# b:
# 	$(CC) -o $(TARGET_B) $(SRC_B) 
clean:
	rm -rf $(TARGET) $(TARGET_B)
