CC = gcc
CFLAGS = -O0 -I ../inc/ -no-pie -fno-stack-protector -fno-builtin -m32 -c

timer.o: timer.c
	$(CC) $(CFLAGS) -o $@ $< 

console.o: console.c
	$(CC) $(CFLAGS) -o $@ $<

keyboard.o: keyboard.c
	$(CC) $(CFLAGS) -o $@ $<

ioqueue.o: ioqueue.c
	$(CC) $(CFLAGS) -o $@ $<

ide.o: ide.c
	$(CC) $(CFLAGS) -o $@ $<

all: timer.o console.o keyboard.o ioqueue.o ide.o

clean:
	rm -rf *.o
