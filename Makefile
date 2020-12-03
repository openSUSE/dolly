CXX = g++
CC = gcc
CFLAGS = -std=gnu11 -s -O
WARNINGS = -Werror -Wall -Wextra -pedantic-errors 
LDFLAGS =
LIBRARIES =
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)
DEPS = $(SOURCES:.c=.d)

EXECUTABLE = dolly

%.d: %.c
	$(CC) $< -o $@ -MM $(CFLAGS)

%.o: %.c
	$(CC) $< -o $@ -c $(CFLAGS) $(WARNINGS)


$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ $(LIBRARIES)

all: $(EXECUTABLE)

-include $(DEPS)

.PHONY: clean       
clean:
	rm -rf $(EXECUTABLE) $(OBJECTS) $(DEPS)

