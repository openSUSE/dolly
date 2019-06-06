/*
   Dolly: A programm to distribute partition-images and whole disks
   over a fast, switched network.

   Copyright: Felix Rauch <rauch@inf.ethz.ch> and ETH Zurich.
   
   License: GNU General Public License.
   
   History:
   V 0.1  xy-MAR-1999 Felix Rauch <rauch@inf.ethz.ch>
          First version for benchmarks required. Presented results
	  at Cluster Computing 99, Karlsruhe, Germany
   V 0.2  02-JUL-1999 Felix Rauch <rauch@inf.ethz.ch>
          Some improvements...
   V 0.21 31-JAN-2000 Felix Rauch <rauch@inf.ethz.ch>
          Status information backflow, no longer a ring topology.
   V 0.3  01-FEB-2000 Felix Rauch <rauch@inf.ethz.ch>
          Dolly now supports tree topologies with any number of fanouts.
	  (Trees are usually slower, so this change won't speed up dolly.
	  I just implemented it to proove that it's really slower.
	  See our Paper for EuroPar 2000 on our publications site
	  http://www.cs.inf.ethz.ch/CoPs/publications/)
   V 0.4  25-11-2000 Felix Rauch <rauch@inf.ethz.ch>
          Started to include the possibility to write compressed outfiles.
   V 0.5  March-2001 Christian Kurmann <kurmann@inf.ethz.ch>
          Started extensions to dolly for switch-benchmark.
   V 0.51 21-MAR-2001 Felix Rauch <rauch@inf.ethz.ch>
          Extended dummy-mode with time limit
	  Integrated use of extra network interfaces
   V 0.52 25-MAI-2001 Christian Kurmann <kurmann@inf.ethz.ch>
          Integrated primary network hostname add to change primary device
   V 0.53 25-MAI-2001 Felix Rauch <rauch@inf.ethz.ch>
          Allowed empty "dummy" parameter
   V 0.54 30-MAY-2001 Felix Rauch <rauch@inf.ethz.ch>
          Fixed a bug with config-files larger than 1448 bytes.
   V 0.55 25-JUL-2001 Felix Rauch <rauch@inf.ethz.ch>
          Added parameter for timeout (-a) and version (-V).
   V 0.56 15-JAN-2003 David Mathog <mathog@mendel.bio.caltech.edu>
          Added parameter -S. Acts like -s except it doesn't
          check the hostname. Handy when multiple interfaces
          are present.
   V 0.57 8-MAY-2003 Felix Rauch <rauch@inf.ethz.ch>
          Splitted infiles and outfiles are now possible.
	  Parameter 'hyphennormal' treats '-' as normal character in hostnames.
   V 0.58 2-NOV-2004 David Mathog <mathog@caltech.edu>
          (plus some minor cleanups by Felix Rauch)
          Added changes to allow "/dev/stdin", "/dev/stdout" processing for
	  input file and output file. "-" would have been used but
	  that already has some other meanings in this program.
	  This makes it easy to use dolly in a pipe
	  like this:
	  
	  master:  tar cf - /tree_to_send | dolly -s -f config
	  clients: dolly | tar xpf -
	  
	  In this mode compression should be disabled. That allows
	  compression option to be used on each machines' pipe.
	  
	  Redirected messages which used to be to stdout to stderr.
	  Also, since in some modes the main node knows the slave node
	  names, but they don't, this information is put into the
	  environment variable MYNODENAME and retrieved via
	  getenv("MYNODENAME") in the program. If this succeeds the
	  node will not even try gethostbyname(). For compatibility
	  with other systems, dolly also checks the "HOST" environment
	  variable.
	  
	  Default to TCP_NODELAY. Set large input/output buffers.
	  Ignore errno EINTR on select on client. Option to suppress
	  the warning (there can be a lot of them!).

   V 0.58C 23-MAR-2005 David Mathog <mathog@caltech.edu>
          Changed TRANSFER_BLOCK_SIZE to T_B_SIZE and replaced all
          explicit 4096 with this define.  Similarly, replaced all
          4095 with T_B_SIZEM1.  This allows a test to see if increasing
          this from 4096 to 8192 speeds anything up.  Netpipe suggests
          it should.
          
          Added a flag -n = 'no sync'.  When dolly waits for sync
          on an 80 Mb file it can take twice as long for the transfer
          to finish.  When dolly exits the disk light does come on, so
          it appears that the data flushes to disk asynchronously anyway
          when sync() is omitted.  

   V 0.59 09-APR-2019 Antoine Ginies <aginies@suse.com>
          Cleanup warning building
	  Change default output in non verbose mode (get some stats)
	  Add -b option to specify the TRANSFER_BLOCK_SIZE
	  Add -u to specify the size of buffers for TCP sockets
	  
   If you change the history, then please also change the version_string
   right below!  */

static const char version_string[] = "0.59, 09-APR-2019";

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
static int t_b_size = 4096;
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
static int fanout = 1;   /* default is linear list */

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
static int dummy_time = 0;              /* Time for run in dummy-mode */
static int dummysize = 0;
static int exitloop = 0;
static int timeout = 0;                 /* Timeout for startup */
static int hyphennormal = 1;      /* '-' normal or interf. sep. in hostnames */
static int verbignoresignals = 1;       /* warn on ignore signal errors */

/* Number of extra links for data transfers */
static unsigned char add_nr = 0;
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
static int nr_childs = 0;
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

/* Handles timeouts by terminating the program. */
static void alarm_handler(int arg)
{
  fprintf(stderr, "Timeout reached (was set to %d seconds).\nTerminating.\n",
	  timeout);
  exit(1);
}

