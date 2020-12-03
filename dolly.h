#ifndef DOLLY_H
#define DOLLY_H
static const char version_string[] = "0.60, 11-SEPT-2019";

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

#define MAXFANOUT 8

/* Size of blocks transf. to/from net/disk and one less than that */
static unsigned int t_b_size = 4096;
#define T_B_SIZE   t_b_size
#define T_B_SIZEM1 (T_B_SIZE - 1)


#define DOLLY_NONBLOCK 1                 /* Use nonblocking network sockets */

static FILE *stdtty;           /* file pointer to the standard terminal tty */

static int meserver = 0;                    /* This machine sends the data. */
static int melast = 0;   /* This machine doesn't have children to send data */
static char myhostname[256] = "";

/* Clients need the ports before they can listen, so we use defaults. */
static unsigned int dataport = 9998;
static unsigned int ctrlport = 9997;

/* File descriptors for file I/O */
static int input = -1, output = -1;

/* Sizes for splitted input/output files. 0 means "don't split" (default) */
static unsigned long long input_split = 0;
static unsigned long long output_split = 0;

/* Numbers for splitted input/output files */
static unsigned int input_nr = 0, output_nr = 0;

/* TCP Segment Size (useful for benchmarking only) */
static int segsize = 0;

/* size of buffers for TCP sockets (approx. 100KB, multiple of 4 KB) */
static int buffer_size = 98304;
#define SCKBUFSIZE buffer_size

/* Describes the tree-structure */
static unsigned int fanout = 1;   /* default is linear list */

/* Normal sockets for data transfer */
static int datain[MAXFANOUT], dataout[MAXFANOUT];
static int datasock = -1;

/* Special sockets for control information */
static int ctrlin = -1, ctrlout[MAXFANOUT];
static int ctrlsock = -1;

static unsigned long long maxbytes = 0; /* max bytes to transfer */
static unsigned long long maxcbytes = 0;/*     --  "  --  in compressed mode */
static int compressed_in = 0;           /* compressed transfer or not? */
static int compressed_out = 0;          /* write results compressed? */
static char flag_v = 0;                 /* verbose */
static char dummy_mode = 0;             /* No disk accesses */
static int dosync = 1;                  /* sync() after transfer */
static unsigned int dummy_time = 0;              /* Time for run in dummy-mode */
static unsigned int dummysize = 0;
static int exitloop = 0;
static int timeout = 0;                 /* Timeout for startup */
static int hyphennormal = 1;      /* '-' normal or interf. sep. in hostnames */
static int verbignoresignals = 1;       /* warn on ignore signal errors */

/* Number of extra links for data transfers */
static unsigned int add_nr = 0;
static int add_primary = 0;  /* Addition Post- or Midfix for primary interf. */

/* Postfix of extra network interface names */
static char add_name[MAXFANOUT][32];
/* Postfix or midfix? */
/* 0 = undefined, 1 = postfix, 2 = midfix */
/* Some explanations about the meanings of postfix and midfix:
   postfix ex.: hostname = "cops1", postfix = "-giga" -> "cops1-giga"
   midfix ex.: hostname = "xibalba101", midfix = "-fast" -> "xibalba-fast101"
*/
static unsigned short add_mode = 0;

static char infile[256] = "";
static char outfile[256] = "";
static unsigned int hostnr = 0;
static char **hostring = NULL;
static int nexthosts[MAXFANOUT];
static unsigned int nr_childs = 0;
static int max_retries = 10;
static char servername[256];
static char dollytab[256];


static char *dollybuf = NULL;
static size_t dollybufsize = 0;

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
