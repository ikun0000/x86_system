CC = gcc
CFLAGS = -O0 -I ../inc/ -fno-builtin -fno-stack-protector -no-pie -m32 -c

print.o: kern/print.S
	nasm -f elf32 -o $@ $< 

string.o: string.c
	$(CC) $(CFLAGS) -o $@ $<

bitmap.o: kern/bitmap.c
	$(CC) $(CFLAGS) -o $@ $<

list.o: kern/list.c
	$(CC) $(CFLAGS) -o $@ $<

syscall.o: user/syscall.c
	$(CC) $(CFLAGS) -o $@ $<

stdio.o: stdio.c
	$(CC) $(CFLAGS) -o $@ $<

stdio_kern.o: kern/stdio_kern.c
	$(CC) $(CFLAGS) -o $@ $<

assert.o: user/assert.c
	$(CC) $(CFLAGS) -o $@ $<

all: print.o string.o bitmap.o list.o syscall.o stdio.o stdio_kern.o assert.o

clean:	
	rm -rf *.o
