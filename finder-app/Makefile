# Cross-compiler prefix (provided by Buildroot)
CROSS_COMPILE ?= aarch64-none-linux-gnu-

# Compiler and flags
CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -O2

# Target executable
TARGET := writer

all: $(TARGET)

$(TARGET): writer.c
    $(CC) $(CFLAGS) -o $(TARGET) writer.c

clean:
    rm -f $(TARGET)