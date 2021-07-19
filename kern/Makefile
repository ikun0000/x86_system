CC = gcc
CFLAGS = -O0 -I ../inc/ -fno-builtin -fno-stack-protector -no-pie -m32 -c

main.o: main.c
	$(CC) $(CFLAGS) -o $@ $< 

intr_entry.o: intr_entry.S
	nasm -f elf32 -o $@ $<

interrupt.o: interrupt.c
	$(CC) $(CFLAGS) -o $@ $<

init.o: init.c
	$(CC) $(CFLAGS) -o $@ $<

debug.o: debug.c
	$(CC) $(CFLAGS) -o $@ $<

memory.o: memory.c
	$(CC) $(CFLAGS) -o $@ $<

all: main.o intr_entry.o interrupt.o init.o debug.o memory.o

clean:
	rm -rf *.o
