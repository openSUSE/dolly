#ifndef DOLLY_H
#define DOLLY_H
static const char version_string[] = "0.62, 28-JAN-2020";

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <netinet/tcp.h>
#include <assert.h>
#include <ctype.h>
#include <signal.h>

#include "dollytab.h"

#define DOLLY_NONBLOCK 1                 /* Use nonblocking network sockets */
/* Normal sockets for data transfer */
extern int datain[MAXFANOUT], dataout[MAXFANOUT];
extern int datasock;

/* Special sockets for control information */
extern int ctrlin, ctrlout[MAXFANOUT];
extern int ctrlsock;
#endif
