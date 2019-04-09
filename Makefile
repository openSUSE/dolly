VERSION=0.58C
CC=gcc
CFLAGS=-Wall -g -O3

all: dolly

dolly: dolly.c

clean:
	rm -f dolly *.o

tarball:
	mkdir -p ../dolly-${VERSION}
	cp -a * ../dolly-${VERSION}
	cd .. && tar cvfj dolly-${VERSION}.tar.bz2 dolly-${VERSION}
