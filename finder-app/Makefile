# Cross-compiler prefix (allow Buildroot to override)
CROSS_COMPILE ?= aarch64-none-linux-gnu-

# Compiler and flags
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -O2

# Target executable
TARGET = writer

.PHONY: all clean

all: $(TARGET)

$(TARGET): writer.c
	$(CC) $(CFLAGS) -o $(TARGET) writer.c

clean:
	rm -f $(TARGET)