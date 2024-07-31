.DEFAULT_GOAL:=aesdsocket
CC ?= $(CROSS_COMPILE)gcc
all: aesdsocket

aesdsocket: aesdsocket.o
	$(CC)  -o aesdsocket aesdsocket.o -pthread -lrt -Wl,--hash-style=gnu

aesdsocket.o: aesdsocket.c
	$(CC) -o aesdsocket.o aesdsocket.c -c -std=gnu99 -W -Wall -pedantic 

clean:
	rm -rf *.o *~ aesdsocket 