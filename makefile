CC=gcc

all: s c

s: server.c
	$(CC) -o s server.c

c: client.c
	$(CC) -o c client.c

clean:
	rm -f s c
