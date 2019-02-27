CC=gcc
CFLAGS=-Wall -g -O

all: dolly

dolly: dolly.c

clean:
	rm -f dolly *.o
