#include "utils.h"
#include <string.h>

// Centralized memory allocation function
void* safe_malloc(size_t size) {
    void* ptr = calloc(1, size);
    if (!ptr) {
        fprintf(stderr, "Memory allocation failed at %s:%d\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void* safe_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "Memory reallocation failed at %s:%d\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }
    return new_ptr;
}

char* safe_strdup(const char *s) {
    char *p = strdup(s);
    if (!p) {
        fprintf(stderr, "strdup failed\n");
        exit(EXIT_FAILURE);
    }
    return p;
}