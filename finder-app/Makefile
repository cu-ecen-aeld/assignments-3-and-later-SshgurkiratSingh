all: CROSS_COMPILE
deafult:
	@gcc writer.c -o writer
	@file writer
CROSS_COMPILE:
	@aarch64-none-linux-gnu-gcc writer.c -o writer

clean:
	@rm -f writer