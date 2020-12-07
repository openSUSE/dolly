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
static unsigned int hostnr = 0;
static char **hostring = NULL;
static unsigned int nr_childs = 0;
static int nexthosts[MAXFANOUT];

static char flag_v = 0;                 /* verbose */

static char dummy_mode = 0; /* No disk accesses */
static unsigned int dummysize = 0;
static int segsize = 0; /* TCP Segment Size (useful for benchmarking only) */

static int compressed_in = 0;           /* compressed transfer or not? */
static int compressed_out = 0;          /* write results compressed? */
static int exitloop = 0;

static char infile[256] = "";
static char outfile[256] = "";

static char *dollybuf = NULL;
static size_t dollybufsize = 0;

static unsigned int add_nr = 0; /* Number of extra links for data transfers */
static int add_primary = 0; /* Addition Post- or Midfix for primary interf. */
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

/* Sizes for splitted input/output files. 0 means "don't split" (default) */
static unsigned long long input_split = 0;
static unsigned long long output_split = 0;

/* Describes the tree-structure */

static unsigned int fanout = 1;   /* default is linear list */

struct dollytab {
  char myhostname[256];
};
/* Parses the config-file. The path to the file is given in dollytab */
void parse_dollytab(FILE *df,struct dollytab*);

/*
 * Clients read the parameters from the control-socket.
 * As they are already parsed from the config-file by the server,
 * we don't do much error-checking here.
 */
void getparams(int f,struct dollytab*);
#endif