/* Parses the config-file. The path to the file is given in dollytab */
static void parse_dollytab(FILE *df)
{
  char str[256];
  char *sp, *sp2;
  unsigned int i;
  int me = -2;
  int hadmynodename = 0; /* Did this node already get its node name? */
  char *mnname = NULL;

  /* Read the parameters... */
  
  /* Is there a MYNODENAME? If so, use that for the nodename. There
     won't be a line like this on the server node, but there should always
     be a first line of some kind
  */
  mnname = getenv("MYNODENAME");
  if(mnname != NULL) {
    (void)strcpy(myhostname, mnname);
    (void)fprintf(stderr,
		  "\nAssigned nodename %s from MYNODENAME environment variable\n",
		  myhostname);
    hadmynodename = 1;
  }
  mnname = getenv("HOST");
  if(mnname != NULL) {
    (void)strcpy(myhostname, mnname);
    (void)fprintf(stderr,
		  "\nAssigned nodename %s from HOST environment variable\n",
		  myhostname);
    hadmynodename = 1;
  }
  
  /* First we want to know the input filename */
  if(!dummy_mode) {
    if(fgets(str, 256, df) == NULL) {
      fprintf(stderr, "errno = %d\n", errno);
      perror("fgets for infile");
      exit(1);
    }
    sp2 = str;
    if(strncmp("compressed ", sp2, 11) == 0) {
      compressed_in = 1;
      sp2 += 11;
    }
    if(strncmp("infile ", sp2, 7) != 0) {
      fprintf(stderr, "Missing 'infile ' in config-file.\n");
      exit(1);
    }
    sp2 += 7;
    if(sp2[strlen(sp2)-1] == '\n') {
      sp2[strlen(sp2)-1] = '\0';
    }
    if((sp = strchr(sp2, ' ')) == NULL) {
      sp = sp2 + strlen(sp2);
    }
    if(compressed_in && (strncmp(&sp2[sp - sp2 - 3], ".gz", 3) != 0)) {
      char tmp_str[256];
      strncpy(tmp_str, sp2, sp - sp2);
      tmp_str[sp - sp2] = '\0';
      fprintf(stderr,
	      "WARNING: Compressed outfile '%s' doesn't end with '.gz'!\n",
	      tmp_str);
    }
    strncpy(infile, sp2, sp - sp2);
    sp++;
    if(strcmp(sp, "split") == 0) {
      input_split = 1;
    }
    
    /* Then we want to know the output filename */
    if(fgets(str, 256, df) == NULL) {
      perror("fgets for outfile");
      exit(1);
    }
    sp2 = str;
    if(strncmp("compressed ", sp2, 11) == 0) {
      compressed_out = 1;
      sp2 += 11;
    }
    if(strncmp("outfile ", sp2, 8) != 0) {
      fprintf(stderr, "Missing 'outfile ' in config-file.\n");
      exit(1);
    }
    sp2 += 8;
    if(sp2[strlen(sp2)-1] == '\n') {
      sp2[strlen(sp2)-1] = '\0';
    }
    if((sp = strchr(sp2, ' ')) == NULL) {
      sp = sp2 + strlen(sp2);
    }
    if(compressed_out && (strncmp(&sp2[sp - sp2 - 3], ".gz", 3) != 0)) {
      char tmp_str[256];
      strncpy(tmp_str, sp2, sp - sp2);
      tmp_str[sp - sp2] = '\0';
      fprintf(stderr,
	      "WARNING: Compressed outfile '%s' doesn't end with '.gz'!\n",
	      tmp_str);
    }
    strncpy(outfile, sp2, sp - sp2);
    sp++;
    if(strncmp(sp, "split ", 6) == 0) {
      unsigned long long size = 0;
      char *s = sp+6;
      while(isdigit(*s)) {
	size *= 10LL;
	size += (unsigned long long)(*s - '0');
	s++;
      }
      switch(*s) {
      case 'T': size *= 1024LL;
      case 'G': size *= 1024LL;
      case 'M': size *= 1024LL;
      case 'k': size *= 1024LL; break;
      default:
	fprintf(stderr, "Unknown multiplier '%c' for split size.\n", *s);
	break;
      }
      output_split = size;
      str[sp - str - 1] = '\0';
    }
  } else {
    /* Dummy Mode: Get the size of to transfer data */ 
    if(fgets(str, 256, df) == NULL) {
      perror("fgets on dummy");
      exit(1);
    }
    if(strncmp("dummy ", str, 6) == 0) {
      if(str[strlen(str)-1] == '\n') {
	str[strlen(str)-1] = '\0';
      }
      sp = strchr(str, ' ');
      if(sp == NULL) {
        fprintf(stderr, "Error dummy line.\n");
      }
      if(atoi(sp + 1) == 0) {
	exitloop = 1;
      }
      dummysize = atoi(sp + 1);
      dummysize = 1024*1024*dummysize;
    } else if(strcmp("dummy\n", str) == 0) {
      dummysize = 0;
    }
  }
  
  /* Get the optional TCPMaxSeg size */ 
  if(fgets(str, 256, df) == NULL) {
    perror("fgets on segsize or fanout");
    exit(1);
  }
  if(strncmp("segsize ", str, 8) == 0) {
    if(str[strlen(str)-1] == '\n') {
      str[strlen(str)-1] = '\0';
    }
    sp = strchr(str, ' ');
    if(sp == NULL) {
      fprintf(stderr, "Error segsize line.\n");
      exit(1);
    }
    segsize = atoi(sp + 1);
    if(fgets(str, 256, df) == NULL) {
      perror("fgets after segsize");
      exit(1);
    }
  }

  /* Get the optional extra network interfaces */
  /* Form of the line: add <nr_extra_interfaces>:<postfix>{:<postfix>} */
  if((strncmp("add ", str, 4) == 0) || (strncmp("add2 ", str, 5) == 0)) {
    char *s1, *s2;
    int max = 0, i;

    if(strncmp("add ", str, 4) == 0) {
      add_mode = 1;
    }
    if(strncmp("add2 ", str, 5) == 0) {
      add_mode = 2;
    }
    if(add_mode == 0) {
      fprintf(stderr,
	      "Bad add_mode: Choose 'add' or 'add2' in config-file.\n");
      exit(1);
    }
    
    s1 = str + 4;
    s2 = s1;
    while((*s2 != ':' && *s2 != '\n' && *s2 != 0)) s2++;
    if(*s2 == 0) {
      fprintf(stderr, "Error in add line: First colon missing.\n");
      exit(1);
    }
    *s2 = 0;
    max = atoi(s1);
    if(max < 0) {
      fprintf(stderr, "Error in add line: negative number.\n");
      exit(1);
    }
    if(max >= MAXFANOUT) {
      fprintf(stderr, "Error in add line: Number larger than MAXFANOUT.\n");
      exit(1);
    }
    if (max==0) {
      /* change names of primary interface */
      add_primary = 1;
      s1 = s2 + 1;
      s2++;
      while((*s2 != ':' && *s2 != '\n' && *s2 != 0)) s2++;
      if(*s2 == 0) {
	fprintf(stderr, "Error in add line: Preliminary end.\n");
	exit(1);
      }
      *s2 = 0;
      strcpy(add_name[0], s1);
    } else {
      for(i = 0; i < max; i++) {
	s1 = s2 + 1;
	s2++;
	while((*s2 != ':' && *s2 != '\n' && *s2 != 0)) s2++;
	if(*s2 == 0) {
	  fprintf(stderr, "Error in add line: Preliminary end.\n");
	  exit(1);
	}
	*s2 = 0;
	strcpy(add_name[i], s1);
      }
    }
    add_nr = max;
    if(fgets(str, 256, df) == NULL) {
      perror("fgets after add");
      exit(1);
    }
  }
  
  /* Get the fanout (1 = linear list, 2 = binary tree, ... */
  if(strncmp("fanout ", str, 7) == 0) {
    if(str[strlen(str)-1] == '\n') {
      str[strlen(str)-1] = '\0';
    }
    sp = strchr(str, ' ');
    if(sp == NULL) {
      fprintf(stderr, "Error in fanout line.\n");
      exit(1);
    }
    fanout = atoi(sp + 1);
    if(fgets(str, 256, df) == NULL) {
      perror("fgets after fanout");
      exit(1);
    }
  }

  /*
   * The parameter "hyphennormal" means that the hyphen '-' is treated
   * as any other character. The default is to treat it as a separator
   * between base hostname and the number of the node.
   */
  if(strncmp("hyphennormal", str, 12) == 0) {
    hyphennormal = 1;
    if(fgets(str, 256, df) == NULL) {
      perror("fgets after hyphennormal");
      exit(1);
    }
  }
  if(strncmp("hypheninterface", str, 12) == 0) {
    hyphennormal = 1;
    if(fgets(str, 256, df) == NULL) {
      perror("fgets after hypheninterface");
      exit(1);
    }
  }
  
  /* Get our own hostname */
  if(!hadmynodename){
    if(gethostname(myhostname, 63) == -1) {
      perror("gethostname");
    }
  }
  /* Get the server's name. */
  if(strncmp("server ", str, 7) != 0) {
    fprintf(stderr, "Missing 'server' in config-file.\n");
    exit(1);
  }
  if(str[strlen(str)-1] == '\n') {
    str[strlen(str)-1] = '\0';
  }
  sp = strchr(str, ' ');
  if(sp == NULL) {
    fprintf(stderr, "Error in firstclient line.\n");
    exit(1);
  }
  strncpy(servername, sp+1, strlen(sp));

  /* 
     disgusting hack to make -S work.  If the server name
     specified in the file is wrong bad things will probably happen!
  */
  
  if(meserver == 2){
    (void) strcpy(myhostname,servername);
    meserver = 1;
  }

  if(!(meserver ^ (strcmp(servername, myhostname) != 0))) {
    fprintf(stderr,
	    "Commandline parameter -s and config-file disagree on server!\n");
    fprintf(stderr, "  My name is '%s'.\n", myhostname);
    fprintf(stderr, "  The config-file specifies '%s'.\n", servername);
    exit(1);
  }
  
  /* We need to know the FIRST host of the ring. */
  /* (Do we still need the firstclient?)         */
  if(fgets(str, 256, df) == NULL) {
    perror("fgets for firstclient");
    exit(1);
  }
  if(strncmp("firstclient ", str, 12) != 0) {
    fprintf(stderr, "Missing 'firstclient ' in config-file.\n");
    exit(1);
  }
  if(str[strlen(str)-1] == '\n') {
    str[strlen(str)-1] = '\0';
  }
  sp = strchr(str, ' ');
  if(sp == NULL) {
    fprintf(stderr, "Error in firstclient line.\n");
    exit(1);
  }

  /* We need to know the LAST host of the ring. */
  if(fgets(str, 256, df) == NULL) {
    perror("fgets for lastclient");
    exit(1);
  }
  if(strncmp("lastclient ", str, 11) != 0) {
    fprintf(stderr, "Missing 'lastclient ' in config-file.\n");
    exit(1);
  }
  if(str[strlen(str)-1] == '\n') {
    str[strlen(str)-1] = '\0';
  }
  sp = strchr(str, ' ');
  if(sp == NULL) {
    fprintf(stderr, "Error in lastclient line.\n");
    exit(1);
  }
  if(strcmp(myhostname, sp+1) == 0) {
    melast = 1;
  } else {
    melast = 0;
  }

  /* Read in all the participating hosts. */
  if(fgets(str, 256, df) == NULL) {
    perror("fgets for clients");
    exit(1);
  }
  if(strncmp("clients ", str, 8) != 0) {
    fprintf(stderr, "Missing 'clients ' in config-file.\n");
    exit(1);
  }
  hostnr = atoi(str+8);
  if((hostnr < 1) || (hostnr > 10000)) {
    fprintf(stderr, "I think %d numbers of hosts doesn't make much sense.\n",
	    hostnr);
    exit(1);
  }
  hostring = (char **)malloc(hostnr * sizeof(char *));
  for(i = 0; i < hostnr; i++) {
    if(fgets(str, 256, df) == NULL) {
      char errstr[256];
      sprintf(errstr, "gets for host %d", i);
      perror(errstr);
      exit(1);
    }
    if(str[strlen(str)-1] == '\n') {
      str[strlen(str)-1] = '\0';
    }
    hostring[i] = (char *)malloc(strlen(str)+1);
    strcpy(hostring[i], str);

    /* Try to find next host in ring */
    /* if(strncmp(hostring[i], myhostname, strlen(myhostname)) == 0) { */
    if(strcmp(hostring[i], myhostname) == 0) {
      me = i;
    } else if(!hyphennormal) {
      /* Check if the hostname is correct, but a different interface is used */
      if((sp = strchr(hostring[i], '-')) != NULL) {
	if(strncmp(hostring[i], myhostname, sp - hostring[i]) == 0) {
	  me = i;
	}
      }
    }
  }

  if(!meserver && (me == -2)) {
    fprintf(stderr, "Couldn't find myself in hostlist.\n");
    exit(1);
  }
  
  /* Build up topology */
  nr_childs = 0;
  
  for(i = 0; i < fanout; i++) {
    if(meserver) {
      if(i + 1 <= hostnr) {
	nexthosts[i] = i;
	nr_childs++;
      }
    } else {
      if((me + 1) * fanout + 1 + i <= hostnr) {
	nexthosts[i] = (me + 1) * fanout + i;
	nr_childs++;
      }
    }
  }
  /* In a tree, we might have multiple last machines. */
  if(nr_childs == 0) {
    melast = 1;
  }

  /* Did we reach the end? */
  if(fgets(str, 256, df) == NULL) {
    perror("fgets for endconfig");
    exit(1);
  }
  if(strncmp("endconfig", str, 9) != 0) {
    fprintf(stderr, "Missing 'endconfig' in config-file.\n");
    exit(1);
  }
  if((nr_childs > 1) && (add_nr > 0)) {
    fprintf(stderr, "Currently dolly supports either a fanout > 1\n"
	    "OR the use of extra network links, but not both.\n");
    exit(1);
  }
  if(flag_v) {
    fprintf(stderr, "done.\n");
  }
  if(flag_v) {
    if(!meserver) {
      fprintf(stderr, "I'm number %d\n", me);
    }
  }
}

