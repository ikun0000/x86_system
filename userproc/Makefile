CC = gcc
CFLAGS = -O0 -I ../inc/ -fno-builtin -fno-stack-protector -no-pie -m32 -c

tss.o: tss.c
	$(CC) $(CFLAGS) -o $@ $<

process.o: process.c
	$(CC) $(CFLAGS) -o $@ $<

syscall_init.o: syscall_init.c
	$(CC) $(CFLAGS) -o $@ $<

fork.o: fork.c
	$(CC) $(CFLAGS) -o $@ $<

all: tss.o process.o syscall_init.o fork.o

clean:
	rm -rf *.o
