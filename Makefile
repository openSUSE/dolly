VERSION=0.60
CC=gcc
CFLAGS=-Wall -ggdb -O0 -Wextra -pedantic-errors

all: dolly

dolly: dolly.c

clean:
	rm -f dolly *.o

tarball:
	mkdir -p ../dolly-${VERSION}
	cp -a * ../dolly-${VERSION}
	cd .. && tar cvfj dolly-${VERSION}.tar.bz2 dolly-${VERSION}
