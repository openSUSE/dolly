VERSION=0.59
CC=gcc
CFLAGS=-Wall -ggdb -O2

all: dolly

dolly: dolly.c

clean:
	rm -f dolly *.o

tarball:
	mkdir -p ../dolly-${VERSION}
	cp -a * ../dolly-${VERSION}
	cd .. && tar cvfj dolly-${VERSION}.tar.bz2 dolly-${VERSION}
