#ifndef DOLLY_H
#define DOLLY_H
extern const char version_string[];

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
#include "transmit.h"

#define DOLLY_NONBLOCK 1                 /* Use nonblocking network sockets */
#define WRITE 1
#define READ 2

extern FILE *stdtty;           /* file pointer to the standard terminal tty */


/* Clients need the ports before they can listen, so we use defaults. */
extern const unsigned int dataport;
extern const unsigned int ctrlport;
extern const char* host_delim;

void print_params(struct dollytab* mydollytab);


#endif
