CXX = g++
CC = gcc
CFLAGS = -std=gnu11 -s -O -fstack-protector-strong
WARNINGS = -Werror -Wall -Wextra -pedantic-errors 
LDFLAGS =
LIBRARIES =
BUILD_DIR = ./build
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:%.c=$(BUILD_DIR)/%.o)
DEPS = $(SOURCES:%.c=$(BUILD_DIR)/%.d)
# Can 
DEBUGFLAGS=-ggdb


EXECUTABLE = dolly

all: $(EXECUTABLE)

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

-include $(DEPS)
