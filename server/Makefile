# Makefile for assignment5

#CROSS_COMPILE=aarch64-none-linux-gnu-
#ARCH=arm64
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS ?= -lpthread -lrt
TARGET ?= aesdsocket
SRC := $(TARGET).c
OBJ := $(SRC:.c=.o)

ifeq ($(ARCH), arm64)
	CFLAGS += -g
endif

.PHONY: all install clean

all: $(TARGET)

#rule for compiling .c files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ -pthread
	
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(INCLUDES) $(LDFLAGS)

#$(TARGET): $(TARGET).c
#	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).c

#rule for install software
install: $(TARGET)
	cp $(TARGET) $(OUT_DIR)

clean:
	rm -f $(TARGET) $(OBJ)