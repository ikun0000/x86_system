CC = gcc
CFLAGS = -O0 -I ../inc/ -fno-builtin -fno-stack-protector -no-pie -m32 -c

shell.o: shell.c
	$(CC) $(CFLAGS) -o $@ $<

buildin_cmd.o: buildin_cmd.c
	$(CC) $(CFLAGS) -o $@ $<

pipe.o: pipe.c
	$(CC) $(CFLAGS) -o $@ $<

all: shell.o buildin_cmd.o pipe.o

clean:
	rm -rf *.o
