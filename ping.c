#include "ping.h"
#include "utils.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int is_host_reachable(const char *hostname) {
  if (!is_valid_hostname(hostname)) {
      fprintf(stderr, "Invalid hostname detected: %s\n", hostname);
      return 0;
  }
  char command[256];
  int result;
  // system command but better than creating a raw socket as root to ping...
  snprintf(command, sizeof(command), "ping -c 1 -W 1 %s > /dev/null 2>&1", hostname);
  result = system(command);
  if (result == 0) {
    return 1; // Host is reachable
  } else {
    return 0; // Host is not reachable
  }
}

int is_port_open(const char *hostname, int port) {
    int sock;
    struct sockaddr_in addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 0; // Could not create socket
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, hostname, &addr.sin_addr) <= 0) {
        close(sock);
        return 0; // Invalid address
    }

    // Set a timeout for the connection attempt
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return 0; // Connection failed
    }

    close(sock);
    return 1; // Connection successful
}
