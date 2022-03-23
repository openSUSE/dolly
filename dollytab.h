#ifndef DOLLYTAB_H
#define DOLLYTAB_H
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>

#include "resolve.h"

#define MAXFANOUT 8
#define MAXHOSTS 65536

/* Size of blocks transf. to/from net/disk and one less than that */


struct dollytab {
  unsigned int flag_v;                 /* verbose */
  unsigned int flag_d;                 /* systemd */
  char myhostname[256];
  char servername[256];
  char infile[256];
  char outfile[256];
  char add_name[MAXFANOUT][32];
  unsigned int meserver; /* This machine sends the data. */
  int nexthosts[MAXFANOUT];
  int compressed_in;           /* compressed transfer or not? */
  int compressed_out;          /* write results compressed? */
  /* Sizes for splitted input/output files. 0 means "don't split" (default) */
  unsigned long long output_split;
  unsigned long long input_split;
  unsigned int fanout;   /* default is linear list */
  unsigned int add_nr; /* Number of extra links for data transfers */
  int add_primary; /* Addition Post- or Midfix for primary interf. */
/* Postfix or midfix? */
/* 0 = undefined, 1 = postfix, 2 = midfix */
/* Some explanations about the meanings of postfix and midfix:
   postfix ex.: hostname = "cops1", postfix = "-giga" -> "cops1-giga"
   midfix ex.: hostname = "xibalba101", midfix = "-fast" -> "xibalba-fast101"
*/
  unsigned short add_mode;
  unsigned int nr_childs;
  unsigned int hostnr;
  unsigned int melast;   /* This machine doesn't have children to send data */
  /* Postfix of extra network interface names */
  int hyphennormal;      /* '-' normal or interf. sep. in hostnames */
/* when 4 resolve hostanme to ipv4 addresses, when 6 try ipv6 addresse, do not resolve if anything else */
  unsigned int resolve;
  size_t dollybufsize;
  unsigned int segsize; /* TCP Segment Size (useful for benchmarking only) */
  ssize_t t_b_size; /* Size of blocks transf. to/from net/disk and one less than that */
  char **hostring;
  char *dollybuf;
};

void init_dollytab(struct dollytab *);
/* Parses the config-file. The path to the file is given in dollytab */
void parse_dollytab(FILE *df,struct dollytab*);

/*
 * Clients read the parameters from the control-socket.
 * As they are already parsed from the config-file by the server,
 * we don't do much error-checking here.
 */
void getparams(int f,struct dollytab*);
#endif
