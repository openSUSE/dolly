#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>

// Centralized memory allocation function
void* safe_malloc(size_t size);
void* safe_realloc(void* ptr, size_t size);
char* safe_strdup(const char *s);

#endif // UTILS_H