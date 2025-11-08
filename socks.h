// socks.h
#ifndef SOCKS_H
#define SOCKS_H

#define SCKBUFSIZE 1048576

#include "dollytab.h"

extern int input;
extern int output;
extern unsigned int input_nr;
extern unsigned int output_nr;
extern unsigned int buffer_size;
extern int datain[MAXFANOUT];
extern int dataout[MAXFANOUT];
extern int datasock;
extern int ctrlin;
extern int ctrlout[MAXFANOUT];
extern int ctrlsock;
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
extern int in_child_pid;
extern int out_child_pid;

void open_insocks(struct dollytab *mydollytab);
void open_insystemdsocks(struct dollytab *mydollytab);
void open_outsocks(struct dollytab *mydollytab);
void buildring(struct dollytab *mydollytab);

#endif // SOCKS_H
