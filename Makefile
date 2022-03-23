CXX = g++
CC = gcc
CFLAGS += -std=gnu11 -s -O -fPIE $(RPM_OPT_FLAGS)
WARNINGS = -Werror -Wall -Wextra -pedantic-errors
LDFLAGS =
LIBRARIES =
BUILD_DIR = ./build
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:%.c=$(BUILD_DIR)/%.o)
DEPS = $(SOURCES:%.c=$(BUILD_DIR)/%.d)
# Can 
DEBUGFLAGS=-ggdb
VERSION=0.64.0

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
SYSTEMDDIR ?= $(PREFIX)/lib/systemd/system
DATADIR ?= $(PREFIX)/share
MANDIR ?= $(DATADIR)/man

EXECUTABLE = dolly

all: $(EXECUTABLE) man

tar: clean
	mkdir $(EXECUTABLE)-$(VERSION)
	find . -maxdepth 1 -type f -exec cp -a {} $(EXECUTABLE)-$(VERSION) \;
	tar cfj $(EXECUTABLE)-$(VERSION).tar.bz2 $(EXECUTABLE)-$(VERSION)
	rm -rf $(EXECUTABLE)-$(VERSION)

$(BUILD_DIR)/%.d: %.c
	@mkdir -p $(dir $@)
	$(CC) $< -MT $(BUILD_DIR)/$*.o -MM -MF $(BUILD_DIR)/$*.d

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $< -o $@ -c $(CFLAGS) $(DEBUGFLAGS) $(WARNINGS)


$(EXECUTABLE): $(OBJECTS)
	@echo "Building $(EXECUTABLE)"
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ $(LIBRARIES)

.PHONY: clean       

clean:
	rm -rf $(EXECUTABLE) $(OBJECTS) $(DEPS)
	rm -rf $(EXECUTABLE)-$(VERSION) $(EXECUTABLE)-$(VERSION).tar.bz2
	rm -rf dolly.1*

dolly.1.gz:
	@echo "Building man page"
	pandoc --standalone -t man README.md -o dolly.1
	gzip dolly.1

man: dolly.1.gz

install:
	echo $(PREFIX)
	install -d -m 0755 $(BINDIR)
	install -d -m 0755 $(SYSTEMDDIR)
	install -m 0755 $(EXECUTABLE) $(BINDIR)
	install -m 0644 dolly.service $(SYSTEMDDIR)
	install -m 0644 dolly.socket $(SYSTEMDDIR)
	-test -e dolly.1.gz && install -d -m 0755 $(MANDIR)/man1
	-test -e dolly.1.gz && install -m 0644 dolly.1.gz $(MANDIR)/man1/

-include $(DEPS)
