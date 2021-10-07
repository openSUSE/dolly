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
VERSION=0.63.4


EXECUTABLE = dolly

all: $(EXECUTABLE)

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
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ $(LIBRARIES)

.PHONY: clean       

clean:
	rm -rf $(EXECUTABLE) $(OBJECTS) $(DEPS)
	rm -rf $(EXECUTABLE)-$(VERSION) $(EXECUTABLE)-$(VERSION).tar.bz2

-include $(DEPS)
