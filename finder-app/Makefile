all: CROSS_COMPILE
deafult:
	@gcc writer.c -o writer
	@file writer
CROSS_COMPILE:
	export PATH=$PATH:$/home/gurkirat/Documents/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin
	aarch64-none-linux-gnu-gcc finder-app/writer.c -o finder-app/writer

	
clean:
	@rm -f writer