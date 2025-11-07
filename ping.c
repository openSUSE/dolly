#include "ping.h"

int is_host_reachable(const char *hostname) {
  char command[256];
  int result;
  snprintf(command, sizeof(command), "ping -c 1 -W 1 %s > /dev/null 2>&1", hostname);
  result = system(command);
  if (result == 0) {
    return 1; // Host is reachable
  } else {
    return 0; // Host is not reachable
  }
}
