#ifndef DOLLY_H
#define DOLLY_H
static const char version_string[] = "0.61, 22-JAN-2020";

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

static FILE *stdtty;           /* file pointer to the standard terminal tty */


/* Clients need the ports before they can listen, so we use defaults. */
static unsigned int dataport = 9998;
static unsigned int ctrlport = 9997;

/* File descriptors for file I/O */
static int input = -1, output = -1;


/* Numbers for splitted input/output files */
static unsigned int input_nr = 0, output_nr = 0;


/* size of buffers for TCP sockets (approx. 100KB, multiple of 4 KB) */
static int buffer_size = 98304;
#define SCKBUFSIZE buffer_size


/* Normal sockets for data transfer */
static int datain[MAXFANOUT], dataout[MAXFANOUT];
static int datasock = -1;

/* Special sockets for control information */
static int ctrlin = -1, ctrlout[MAXFANOUT];
static int ctrlsock = -1;

static unsigned long long maxbytes = 0; /* max bytes to transfer */
static unsigned long long maxcbytes = 0;/*     --  "  --  in compressed mode */
static int dosync = 1;                  /* sync() after transfer */
static unsigned int dummy_time = 0;              /* Time for run in dummy-mode */
static int timeout = 0;                 /* Timeout for startup */
static int verbignoresignals = 1;       /* warn on ignore signal errors */

static int max_retries = 10;
static char dollytab[256];



static int flag_log = 0;
static char logfile[256] = "";

const char* host_delim = ",";

/* Pipe descriptor in case data must be uncompressed before write */
static int pd[2];
/* Pipe descriptor in case input data must be compressed */
static int id[2]; 

/* PIDs of child processes */
static int in_child_pid = 0, out_child_pid = 0;

#endif
