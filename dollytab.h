#ifndef DOLLYTAB_H
#define DOLLYTAB_H
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXFANOUT 8

/* Size of blocks transf. to/from net/disk and one less than that */
static unsigned int t_b_size = 4096;
#define T_B_SIZE   t_b_size
#define T_B_SIZEM1 (T_B_SIZE - 1)

static char servername[256] = "";
static int meserver = 0; /* This machine sends the data. */
static int melast = 0;   /* This machine doesn't have children to send data */
static char **hostring = NULL;

static char flag_v = 0;                 /* verbose */

static char dummy_mode = 0; /* No disk accesses */
static int segsize = 0; /* TCP Segment Size (useful for benchmarking only) */

static int exitloop = 0;

static char *dollybuf = NULL;
static size_t dollybufsize = 0;

/* Postfix of extra network interface names */
static char add_name[MAXFANOUT][32];
/* Postfix or midfix? */
/* 0 = undefined, 1 = postfix, 2 = midfix */
/* Some explanations about the meanings of postfix and midfix:
   postfix ex.: hostname = "cops1", postfix = "-giga" -> "cops1-giga"
   midfix ex.: hostname = "xibalba101", midfix = "-fast" -> "xibalba-fast101"
*/
static unsigned short add_mode = 0;
static int hyphennormal = 1;      /* '-' normal or interf. sep. in hostnames */


/* Describes the tree-structure */

static unsigned int fanout = 1;   /* default is linear list */

struct dollytab {
  char myhostname[256];
  char infile[256];
  char outfile[256];
  int nexthosts[MAXFANOUT];
  int compressed_in;           /* compressed transfer or not? */
  int compressed_out;          /* write results compressed? */
  /* Sizes for splitted input/output files. 0 means "don't split" (default) */
  unsigned long long output_split;
  unsigned long long input_split;
  unsigned int dummysize;
  unsigned int add_nr; /* Number of extra links for data transfers */
  int add_primary; /* Addition Post- or Midfix for primary interf. */
  unsigned int nr_childs;
  unsigned int hostnr;
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
