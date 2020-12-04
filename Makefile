CXX = g++
CC = gcc
CFLAGS = -std=gnu11 -s -O
WARNINGS = -Werror -Wall -Wextra -pedantic-errors 
LDFLAGS =
LIBRARIES =
BUILD_DIR = ./build
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:%.c=$(BUILD_DIR)/%.o)
DEPS = $(SOURCES:.c=.d)

EXECUTABLE = dolly

%$(BUILD_DIR)/.d: %.c
	mkdir -p $(dir $@)
	$(CC) $< -o $@ -MM $(CFLAGS)

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $< -o $@ -c $(CFLAGS) $(WARNINGS)


$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ $(LIBRARIES)

all: $(EXECUTABLE)

-include $(DEPS)

.PHONY: clean       
clean:
	rm -rf $(EXECUTABLE) $(OBJECTS) $(DEPS)

