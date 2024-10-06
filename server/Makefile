CC ?= ${CROSS_COMPILE}gcc
CFLAGS ?= -g -Wall -Werror
CFLAGS += -DUSE_AESD_CHAR_DEVICE=1
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt

default: all

all: $(TARGET)

$(TARGET): aesdsocket.c
	${CC} $(CFLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(TARGET) *.o