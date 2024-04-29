default : ## build writer
	gcc -o writer writer.c

clean : ## remove writer and all *.o files
	@if [ -f writer ]; then\
		rm writer;\
	fi
	@if [ -f *.o ]; then\
		rm *.o;\
	fi
CROSS_COMPILE :  ## cross compile writer
	export PATH=$PATH:$(pwd)/../gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin
	aarch64-none-linux-gnu-gcc finder-app/writer.c -o finder-app/writer