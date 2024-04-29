# Compiler settings
CC = gcc
CROSS_COMPILE ?=
TARGET = writer

# Source files
SRCS = writer.c
OBJS = $(SRCS:.c=.o)

# Compiler flags
CFLAGS = -Wall -Wextra -std=c99
LDFLAGS = -lm  # Add any necessary linker flags here

# Cross-compiler settings
ifeq ($(filter aarch64-none-linux-gnu-%,$(CROSS_COMPILE)),)
# Native build
CROSS_PREFIX =
else
# Cross-compilation
CROSS_PREFIX = $(CROSS_COMPILE)
endif

# Default target: build the writer application
$(TARGET): $(OBJS)
	$(CROSS_PREFIX)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# Compile .c files to .o files
%.o: %.c
	$(CROSS_PREFIX)$(CC) $(CFLAGS) -c -o $@ $<

# Clean target: remove generated files
clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: clean
