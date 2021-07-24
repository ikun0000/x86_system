CC = gcc
CFLAGS = -O0 -I ../inc/ -fno-builtin -fno-stack-protector -no-pie -m32 -c

fs.o: fs.c
	$(CC) $(CFLAGS) -o $@ $<   

inode.o: inode.c
	$(CC) $(CFLAGS) -o $@ $<   

file.o: file.c
	$(CC) $(CFLAGS) -o $@ $<   

dir.o: dir.c
	$(CC) $(CFLAGS) -o $@ $<   

all: fs.o inode.c file.o dir.o

clean: 
	rm -rf *.o
