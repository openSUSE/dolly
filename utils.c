#include "utils.h"
#include <string.h>
#include <ctype.h>

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

char** expand_host_range(const char* host_str, int* count) {
  char** host_list = NULL;
  *count = 0;

  char* str = strdup(host_str);
  char* token = strtok(str, ",");

  while (token) {
    char* range_hyphen = strchr(token, '-');
    if (range_hyphen) {
      // IP range detected
      char* ip_prefix = strdup(token);
      ip_prefix[range_hyphen - token] = '\0';

      char* last_dot = strrchr(ip_prefix, '.');
      if (last_dot) {
	int start_range = atoi(last_dot + 1);
	int end_range = atoi(range_hyphen + 1);
                
	char prefix_base[256];
	strncpy(prefix_base, ip_prefix, last_dot - ip_prefix + 1);
	prefix_base[last_dot - ip_prefix + 1] = '\0';

	for (int i = start_range; i <= end_range; ++i) {
	  host_list = (char**)safe_realloc(host_list, (*count + 1) * sizeof(char*));
	  char ip[256];
	  snprintf(ip, sizeof(ip), "%s%d", prefix_base, i);
	  host_list[*count] = strdup(ip);
	  (*count)++;
	}
      }
      free(ip_prefix);
    } else {
      // Single IP
      host_list = (char**)safe_realloc(host_list, (*count + 1) * sizeof(char*));
      host_list[*count] = strdup(token);
      (*count)++;
    }
    token = strtok(NULL, ",");
  }

  free(str);
  return host_list;
}
