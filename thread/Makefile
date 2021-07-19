CC = gcc
CFLAGS = -O0 -I ../inc/ -fno-builtin -fno-stack-protector -no-pie -m32 -c

thread.o: thread.c
	$(CC) $(CFLAGS) -o $@ $<

switch.o: switch.S
	nasm -f elf32 -o $@ $<

sync.o: sync.c
	$(CC) $(CFLAGS) -o $@ $<

all: thread.o switch.o sync.o

clean:
	rm -rf *.o

