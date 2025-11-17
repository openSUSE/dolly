#ifndef PING_H
#define PING_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int is_host_reachable(const char *hostname);
int is_port_open(const char *hostname, int port);

#endif /* PING_H */