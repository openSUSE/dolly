#ifndef DOLLY_H
#define DOLLY_H
static const char version_string[] = "0.63.6, 13-OCT-2021";

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

/* File descriptors for file I/O */
extern int input, output;

/* Numbers for splitted input/output files */
extern unsigned int input_nr, output_nr;


/* size of buffers for TCP sockets (approx. 100KB, multiple of 4 KB) */
extern unsigned int buffer_size ;

#define SCKBUFSIZE buffer_size
/* Normal sockets for data transfer */
extern int datain[MAXFANOUT], dataout[MAXFANOUT];
extern int datasock;

/* Special sockets for control information */
extern int ctrlin, ctrlout[MAXFANOUT];
extern int ctrlsock;

extern unsigned long long maxbytes; /* max bytes to transfer */
extern unsigned long long maxcbytes;/*     --  "  --  in compressed mode */
extern int dosync;                  /* sync() after transfer */
extern int timeout;                 /* Timeout for startup */
extern int verbignoresignals;       /* warn on ignore signal errors */

extern int max_retries;
extern char dollytab[256];



extern int flag_log;
extern char logfile[256];


/* Pipe descriptor in case data must be uncompressed before write */
extern int pd[2];
/* Pipe descriptor in case input data must be compressed */
extern int id[2]; 

/* PIDs of child processes */
extern int in_child_pid, out_child_pid;

#endif