/*
 * Clients read the parameters from the control-socket.
 * As they are already parsed from the config-file by the server,
 * we don't do much error-checking here.
 */
static void getparams(int f)
{
  size_t readsize, writesize;
  int fd, ret;
  FILE *dolly_df = NULL;
  char tmpfile[32] = "/tmp/dollytmpXXXXXX";

  if(flag_v) {
    fprintf(stderr, "Trying to read parameters...");
    fflush(stderr);
  }
  dollybuf = (char *)malloc(T_B_SIZE);
  if(dollybuf == NULL) {
    fprintf(stderr, "Couldn't get memory for dollybuf.\n");
    exit(1);
  }

  readsize = 0;
  do {
    ret = read(f, dollybuf + readsize, T_B_SIZE);
    if(ret == -1) {
      perror("read in getparams while");
      exit(1);
    } else if(ret == T_B_SIZE) {  /* This will probably not happen... */
      fprintf(stderr, "Ups, the transmitted config-file seems to long.\n"
	      "Please rewrite dolly.\n");
      exit(1);
    }
    readsize += ret;
  } while(ret == 1448);
  dollybufsize = readsize;

  /* Write everything to a file so we can use parse_dollytab(FILE *)
     afterwards.  */
#if 0
  tmpfile = tmpnam(NULL);
  fd = open(tmpfile, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
#endif
  fd = mkstemp(tmpfile);
  if(fd == -1) {
    perror("Opening temporary file 'tmp' in getparams");
    exit(1);
  }
  writesize = write(fd, dollybuf, readsize);
  if(writesize == -1) {
    perror("Writing temporary file 'tmp' in getparams");
    exit(1);
  }
  dolly_df = fdopen(fd, "r");
  if(dolly_df == NULL) {
    perror("fdopen in getparams");
    exit(1);
  }
  rewind(dolly_df);
  if(flag_v) {
    fprintf(stderr, "done.\n");
  }
  if(flag_v) {
    fprintf(stderr, "Parsing parameters...");
    fflush(stderr);
  }
  parse_dollytab(dolly_df);
  fclose(dolly_df);
  close(fd);
  unlink(tmpfile);
}

/* This functions prints all the parameters before starting.
   It's mostly used for debugging. */
static void print_params(void)
{
  unsigned int i;
  fprintf(stderr, "Parameter file: \n");
  if(!dummy_mode) {
    if(compressed_in) {
      fprintf(stderr, "compressed ");
    }
    fprintf(stderr, "infile = '%s'", infile);
    if(input_split != 0) {
      fprintf(stderr, ", splitted in parts.\n");
    } else {
      fprintf(stderr, "\n");
    }
    if(compressed_out) {
      fprintf(stderr, "compressed ");
    }
    fprintf(stderr, "outfile = '%s'", outfile);
    if(output_split != 0) {
      fprintf(stderr, ", splitted in %llu byte parts.\n", output_split);
    } else {
      fprintf(stderr, "\n");
    }
  } else {
    fprintf(stderr, "dummy filesize = %d MB\n", dummysize/1024/1024);
  }
  fprintf(stderr, "using data port %u\n", dataport);
  fprintf(stderr, "using ctrl port %u\n", ctrlport);
  fprintf(stderr, "myhostname = '%s'\n", myhostname);
  if(segsize > 0) {
    fprintf(stderr, "TCP segment size = %d\n", segsize);
  }
  if(add_nr > 0) {
    fprintf(stderr, "add_nr (extra network interfaces) = %d\n", add_nr);
    if(add_mode == 1) {
      fprintf(stderr, "Postfixes: ");
    } else if(add_mode == 2) {
      fprintf(stderr, "Midfixes: ");
    } else {
      fprintf(stderr, "Undefined value fuer add_mode: %d\n", add_mode);
      exit(1);
    }
    for(i = 0; i < add_nr; i++) {
      fprintf(stderr, "%s", add_name[i]);
      if(i < add_nr - 1) fprintf(stderr, ":");
    }
    fprintf(stderr, "\n");
  }
  if (add_primary == 1) {
    fprintf(stderr, "add to primary hostname = ");
    fprintf(stderr, "%s", add_name[0]);
    fprintf(stderr, "\n");
  }
  fprintf(stderr, "fanout = %d\n", fanout);
  fprintf(stderr, "nr_childs = %d\n", nr_childs);
  fprintf(stderr, "server = '%s'\n", servername);
  fprintf(stderr, "I'm %sthe server.\n", (meserver ? "" : "not "));
  fprintf(stderr, "I'm %sthe last host.\n", (melast ? "" : "not "));
  fprintf(stderr, "There are %d hosts in the ring (excluding server):\n",
	  hostnr);
  for(i = 0; i < hostnr; i++) {
    fprintf(stderr, "\t'%s'\n", hostring[i]);
  }
  fprintf(stderr, "Next hosts in ring:\n");
  if(nr_childs == 0) {
    fprintf(stderr, "\tnone.\n");
  } else {
    for(i = 0; i < nr_childs; i++) {
      fprintf(stderr, "\t%s (%d)\n",
	      hostring[nexthosts[i]], nexthosts[i]);
    }
  }
  fprintf(stderr, "All parameters read successfully.\n");
  if(compressed_in && !meserver) {
    fprintf(stderr,
	    "Will use gzip to uncompress data before writing.\n");
  } else if(compressed_in && meserver) {
    fprintf(stderr,
	    "Clients will have to use gzip to uncompress data before writing.\n");
  } else {
    fprintf(stderr, "No compression used.\n");
  }
  fprintf(stderr, "Using transfer size %d bytes.\n", T_B_SIZE);
}

static void open_insocks(void)
{
  struct sockaddr_in addr;
  int optval;
  char *drcvbuf = NULL;
  int recv_size, sizeofint = sizeof(int);
  
  /* All machines have an incoming data link */
  datasock = socket(PF_INET, SOCK_STREAM, 0);
  if(datasock == -1) {
    perror("Opening input data socket");
    exit(1);
  }
  optval = 1;
  if(setsockopt(datasock, SOL_SOCKET, SO_REUSEADDR,
		&optval, sizeof(int)) == -1) {
    perror("setsockopt on datasock");
    exit(1);
  }
  
  /* Attempt to set TCP_NODELAY */
  optval = 1;
  if(setsockopt(datasock, IPPROTO_TCP, TCP_NODELAY,
        	&optval, sizeof(int)) < 0) {
    (void)fprintf(stderr, "setsockopt: TCP_NODELAY failed! errno = %d\n",
		  errno);
    // exit(1);
  }

  if(segsize > 0) {
    /* Attempt to set TCP_MAXSEG */
    fprintf(stderr, "Set TCP_MAXSEG to %d bytes\n", segsize);
    if(setsockopt(datasock, IPPROTO_TCP, TCP_MAXSEG,
		  &segsize, sizeof(int)) < 0) {
      (void) fprintf(stderr,"setsockopt: TCP_MAXSEG failed! errno=%d\n", errno);
      // exit(1);
    }
  }
  
  /* MATHOG, set a large buffer for the data socket, this section is
     taken from NETPIPE. */
  /* Attempt to set input BUFFER sizes */
  if(flag_v) { fprintf(stderr, "Buffer size: %d\n", SCKBUFSIZE); }
  drcvbuf = malloc(SCKBUFSIZE);
  if(drcvbuf == NULL) {
    perror("Error creating buffer for input data socket");
    exit(1);
  }
  if(setsockopt(datasock, SOL_SOCKET, SO_RCVBUF, &drcvbuf, SCKBUFSIZE) < 0) {
    (void) fprintf(stderr, "setsockopt: SO_RCVBUF failed! errno = %d\n",
		   errno);
    exit(556);
  }
  getsockopt(datasock, SOL_SOCKET, SO_RCVBUF,
	     (char *) &recv_size, (void *) &sizeofint);
  if(flag_v) {
    (void)fprintf(stderr, "Receive buffer is %d bytes\n", recv_size);
  }
  
  addr.sin_family = AF_INET;
  addr.sin_port = htons(dataport);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(datasock, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
    perror("binding input data socket");
    exit(1);
  }
  if(listen(datasock, 1) == -1) {
    perror("listen input data socket");
    exit(1);
  }
  
  /* All machines have an incoming control link */
  ctrlsock = socket(PF_INET, SOCK_STREAM, 0);
  if(ctrlsock == -1) {
    perror("Opening input control socket");
    exit(1);
  }
  optval = 1;
  if(setsockopt(ctrlsock, SOL_SOCKET, SO_REUSEADDR,
		&optval, sizeof(int)) == -1) {
    perror("setsockopt on ctrlsock");
    exit(1);
  }
  addr.sin_family = AF_INET;
  addr.sin_port = htons(ctrlport);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(ctrlsock, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
    perror("binding input control socket");
    exit(1);
  }
  if(listen(ctrlsock, 1) == -1) {
    perror("listen input control socket");
    exit(1);
  }
}

static void open_outsocks(void)
{
  struct hostent *hent;
  struct sockaddr_in addrdata, addrctrl;
  int ret;
  int dataok = 0, ctrlok = 0;
  int i;
  int optval;
  int max;
  char hn[256+32];
  char *dsndbuf = NULL;
  int send_size, sizeofint = sizeof(int);

  if(nr_childs > 1) {
    max = nr_childs;
  } else if(add_nr > 0) {
    max = add_nr + 1;
  } else {
    max = 1;
  }
  for(i = 0; i < max; i++) {  /* For all childs we have */
    if(nr_childs > 1) {
      strcpy(hn, hostring[nexthosts[i]]);
    } else if(add_nr > 0) {
      if(add_mode == 1) {
	strcpy(hn, hostring[nexthosts[0]]);
	if(i > 0) {
	  strcat(hn, add_name[i - 1]);
	}
      } else if(add_mode == 2) {
	if(i == 0) {
	  strcpy(hn, hostring[nexthosts[0]]);
	} else {
	  int j = 0;
	  while(!isdigit(hostring[nexthosts[0]][j])) {
	    hn[j] = hostring[nexthosts[0]][j];
	    j++;
	  }
	  hn[j] = 0;
	  strcat(hn, add_name[i - 1]);
	  strcat(hn, &hostring[nexthosts[0]][j]);
	}
      } else {
	fprintf(stderr, "Undefined add_mode %d!\n", add_mode);
	exit(1);
      }
    } else if (add_primary) {
      assert(i < 1);
      
      if(add_mode == 1) {
	strcpy(hn, hostring[nexthosts[0]]);
	strcat(hn, add_name[0]);
      } else if(add_mode == 2) {
	int j = 0;
	while(!isdigit(hostring[nexthosts[0]][j])) {
	  hn[j] = hostring[nexthosts[0]][j];
	  j++;
	}
	hn[j] = 0;
	strcat(hn, add_name[0]);
	strcat(hn, &hostring[nexthosts[0]][j]);
      } else {
	fprintf(stderr, "Undefined add_mode %d!\n", add_mode);
	exit(1);
      }
    } else {
      assert(i < 1);
      strcpy(hn, hostring[nexthosts[i]]);
    }
    
    hent = gethostbyname(hn);
    /*  (void)fprintf(stderr,"DEBUG gethostbyname on >%s<\n",hn); */
    if(hent == NULL) {
      char str[strlen(hn)];
      sprintf(str, "gethostbyname for host '%s' error %d",
	      hn, h_errno);
      herror(str);
      exit(1);
    }
    if(hent->h_addrtype != AF_INET) {
      fprintf(stderr, "Expected h_addrtype of AF_INET, got %d\n",
	      hent->h_addrtype);
    }
    
    if(flag_v) {
      fprintf(stderr, "Connecting to host %s... ", hn);
      fflush(stderr);
    }
    
    /* Wait until we connected to everything... */
    dataok  = ctrlok = 0;
    dsndbuf = NULL;
    do {
      dataout[i] = socket(PF_INET, SOCK_STREAM, 0);
      if(dataout[i] == -1) {
	perror("Opening output data socket");
	exit(1);
      }

      if((nr_childs > 1) || (i == 0)) {
	ctrlout[i] = socket(PF_INET, SOCK_STREAM, 0);
	if(ctrlout[i] == -1) {
	  perror("Opening output control socket");
	  exit(1);
	}
      }
      
      /* Attempt to set TCP_NODELAY */
      optval = 1;
      if(setsockopt(dataout[i], IPPROTO_TCP, TCP_NODELAY,
        	    &optval, sizeof(int)) < 0) {
        (void) fprintf(stderr,"setsockopt: TCP_NODELAY failed! errno = %d\n",
		       errno);
        // exit(1);
      }

      if(segsize > 0) {
	/* Attempt to set TCP_MAXSEG */
	fprintf(stderr, "Set TCP_MAXSEG to %d bytes\n", segsize);
	if(setsockopt(dataout[i], IPPROTO_TCP, TCP_MAXSEG,
		      &segsize, sizeof(int)) < 0) {
	  (void) fprintf(stderr, "setsockopt: TCP_MAXSEG failed! errno = %d\n", 
			 errno);
	  // exit(1);
	}
      }
      
      /* MATHOG, set a large buffer for the data socket, this section is
     	 taken from NETPIPE */
      /* Attempt to set output BUFFER sizes */
      if(dsndbuf == NULL){
	dsndbuf = malloc(SCKBUFSIZE);/* Note it may reallocate, which is ok */
	if(dsndbuf == NULL){
	  perror("Error creating buffer for input data socket");
	  exit(1);
	}
	if(setsockopt(dataout[i], SOL_SOCKET, SO_SNDBUF, &dsndbuf,
		      SCKBUFSIZE) < 0)
	  {
	    (void) fprintf(stderr,
			   "setsockopt: SO_SNDBUF failed! errno = %d\n",
			   errno);
	    exit(556);
	  }
	getsockopt(dataout[i], SOL_SOCKET, SO_RCVBUF,
		   (char *) &send_size, (void *) &sizeofint);
	fprintf(stderr, "Send buffer %d is %d bytes\n", i, send_size);
      }

      /* Setup data port */
      addrdata.sin_family = hent->h_addrtype;
      addrdata.sin_port = htons(dataport);
      bcopy(hent->h_addr, &addrdata.sin_addr, hent->h_length);

      if((nr_childs > 1) || (i == 0)) {
	/* Setup control port */
	addrctrl.sin_family = hent->h_addrtype;
	addrctrl.sin_port = htons(ctrlport);
	bcopy(hent->h_addr, &addrctrl.sin_addr, hent->h_length);
      }
      
      if(!dataok) {
	ret = connect(dataout[i],
		      (struct sockaddr *)&addrdata, sizeof(addrdata));
	if((ret == -1) && (errno == ECONNREFUSED)) {
	  close(dataout[i]);
	} else if(ret == -1) {
	  perror("connect");
	  exit(1);
	} else {
	  dataok = 1;
#ifdef DOLLY_NONBLOCK
	  ret = fcntl(dataout[i], F_SETFL, O_NONBLOCK);
	  if(ret == -1) {
	    perror("fcntl");
	  }
#endif /* DOLLY_NONBLOCK */
	  if(add_nr > 0) {
	    ret = write(dataout[i], &i, sizeof(i));
	    if(ret == -1) {
	      perror("Write fd-nr in open_outsocks()");
	      exit(1);
	    }
	  }
	  if(flag_v) {
	    fprintf(stderr, "data ");
	    fflush(stderr);
	  }
	}
      }
      if(!ctrlok) {
	if((nr_childs > 1) || (i == 0)) {
	  ret = connect(ctrlout[i],
			(struct sockaddr *)&addrctrl, sizeof(addrctrl));
	  if((ret == -1) && (errno == ECONNREFUSED)) {
	    close(ctrlout[i]);
	  } else if(ret == -1) {
	    perror("connect");
	    exit(1);
	  } else {
	    ctrlok = 1;
	    if(flag_v) {
	      fprintf(stderr, "control ");
	      fflush(stderr);
	    }
	  }
	} else {
	  ctrlok = 1;
	}
      }
      if(dataok + ctrlok != 2) {
	sleep(1);
      }
    } while(dataok + ctrlok != 2);
    if(flag_v) {
      fprintf(stderr, "\b.\n");
    }
  }
}

/*
 * If "try_hard" is 1, call must be succesful.
 * If try_hard is 1 and an input file can't be opened, the program terminates.
 * If try_hard is not 1 and an input file can't be opened, -1 is returend.
 */
static int open_infile(int try_hard)
{
  char name[256+16];

  /* Close old input file if there is one */
  if(input != -1) {
    if(close(input) == -1) {
      perror("close() in open_infile()");
      exit(1);
    }
  }
  if(input_split != 0) {
    sprintf(name, "%s_%d", infile, input_nr);
  } else {
    strcpy(name, infile);
  }
  
  /* Files for input/output */
  if(!compressed_out) {
    /* Input is from file */
    input = open(name, O_RDONLY);
    if(input == -1) {
      if(try_hard == 1) {
	char str[strlen(name)];
	sprintf(str, "open inputfile '%s'", name);
	perror(str);
	exit(1);
      } else {
	return -1;
      }
    }
  } else {
    /* Input should be compressed first. */
    if(access(name, R_OK) == -1) {
      if(try_hard == 1) {
	char str[strlen(name)];
	sprintf(str, "open inputfile '%s'", name);
	perror(str);
	exit(1);
      } else {
	return -1;
      }
    }
    if(pipe(id) == -1) {
      perror("input pipe()");
      exit(1);
    }
    input = id[0];
    if((in_child_pid = fork()) == 0) {
      int fd;
      /* Here's the child. */
      close(id[0]);
      close(1);
      dup(id[1]);
      close(id[1]);
      if((fd = open(name, O_RDONLY)) == -1) {
	exit(1);
      }
      close(0);
      dup(fd);
      close(fd);
      if(execl("/usr/bin/gzip", "gzip", "-cf", NULL) == -1) {
	perror("execl for gzip in child");
	exit(1);
      }
    } else {
      /* Father */
      close(id[1]);
    }
  } /* endif compressed_out */
  input_nr++;
  return 0;
}

static int open_outfile(int try_hard)
{
  char name[256+16];
  int is_device = 0;
  /* Close old output file, if there is one. */
  if(output != -1) {
    if(close(output) == -1) {
      perror("close() in open_outfile()");
      exit(1);
    }
  }
  if(output_split != 0) {
    sprintf(name, "%s_%d", outfile, output_nr);
  } else {
    strcpy(name, outfile);
  }
  /* check if file is under /dev, if not open even if the file does not exist. */
  if (strncmp("/dev/",name,6) != 0 ) {
    is_device = 1;
  }
  /* Setup the output files/pipes. */
  if(!compressed_in) {
    /* Output is to a file */
    if(!compressed_out && (output_split == 0) && is_device) {
      /* E.g. partition-to-partition cloning */
      output = open(name, O_WRONLY);
    } else {
      /* E.g. patition-to-compressed-archive cloning */
      output = open(name, O_WRONLY | O_CREAT | O_EXCL, 0644);
    }
    if(output == -1) {
	char str[strlen(name)];
	sprintf(str, "open outputfile '%s'", name);
	perror(str);
	exit(1);
    }
  } else { /* Compressed_In */
    if(access(name, W_OK) == -1) {
      if(try_hard == 1) {
	char str[strlen(name)];
	sprintf(str, "open outputfile '%s'", name);
	perror(str);
	exit(1);
      } else {
	return -1;
      }
    }
    /* Pipe to gunzip */
    if(pipe(pd) == -1) {
      perror("output pipe");
      exit(1);
    }
    output = pd[1];
    if((out_child_pid = fork()) == 0) {
      int fd;
      /* Here's the child! */
      close(pd[1]);
      close(0);      /* Close stdin */
      dup(pd[0]);    /* Duplicate pipe on stdin */
      close(pd[0]);  /* Close the unused end of the pipe */
      if((fd = open(name, O_WRONLY)) == -1) {
	if(errno == ENOENT) {
	  fprintf(stderr, "Outputfile in child does not exist.\n");
	}
	perror("open outfile in child");
	exit(1);
      }
      close(1);
      dup(fd);
      close(fd);
      /* Now stdout is redirected to our file */
      if(execl("/usr/bin/gunzip", "gunzip", "-c", NULL) == -1) {
	perror("execl for gunzip in child");
	exit(1);
      }
    } else {
      /* Father */
      close(pd[0]);
    }
  }
  output_nr++;
  return 0;
}

#define WRITE 1
#define READ 2

static int movebytes(int fd, int dir, char *addr, unsigned int n)
{
  int ret, bytes;
  static int child_done = 0;
  
  bytes = 0;

  if(child_done && (dir == READ)) {
    child_done = 0;
    return 0;
  }
  while(0 != n) {
    if(dir == WRITE) {
      ret = write(fd, addr, n);
    } else if(dir == READ) {
      fflush(stderr);
      ret = read(fd, addr, n);
      if((ret < n) && compressed_out && meserver) {
	int wret, status;
	sleep(1);
	wret = waitpid(in_child_pid, &status, WNOHANG);
	if(wret == -1) {
	  perror("waitpid");
	} else if(wret == 0) {
	  fprintf(stderr, "waitpid returned 0\n");
	} else {
	  if(WIFEXITED(status)) {
	    if(WEXITSTATUS(status) == 0) {
	      child_done++;
	      return ret;
	    } else {
	      fprintf(stderr, "Child terminated with return value %d\n",
		      WEXITSTATUS(status));
	    }
	  }
	}
	(void) fprintf(stderr,"read returned %d\n", ret);
      }
    } else {
      fprintf(stderr, "Bad direction in movebytes!\n");
      ret = 0;
    }
    if(ret == -1) {
#ifdef DOLLY_NONBLOCK
      if(errno == EAGAIN) {
	continue;
      }
#endif /* DOLLY_NONBLOCK */
      perror("movebytes read/write");
      fprintf(stderr, "\terrno = %d\n", errno);
      exit(1);
    } else if(ret == 0) {
      return bytes;
    } else {
      addr += ret;
      n -= ret;
      bytes += ret;
    }
  }
  return bytes;
}

static void buildring(void)
{
  socklen_t size;
  int ret, i, nr;
  int ready_mach = 0;  /* Number of ready machines */
  char msg[128];
  char info_buf[1024];

  if(!meserver) {
    /* Open the input sockets and wait for connections... */
    open_insocks();
    if(flag_v) {
      fprintf(stderr, "Accepting...");
      fflush(stderr);
    }
    
    /* All except the first accept a connection now */
    ctrlin = accept(ctrlsock, NULL, &size);
    if(ctrlin == -1) {
      perror("accept input control socket");
      exit(1);
    }
    if(flag_v) {
      fprintf(stderr, "control...\n");
      fflush(stderr);
    }
    
    /* Clients should now read everything from the ctrl-socket. */
    getparams(ctrlin);
    if(flag_v) {
      print_params();
    }

    datain[0] = accept(datasock, NULL, &size);
    if(datain[0] == -1) {
      perror("accept input data socket");
      exit(1);
    }
    if(add_nr > 0) {
      ret = read(datain[0], &nr, sizeof(nr));
      if(ret == -1) {
	perror("First read for nr in buildring");
	exit(1);
      }
      assert(nr == 0);
      for(i = 0; i < add_nr; i++) {
	datain[1 + i] = accept(datasock, NULL, &size);
	if(datain[1 + i] == -1) {
	  perror("accept extra input data socket");
	  exit(1);
	}
	ret = read(datain[1 + i], &nr, sizeof(nr));
	if(ret == -1) {
	  perror("Read for nr in buildring");
	  exit(1);
	}
	assert(nr == (1 + i));
      }
    }
    if(flag_v) {
      fprintf(stderr, "Connected data...done.\n");
    }
    /* The input sockets are now connected. */

    /* Give information back to server */
    sprintf(msg, "Host got parameters '%s'.\n", myhostname);
    ret = movebytes(ctrlin, WRITE, msg, strlen(msg));
    if(ret != strlen(msg)) {
      fprintf(stderr,
	      "Couldn't write got-parameters-message back to server "
	      "(sent %d instead of %zu bytes).\n",
	      ret, strlen(msg));
    }
  }

  if(!dummy_mode) {
    if(meserver) {
      open_infile(1);
    } else {
      open_outfile(1);
    }
  }

  /* All the machines except the leaf(s) need to open output sockets */
  if(!melast) {
    open_outsocks();
  }
  
  /* Finally, the first machine also accepts a connection */
  if(meserver) {
    char buf[T_B_SIZE];
    size_t readsize;
    int fd, ret, maxsetnr = -1;
    fd_set real_set, cur_set;
    
    /* Send out dollytab */
    fd = open(dollytab, O_RDONLY);
    if(fd == -1) {
      perror("open dollytab");
      exit(1);
    }
    readsize = read(fd, buf, T_B_SIZE);
    if(readsize == -1) {
      perror("read dollytab");
      exit(1);
    } else if(readsize == T_B_SIZE) {
      fprintf(stderr, "Dollytab possibly too long, adjust program...\n");
      exit(1);
    } else if(readsize == 1448) {
      buf[1448] = ' ';
      readsize += 1;
    }
    for(i = 0; i < nr_childs; i++) {
      movebytes(ctrlout[i], WRITE, buf, readsize);
    }
    close(fd);


    /* Wait for backflow-information or the data socket connection */
    fprintf(stderr, "Waiting for ring to build...\n");
    FD_ZERO(&real_set);
    maxsetnr = -1;
    for(i = 0; i < nr_childs; i++) {
      FD_SET(ctrlout[i], &real_set);
      if(ctrlout[i] > maxsetnr) {
	maxsetnr = ctrlout[i];
      }
    }
    maxsetnr++;
    do {
      cur_set = real_set;
      ret = select(maxsetnr, &cur_set, NULL, NULL, NULL);
      if(ret == -1) {
	perror("select in buildring()\n");
	exit(1);
      }
      for(i = 0; i < nr_childs; i++) {
	if(FD_ISSET(ctrlout[i], &cur_set)) {
	  ret = read(ctrlout[i], info_buf, 1024);
	  if(ret == -1) {
	    perror("read backflow in buildring");
	    exit(1);
	  }
	  if(ret == 0) {
	    fprintf(stderr, "read returned 0 from backflow in buildring.\n");
	    exit(1);
	  } else {
	    char *p;
	    
	    p = info_buf;
	    info_buf[ret] = 0;	
	    if(flag_v) {
	      fprintf(stderr, info_buf);
	    }
	    while((p = strstr(p, "ready")) != NULL) {
	      ready_mach++;
	      p++;
	    }
	    fprintf(stderr,
		    "Machines left to wait for: %d\n", hostnr - ready_mach);
	  }
	}
      } /* For all childs */
    } while(ready_mach < hostnr);
  }

  if(flag_v) {
    fprintf(stderr, "Accepted.\n");
  }
  
  if(!meserver) {
    /* Send it further */
    if(!melast) {
      for(i = 0; i < nr_childs; i++) {
	movebytes(ctrlout[i], WRITE, dollybuf, dollybufsize);
      }
    }
    
    /* Give information back to server */
    sprintf(msg, "Host ready '%s'.\n", myhostname);
    ret = movebytes(ctrlin, WRITE, msg, strlen(msg));
    if(ret != strlen(msg)) {
      fprintf(stderr,
	      "Couldn't write ready-message back to server "
	      "(sent %d instead of %zu bytes).\n",
	      ret, strlen(msg));
    }
  }
}

/* The main transmitting function */
static void transmit(void)
{
  char *buf_addr, *buf;
  unsigned long long t, transbytes = 0, lastout = 0;
  unsigned int bytes = T_B_SIZE;
  int ret = 1, maxsetnr = 0;
  unsigned long td = 0, tdlast = 0;
  int i, a;
  FILE *logfd = NULL;
  struct timeval tv1, tv2, tv3;
  fd_set real_set, cur_set;

  buf_addr = (char *)malloc(2 * T_B_SIZEM1);
  buf = (char *)((unsigned long)(buf_addr + T_B_SIZEM1) & (~T_B_SIZEM1));
  if(dummy_mode) {
    bzero(buf, T_B_SIZE);
  }

  t = 0x7fffffff;
  t <<= 32;
  t |= 0xffffffff;
  maxbytes = 0;
#define ADJ_MAXSET(a) if((a)>maxsetnr){maxsetnr=(a);}
  if(!meserver) {
    FD_ZERO(&real_set);
    FD_SET(datain[0], &real_set);
    FD_SET(ctrlin, &real_set);
    ADJ_MAXSET(datain[0]);
    ADJ_MAXSET(ctrlin);
    if(!melast) {
      for(i = 0; i < nr_childs; i++) {
	FD_SET(ctrlout[i], &real_set);  /* Check ctrlout too for backflow */
	ADJ_MAXSET(ctrlout[i]);
      }
    }
    maxsetnr++;
  } else {
    FD_ZERO(&real_set);
    for(i = 0; i < nr_childs; i++) {
      FD_SET(ctrlout[i], &real_set);
      ADJ_MAXSET(ctrlout[i]);
    }
    maxsetnr++;
  }
#undef ADJ_MAXSET
  
  gettimeofday(&tv1, NULL);
  tv2 = tv1;
  
  while((meserver && (ret > 0)) || (!meserver && (t > 0))) {
    /* The server writes data as long has it can to the data stream.
       When there's nothing left, it writes the actual number of bytes
       to the control stream (as long long int).
    */
    if(meserver) {
      /*
       * Server part
       */
      if(!dummy_mode) {
	ret = movebytes(input, READ, buf, bytes);
      } else {
	if((dummysize > 0) && ((maxbytes + bytes) > dummysize)) {
	  ret = dummysize - maxbytes;
	} else if(dummy_time > 0) {
	  /* Check if the dummy transmission time is reached. */
	  struct timeval tvt;
	  static long long last_check = 0;
	  if(maxbytes - last_check >= 1000000) {
	    last_check = maxbytes;
	    gettimeofday(&tvt, NULL);
	    td = tvt.tv_sec - tv1.tv_sec;
	    if(td > dummy_time) {
	      ret = 0;
	    } else {
	      ret = bytes;
	    }
	  } else {
	    ret = bytes;
	  }
	} else {
	  ret = bytes;
	}
      }
      maxbytes += ret;
      if(ret > 0) {
	if(add_nr == 0) {
	  for(i = 0; i < nr_childs; i++) {
	    movebytes(dataout[i], WRITE, buf, ret);
	  }
	} else {
	  static int cur_out = 0;
	  movebytes(dataout[cur_out], WRITE, buf, ret);
	  cur_out++;
	  if(cur_out > add_nr) {
	    cur_out = 0;
	  }
	}
      } else {
	/* Here: ret <= 0 */
	int res = -1;
	if(input_split) {
	  res = open_infile(0);
	}
	if(!input_split || (res < 0)) {
	  if(flag_v) {
	    fprintf(stderr, "\nRead %llu bytes from file(s).\n", maxbytes);
	  }
	  if(add_nr == 0) {
	    for(i = 0; i < nr_childs; i++) {
	      (void)fprintf(stderr, "Writing maxbytes = %lld to ctrlout\n",
			    maxbytes);
	      movebytes(ctrlout[i], WRITE, (char *)&maxbytes, 8);
	      shutdown(dataout[i], 2);
	    }
	  } else {
	    (void)fprintf(stderr, "Writing maxbytes = %lld to ctrlout\n",
			  maxbytes);
	    movebytes(ctrlout[0], WRITE, (char *)&maxbytes, 8);
	    for(i = 0; i <= add_nr; i++) {
	      shutdown(dataout[i], 2);
	    }
	  }
	} else {
	  /* Next input file opened */
	  ret = 100000;
	  continue;
	} /* end input_split */
      }
      //if(flag_v && (maxbytes - lastout >= 10000000)) {
      if(maxbytes - lastout >= 10000000) {
	tv3=tv2;
	gettimeofday(&tv2, NULL);
	td = (tv2.tv_sec*1000000 + tv2.tv_usec)
	  - (tv1.tv_sec*1000000 + tv1.tv_usec);
	tdlast = (tv2.tv_sec*1000000 + tv2.tv_usec)
	  - (tv3.tv_sec*1000000 + tv3.tv_usec);
	fprintf(stdtty,
		"\rSent MB: %.0f, MB/s: %.3f, Current MB/s: %.3f      ",
		(float)maxbytes/1000000,
		(float)maxbytes/td,(float)(maxbytes - lastout)/tdlast);
	fflush(stdtty);
	lastout = maxbytes;
      }
    } else {
      /*
       * Client part
       */
      unsigned int i, nr_descr;
      cur_set = real_set;
      ret = select(maxsetnr, &cur_set, NULL, NULL, NULL);
      if(ret == -1) {
        if(errno != EINTR) {
	  /* MATHOG: (on above "if" statement)
	   * Some signal was received, don't have a handler, ignore it.
	   */
	  perror("select");
	  exit(1);
	}
	ret = 0;
      }
      if(ret < 1) {
	if(verbignoresignals) {
	  /* fr: Shouldn't that be a bit further up? */
	  (void)fprintf(stderr,
			"\nIgnoring unhandled signal (select() returned %d.\n",
			ret);
	}
      }
      else {
	nr_descr = ret;
	for(i = 0; i < nr_descr; i++) {
	  if(FD_ISSET(ctrlin, &cur_set)) {
	    char mybuf[128];

	    ret = read(ctrlin, mybuf, 128);
	    if(ret == -1) {
	      perror("read from ctrlin in transfer");
	      exit(1);
	    }
	    if(ret == 0) {
	      fprintf(stderr,
		      "Got 0 from ctrlin, but there should be something there!\n"
		      "Probably transmission interrupted, terminating.\n");
	      exit(1);
	    }
	    if(ret != 8) {
	      fprintf(stderr, "Got %d bytes from ctrlin in transfer, "
		      "expected 8.\n", ret);
	    }
	    maxbytes = *(unsigned long long *)&mybuf;
	    if(!melast) {
	      for(i = 0; i < nr_childs; i++) {
		movebytes(ctrlout[i], WRITE, (char *)&maxbytes, 8);
	      }
	    }
	    t = maxbytes - transbytes;
	    if(flag_v) {
	      fprintf(stderr,"\nMax. bytes will be %llu bytes. %llu bytes left.\n", maxbytes, t);
	    }
	    FD_CLR(ctrlin, &real_set);
	    FD_CLR(ctrlin, &cur_set);
	  } else if(FD_ISSET(datain[0], &cur_set)) {
	    /* There is data to be read from the net */
	    bytes = (t >= T_B_SIZE ? T_B_SIZE : t);
	    ret = movebytes(datain[0], READ, buf, bytes);
	    if(!dummy_mode) {
	      if(!output_split) {
		movebytes(output, WRITE, buf, ret);
	      } else {
		/* Check if output file needs to be split. */
		if((transbytes / output_split)
		   != ((transbytes + ret) / output_split)) {
		  size_t old_part, new_part;
		  old_part = ret - (transbytes + ret) % output_split;
		  new_part = ret - old_part;
		  movebytes(output, WRITE, buf, old_part);
		  open_outfile(1);
		  movebytes(output, WRITE, buf + old_part, new_part);
		} else {
		  movebytes(output, WRITE, buf, ret);
		}
	      } /* end input_split */
	    }
	    if(!melast) {
	      for(i = 0; i < nr_childs; i++) {
		movebytes(dataout[i], WRITE, buf, ret);
	      }
	    }
	    transbytes += ret;
	    t -= ret;
	    FD_CLR(datain[0], &cur_set);
	    /* Handle additional network interfaces, if available */
	    for(a = 1; a <= add_nr; a++) {
	      bytes = (t >= T_B_SIZE ? T_B_SIZE : t);
	      ret = movebytes(datain[a], READ, buf, bytes);
	      if(!dummy_mode) {
		movebytes(output, WRITE, buf, ret);
	      }
	      if(!melast) {
		movebytes(dataout[a], WRITE, buf, bytes);
	      }
	      transbytes += ret;
	      t -= ret;
	    }
	  } else { /* FD_ISSET(ctrlin[]) */
	    int foundfd = 0;

	    for(i = 0; i < nr_childs; i++) {
	      if(FD_ISSET(ctrlout[i], &cur_set)) {
		/* Backflow of control-information, just pass it on */
		ret = read(ctrlout[i], buf, T_B_SIZE);
		if(ret == -1) {
		  perror("read backflow in transmit");
		  exit(1);
		}
		movebytes(ctrlin, WRITE, buf, ret);
		foundfd++;
		FD_CLR(ctrlout[i], &cur_set);
	      }
	    }
	    /* if nothing found */
	    if(foundfd == 0) {
	      fprintf(stderr,
		      "select returned without any ready fd, ret = %d.\n",
		      ret);
	      for(i = 0; i < maxsetnr; i++) {
		if(FD_ISSET(i, &cur_set)) {
		  int j;
		  fprintf(stderr, "  file descriptor %d is set.\n", i);
		  for(j = 0; j < nr_childs; j++) {
		    if(FD_ISSET(ctrlout[j], &cur_set)) {
		      fprintf(stderr, "  (fd %d = ctrlout[%d])\n", i, j);
		    }
		  }
		  for(j = 0; j <= add_nr; j++) {
		    if(FD_ISSET(datain[j], &cur_set)) {
		      fprintf(stderr, "  (fd %d = datain[%d])\n", i, j);
		    }
		  }
		}
	      }
	      exit(1);
	    }
	  }
	}
      }
      if(flag_v && (transbytes - lastout >= 10000000)) {
	tv3=tv2;
	gettimeofday(&tv2, NULL);
	td = (tv2.tv_sec*1000000 + tv2.tv_usec)
	  - (tv1.tv_sec*1000000 + tv1.tv_usec);
	tdlast = (tv2.tv_sec*1000000 + tv2.tv_usec)
	  - (tv3.tv_sec*1000000 + tv3.tv_usec);
	fprintf(stdtty, "\rTransfered MB: %.0f, MB/s: %.3f, Current MB/s: %.3f      ", (float)transbytes/1000000, (float)transbytes/td,(float)(transbytes - lastout)/tdlast);
	fflush(stdtty);
	lastout = transbytes;
      }
    }
    alarm(0);  /* We did something, so turn off the timeout-alarm */
  } /* end while */
  gettimeofday(&tv2, NULL);
  td = (tv2.tv_sec*1000000 + tv2.tv_usec) - (tv1.tv_sec*1000000 + tv1.tv_usec);
  
  if(meserver) {
    fprintf(stdtty, "\rSent MB: %.0f.       \n", (float)maxbytes/1000000);
 
    if(flag_log){
      logfd = fopen(logfile, "a");
      if(logfd == NULL) {
	perror("open logfile");
	exit(1);
      }
      if(!dummy_mode) {
	if(compressed_in) {
	  fprintf(logfd, "compressed ");
	}
	fprintf(logfd, "infile = '%s'\n", infile);
	if(compressed_out) {
	  fprintf(logfd, "compressed ");
	}
	fprintf(logfd, "outfile = '%s'\n", outfile);
      } else {
	if(flag_v) {
	  fprintf(logfd, "Transfered block : %d MB\n", dummysize/1024/1024);
	} else {
	  fprintf(logfd, " %8d",
		  dummysize > 0 ? (dummysize/1024/1024) : (int)(maxbytes/1024LL/1024LL));
	}
      }
      if(flag_v) {
	if(segsize > 0) {
	  fprintf(logfd, "TCP segment size : %d Byte (%d Byte eth)\n", 
		  segsize,segsize+54);
	} else {
	  fprintf(logfd,
		  "Standard TCP segment size : 1460 Bytes (1514 Byte eth)\n");
	}
      } else {
	if(segsize > 0) {
	  fprintf(logfd, " %8d", segsize);
	} else {
	  fprintf(logfd, " %8d", 1460);
	}
      }
      
      if(flag_v) {
	fprintf(logfd, "Server : '%s'\n", myhostname);
	fprintf(logfd, "Fanout = %d\n", fanout);
	fprintf(logfd, "Nr of childs = %d\n", nr_childs);
	fprintf(logfd, "Nr of hosts = %d\n", hostnr);
      } else {
	fprintf(logfd, " %8d", hostnr);
      }
      //fprintf(logfd, "server = '%s'\n", servername);
      //fprintf(logfd, "I'm %sthe server.\n", (meserver ? "" : "not "));
      //fprintf(logfd, "I'm %sthe last host.\n", (melast ? "" : "not "));
      //fprintf(logfd, "There are %d hosts in the ring (excluding server):\n", hostnr);
    }
  } else {
    fprintf(stderr, "Transfered MB: %.0f, MB/s: %.3f \n\n",
	    (float)transbytes/1000000, (float)transbytes/td);
    fprintf(stdtty, "\n");
  }
  
  close(output);
  if(dosync){
    sync();
    if(flag_v) {
      fprintf(stderr, "Synced.\n");
    }
  }
  if(meserver) {
    for(i = 0; i < nr_childs; i++) {
      if(flag_v) {
        fprintf(stderr, "Waiting for child %d.\n",i);
      }
      ret = movebytes(ctrlout[i], READ, buf, 8);
      if(ret != 8) {
	fprintf(stderr,
		"Server got only %d bytes back from client %d instead of 8\n",
		ret, i);
      }
    }
    buf[8] = 0;
    if(*(unsigned long long *)buf != maxbytes) {
      fprintf(stderr, "*** ERROR *** Didn't get correct maxbytes back!\n");
	      /* create unneeded error, so removing as only used for debugging 
	      "Got %lld (0x%016llx) instead of %lld (0x%016llx)\n",
	      *(unsigned long long *)&buf,
	      *(unsigned long long *)&buf,
	      maxbytes, maxbytes);
	      */
    } else {
      fprintf(stderr, "Clients done.\n");
    }

    fprintf(stderr, "Time: %lu.%03lu\n", td / 1000000, td % 1000000);
    fprintf(stderr, "MBytes/s: %0.3f\n", (double)maxbytes / td);
    fprintf(stderr, "Aggregate MBytes/s: %0.3f\n",
	    (double)maxbytes * hostnr / td);
    if(maxcbytes != 0) {
      fprintf(stderr, "Bytes written on each node: %llu\n", maxcbytes);
      fprintf(stderr, "MBytes/s written: %0.3f\n",
	      (double)maxcbytes / td);
      fprintf(stderr, "Aggregate MBytes/s written: %0.3f\n",
	      (double)maxcbytes * hostnr / td);
    }
    if(flag_log) {
      if(flag_v) {
	fprintf(logfd, "Time: %lu.%03lu\n", td / 1000000, td % 1000000);
	fprintf(logfd, "MBytes/s: %0.3f\n", (double)maxbytes / td);
	fprintf(logfd, "Aggregate MBytes/s: %0.3f\n",
		(double)maxbytes * hostnr / td);
	if(maxcbytes != 0) {
	  fprintf(logfd, "Bytes written on each node: %llu\n", maxcbytes);
	  fprintf(logfd, "MBytes/s written: %0.3f\n",
		  (double)maxcbytes / td);
	  fprintf(logfd, "Aggregate MBytes/s written: %0.3f\n",
		  (double)maxcbytes * hostnr / td);
	}
      } else {
	fprintf(logfd, "%4lu.%06lu  ", td / 1000000, td % 1000000);
	fprintf(logfd, "%4.6f  ", (double)maxbytes / td);
	fprintf(logfd, "%4.6f  ",
		(double)maxbytes * hostnr / td);
	if(maxcbytes != 0) {
	  fprintf(logfd, "Bytes written on each node: %llu\n", maxcbytes);
	  fprintf(logfd, "MBytes/s written: %0.3f\n",
		  (double)maxcbytes / td);
	  fprintf(logfd, "Aggregate MBytes/s written: %0.3f\n",
		  (double)maxcbytes * hostnr / td);
	}
	fprintf(logfd, "\n");
      }
      fclose(logfd);
    }
    
  } else if(!melast) {
    /* All clients except the last just transfer 8 bytes in the backflow */
    unsigned long long ll;
    for(i = 0; i < nr_childs; i++) {
      movebytes(ctrlout[i], READ, (char *)&ll, 8);
    }
    movebytes(ctrlin, WRITE, (char *)&ll, 8);
  } else if(melast) {
    movebytes(ctrlin, WRITE, (char *)&maxbytes, 8);
  }
  if(flag_v) {
    fprintf(stderr, "Transmitted.\n");
  }
  free(buf_addr);
  if(maxbytes == 0) {
    exitloop = 1;
  }
  
}

static void usage(void)
{
  fprintf(stderr, "\n");
  fprintf(stderr,
	  "Usage: dolly [-hVvsnC] [-c <size>] [-b <size>] [-u <size>] [-d] [-f configfile] "
	  "[-o logfile] [-t time]\n");
  fprintf(stderr, "\t-s: this is the server, check hostname\n");
  fprintf(stderr, "\t-S: this is the server, do not check hostname\n");
  fprintf(stderr, "\t-v: verbose\n");
  fprintf(stderr, "\t-b <size>, where size is the size of block to transfer (default 4096)\n");
  fprintf(stderr, "\t-u <size>, size of the buffer (multiple of 4K)\n");
  fprintf(stderr, "\t-c <size>, where size is uncompressed size of "
	  "compressed inputfile\n\t\t(for statistics only)\n");

  fprintf(stderr, "\t-C do not use a config file.");
  fprintf(stderr, "\t-f <configfile>, where <configfile> is the "
	  "configuration file with all\n\t\tthe required information for "
	  "this run. Required on server only.\n");
  fprintf(stderr,
	  "\t-d: dummy-mode. Dolly just sends data over the net,\n\t\twithout "
	  "disk accesses. This ist mostly used to test switches.\n");
  fprintf(stderr, "\t-o <logfile>: Write some statistical information  "
	  "in <logfile>\n");
  fprintf(stderr, "\t-t <time>, where <time> is the run-time in seconds of this "
	  "dummy-mode\n");
  fprintf(stderr, "\t-a <timeout>: Lets dolly terminate if it could not transfer\n\t\tany data after <timeout> seconds.\n");
  fprintf(stderr, "\t-n: Do not sync before exit. Dolly exits sooner.\n");
  fprintf(stderr, "\t    Data may not make it to disk if power fails soon after dolly exits.\n");
  fprintf(stderr, "\t-h: Print this help and exit\n");
  fprintf(stderr, "\t-q: Suppresss \"ignored signal\" messages\n");
  fprintf(stderr, "\t-V: Print version number and exit\n");
  fprintf(stderr, "\t-H: comma seperated list of the hosts to send to\n");
  fprintf(stderr, "\nDolly is part of the Patagonia cluster project, ");
  fprintf(stderr, "see also\nhttp://www.cs.inf.ethz.ch/cops/patagonia/\n");
  fprintf(stderr, "\n");
  exit(1);
}

int main(int argc, char *argv[])
{
  int c, i;
  int flag_f = 0;
  int flag_cargs = 0;
  FILE *df;
  char *mnname = NULL, *tmp_str, *host_str, *a_str;
  size_t nr_hosts = 0;


  /* Parse arguments */
  while(1) {
    c = getopt(argc, argv, "f:c:b:u:vqo:Sshndt:a:V:i:O:Y:H");
    if(c == -1) break;
    
    switch(c) {
    case 'f':
      /* Where to find the config-file. */
      if(strlen(optarg) > 255) {
        fprintf(stderr, "Name of config-file too long.\n");
        exit(1);
      }
      strcpy(dollytab, optarg);
      flag_f = 1;
      break;

    case 'v':
      /* Verbose */
      flag_v = 1;
      break;

    case 'o':
      /* log filename */
      if(strlen(optarg) > 255) {
	fprintf(stderr, "Name of log-file too long.\n");
	exit(1);
      }
      strcpy(logfile, optarg);
      flag_log = 1;
      break;

      /* This is now in the config file. */
    case 'c':
      compressed_in = 1;
      maxcbytes = atoi(optarg);
      break;
      
    case 'b':
      t_b_size = atoi(optarg);
      break;

    case 'u':
      buffer_size = atoi(optarg);
      break;

    case 'n':
      dosync = 0;
      break;
      
      
    case 's':
      /* This machine is the server. */
      meserver = 1;
      break;

    case 'S':
      /* This machine is the server - don't check hostname. */
      meserver = 2;
      break;
      
    case 'd':
      /* Dummy mode. Just transfer data without disk accesses. */
      dummy_mode = 1;
      break;

    case 't':
      /* How long should dolly run in dummy mode? */
      if(atoi(optarg) < 0) {
        fprintf(stderr,
          "Time for -t parameter should be positive instead of %d.\n",
        atoi(optarg));
        exit(1);
      } else if(atoi(optarg) == 0) {

      } else {
        dummy_time = atoi(optarg);
      }
      break;

    case 'a':
      i = atoi(optarg);
      if(i <= 0) {
        fprintf(stderr, "Timeout of %d doesn't make sense.\n", i);
        exit(1);
      }
      timeout = i;
      if(flag_v) {
        fprintf(stderr, "Will set timeout to %d seconds.\n", timeout);
      }
      signal(SIGALRM, alarm_handler);
      break;

    case 'h':
      /* Give a little help */
      usage();
      break;
      
    case 'q':
      verbignoresignals = 0;
      break;

    case 'V':
      fprintf(stderr, "Dolly version %s\n", version_string);
      exit(0);
      break;
    case 'C':
      flag_cargs = 1;
      break;

    case 'i':
      if(strlen(optarg) > 255) {
        fprintf(stderr, "Name of input-file too long.\n");
        exit(1);
      }
      strncpy(optarg,infile,strlen(optarg)); 
      flag_cargs = 1;
      break;

    case 'O':
      if(strlen(optarg) > 255) {
        fprintf(stderr, "Name of output-file too long.\n");
        exit(1);
      }
      strncpy(optarg,outfile,strlen(optarg)); 
      flag_cargs = 1;
      break;

    case 'Y':
      hyphennormal = 1;
      break;

    case 'H':
      /* copying string as it is manipulatet */
      a_str = strdup(optarg);
      tmp_str = a_str;
      while(*tmp_str) {
        if(*host_delim == *tmp_str) {
          nr_hosts++;
        }
        tmp_str++;
      }
      nr_hosts++;
      hostring = (char**) malloc(nr_hosts * sizeof(char *));
      /* now find the first host */
      host_str = strtok(a_str,host_delim);
      nr_hosts = 0;
      while(host_str != NULL) {
        hostring[nr_hosts] = (char *)malloc(strlen(host_str)+1);
        strcpy(hostring[nr_hosts], host_str);
        host_str = strtok(NULL,host_delim);
        nr_hosts++;
      }
      free(a_str);
      /* make sure that we are the server */
      meserver = 1;
      
      break;
      
    default:
      fprintf(stderr, "Unknown option '%c'.\n", c);
      exit(1);
    }
    if (flag_cargs) {
      mnname = getenv("HOST");
      (void)strcpy(myhostname, mnname);

    }
    if (flag_f && !flag_cargs) {
      /* Open the config-file */
      df = fopen(optarg, "r");
      if(df == NULL) {
        char errstr[256];
        sprintf(errstr, "fopen dollytab '%s'", optarg);
        perror(errstr);
        exit(1);
      }
      parse_dollytab(df);
      fclose(df);
    }
  }

  /* Did we get the parameters we need? */
  if(meserver && !flag_f && !flag_cargs) {
    fprintf(stderr, "Missing parameter -f <configfile> or -C for commandline arguments.\n");
    exit(1);
  }

  if(meserver && flag_v) {
    print_params();
  }

  /* try to open standard terminal output */
  /* if it fails, redirect stdtty to stderr */
  stdtty = fopen("/dev/tty","a");
  if (stdtty == NULL)
    {
      stdtty = stderr;
    }

  do {
    if(flag_v) {
      fprintf(stderr, "\nTrying to build ring...\n");
    }
    
    alarm(timeout);

    buildring();
    
    if(meserver) {
      fprintf(stdtty, "Server: Sending data...\n");
    } else {    
      if(flag_v) {
	fprintf(stdtty, "Receiving...\n");
      }
    }

    if(!exitloop) {
      transmit();
    }
    
    close(datain[0]);
    close(ctrlin);
    close(datasock);
    close(ctrlsock);
    for(i = 0; i < nr_childs; i++) {
      close(ctrlout[i]);
      close(dataout[i]);
    }
    if(flag_v) {
      fprintf(stderr, "\n");
    }
  } while (!meserver && dummy_mode && !exitloop);
 
  fclose(stdtty);
 
  exit(0);
}
