#ifndef DOLLY_H
#define DOLLY_H

#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SCKBUFSIZE 65536
#define MAXFANOUT 16

struct dollytab;

extern const unsigned int dataport;
extern const unsigned int ctrlport;
extern const char *host_delim;

extern FILE *stdtty;

extern int datain[MAXFANOUT], dataout[MAXFANOUT];
extern int datasock;
extern int ctrlin, ctrlout[MAXFANOUT];
extern int ctrlsock;

extern int input, output;
extern unsigned int input_nr, output_nr;

extern unsigned long long maxcbytes;
extern unsigned long long maxbytes;
extern int dosync;
extern int timeout;
extern int verbignoresignals;

extern int max_retries;
extern char dollytab[256];

extern int flag_log;
extern char logfile[256];

extern int pd[2];
extern int id[2];

extern int in_child_pid, out_child_pid;

#endif /* DOLLY_H */