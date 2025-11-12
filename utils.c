#include "utils.h"

// Centralized memory allocation function
void* safe_malloc(size_t size) {
    void* ptr = calloc(1, size);
    if (!ptr) {
        fprintf(stderr, "Memory allocation failed at %s:%d\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }
    return ptr;
}