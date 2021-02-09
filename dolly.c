#include "dolly.h"
#include "transmit.h"
#include "files.h"
#include "movebytes.h"

/* Clients need the ports before they can listen, so we use defaults. */
const unsigned int dataport = 9998;
const unsigned int ctrlport = 9997;
const char* host_delim = ",";

FILE *stdtty;           /* file pointer to the standard terminal tty */


/* File descriptors for file I/O */
int input = -1, output = -1;


/* Numbers for splitted input/output files */
unsigned int input_nr = 0, output_nr = 0;


/* size of buffers for TCP sockets (approx. 100KB, multiple of 4 KB) */
unsigned int buffer_size = 98304;


/* Normal sockets for data transfer */
int datain[MAXFANOUT], dataout[MAXFANOUT];
int datasock = -1;

/* Special sockets for control information */
int ctrlin = -1, ctrlout[MAXFANOUT];
int ctrlsock = -1;

unsigned long long maxbytes = 0; /* max bytes to transfer */
unsigned long long maxcbytes = 0;/*     --  "  --  in compressed mode */
int dosync = 1;                  /* sync() after transfer */
int timeout = 0;                 /* Timeout for startup */
int verbignoresignals = 1;       /* warn on ignore signal errors */

int max_retries = 10;
char dollytab[256];



int flag_log = 0;
char logfile[256] = "";


/* Pipe descriptor in case data must be uncompressed before write */
int pd[2];
/* Pipe descriptor in case input data must be compressed */
int id[2]; 

/* PIDs of child processes */
int in_child_pid = 0, out_child_pid = 0;


/* Handles timeouts by terminating the program. */
static void alarm_handler() {
  fprintf(stderr, "Timeout reached (was set to %d seconds).\nTerminating.\n",
	  timeout);
  exit(1);
}

/* This functions prints all the parameters before starting.
   It's mostly used for debugging. */
static void print_params(struct dollytab* mydollytab) {
  unsigned int i;
  fprintf(stderr, "Parameter file: \n");
  if(mydollytab->compressed_in) {
    fprintf(stderr, "compressed ");
  }
  fprintf(stderr, "infile = '%s'", mydollytab->infile);
  if(mydollytab->input_split != 0) {
    fprintf(stderr, ", splitted in parts.\n");
  } else {
    fprintf(stderr, "\n");
  }
  if(mydollytab->compressed_out) {
    fprintf(stderr, "compressed ");
  }
  fprintf(stderr, "outfile = '%s'", mydollytab->outfile);
  if(mydollytab->output_split != 0) {
    fprintf(stderr, ", splitted in %llu byte parts.\n", mydollytab->output_split);
  } else {
    fprintf(stderr, "\n");
  }
  fprintf(stderr, "using data port %u\n", dataport);
  fprintf(stderr, "using ctrl port %u\n", ctrlport);
  fprintf(stderr, "myhostname = '%s'\n", mydollytab->myhostname);
  if(mydollytab->segsize > 0) {
    fprintf(stderr, "TCP segment size = %d\n", mydollytab->segsize);
  }
  if(mydollytab->add_nr > 0) {
    fprintf(stderr, "add_nr (extra network interfaces) = %d\n", mydollytab->add_nr);
    if(mydollytab->add_mode == 1) {
      fprintf(stderr, "Postfixes: ");
    } else if(mydollytab->add_mode == 2) {
      fprintf(stderr, "Midfixes: ");
    } else {
      fprintf(stderr, "Undefined value fuer add_mode: %d\n", mydollytab->add_mode);
      exit(1);
    }
    for(i = 0; i < mydollytab->add_nr; i++) {
      fprintf(stderr, "%s", mydollytab->add_name[i]);
      if(i < mydollytab->add_nr - 1) fprintf(stderr, ":");
    }
    fprintf(stderr, "\n");
  }
  if (mydollytab->add_primary == 1) {
    fprintf(stderr, "add to primary hostname = ");
    fprintf(stderr, "%s", mydollytab->add_name[0]);
    fprintf(stderr, "\n");
  }
  fprintf(stderr, "fanout = %d\n", mydollytab->fanout);
  fprintf(stderr, "nr_childs = %d\n", mydollytab->nr_childs);
  fprintf(stderr, "server = '%s'\n", mydollytab->servername);
  fprintf(stderr, "I'm %sthe server.\n", (mydollytab->meserver ? "" : "not "));
  fprintf(stderr, "I'm %sthe last host.\n", (mydollytab->melast ? "" : "not "));
  fprintf(stderr, "There are %d hosts in the ring (excluding server):\n",
	  mydollytab->hostnr);
  for(i = 0; i < mydollytab->hostnr; i++) {
    fprintf(stderr, "\t'%s'\n", mydollytab->hostring[i]);
  }
  fprintf(stderr, "Next hosts in ring:\n");
  if(mydollytab->nr_childs == 0) {
    fprintf(stderr, "\tnone.\n");
  } else {
    for(i = 0; i < mydollytab->nr_childs; i++) {
      fprintf(stderr, "\t%s (%d)\n",
	      mydollytab->hostring[mydollytab->nexthosts[i]], mydollytab->nexthosts[i]);
    }
  }
  fprintf(stderr, "All parameters read successfully.\n");
  if(mydollytab->compressed_in && !mydollytab->meserver) {
    fprintf(stderr,
	    "Will use gzip to uncompress data before writing.\n");
  } else if(mydollytab->compressed_in && mydollytab->meserver) {
    fprintf(stderr,
	    "Clients will have to use gzip to uncompress data before writing.\n");
  } else {
    fprintf(stderr, "No compression used.\n");
  }
  fprintf(stderr, "Using transfer size %d bytes.\n",(int) mydollytab->t_b_size);
}

static void open_insocks(struct dollytab * mydollytab) {
  struct sockaddr_in addr;
  int optval;
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

  if(mydollytab->segsize > 0) {
    /* Attempt to set TCP_MAXSEG */
    fprintf(stderr, "Set TCP_MAXSEG to %d bytes\n", mydollytab->segsize);
    if(setsockopt(datasock, IPPROTO_TCP, TCP_MAXSEG,
		  &mydollytab->segsize, sizeof(int)) < 0) {
      (void) fprintf(stderr,"setsockopt: TCP_MAXSEG failed! errno=%d\n", errno);
      // exit(1);
    }
  }
  
  /* Attempt to set input BUFFER sizes */
  if(mydollytab->flag_v) { fprintf(stderr, "Buffer size: %d\n", SCKBUFSIZE); }
  if(setsockopt(datasock, SOL_SOCKET, SO_RCVBUF, &SCKBUFSIZE,sizeof(SCKBUFSIZE)) < 0) {
    (void) fprintf(stderr, "setsockopt: SO_RCVBUF failed! errno = %d\n",
		   errno);
    exit(556);
  }
  getsockopt(datasock, SOL_SOCKET, SO_RCVBUF,
	     (char *) &recv_size, (void *) &sizeofint);
  if(mydollytab->flag_v) {
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

static void open_outsocks(struct dollytab * mydollytab) {
  struct hostent *hent;
  struct sockaddr_in addrdata, addrctrl;
  int ret;
  int dataok = 0, ctrlok = 0, retry_count = 0;
  unsigned int i;
  int optval;
  unsigned int max = 0;
  char hn[256+32];
  int send_size, sizeofint = sizeof(int);

  if(mydollytab->nr_childs > 1) {
    max = mydollytab->nr_childs;
  } else if(mydollytab->add_nr > 0) {
    max = mydollytab->add_nr + 1;
  } else {
    max = 1;
  }
  for(i = 0; i < max; i++) {  /* For all childs we have */
    if(mydollytab->nr_childs > 1) {
      strcpy(hn, mydollytab->hostring[mydollytab->nexthosts[i]]);
    } else if(mydollytab->add_nr > 0) {
      if(mydollytab->add_mode == 1) {
	strcpy(hn, mydollytab->hostring[mydollytab->nexthosts[0]]);
	if(i > 0) {
	  strcat(hn, mydollytab->add_name[i - 1]);
	}
      } else if(mydollytab->add_mode == 2) {
	if(i == 0) {
	  strcpy(hn, mydollytab->hostring[mydollytab->nexthosts[0]]);
	} else {
	  int j = 0;
	  while(!isdigit(mydollytab->hostring[mydollytab->nexthosts[0]][j])) {
	    hn[j] = mydollytab->hostring[mydollytab->nexthosts[0]][j];
	    j++;
	  }
	  hn[j] = 0;
	  strcat(hn, mydollytab->add_name[i - 1]);
	  strcat(hn, &mydollytab->hostring[mydollytab->nexthosts[0]][j]);
	}
      } else {
	fprintf(stderr, "Undefined add_mode %d!\n", mydollytab->add_mode);
	exit(1);
      }
    } else if (mydollytab->add_primary) {
      assert(i < 1);
      
      if(mydollytab->add_mode == 1) {
        strcpy(hn, mydollytab->hostring[mydollytab->nexthosts[0]]);
        strcat(hn, mydollytab->add_name[0]);
      } else if(mydollytab->add_mode == 2) {
        int j = 0;
        while(!isdigit(mydollytab->hostring[mydollytab->nexthosts[0]][j])) {
          hn[j] = mydollytab->hostring[mydollytab->nexthosts[0]][j];
          j++;
        }
        hn[j] = 0;
        strcat(hn, mydollytab->add_name[0]);
        strcat(hn, &mydollytab->hostring[mydollytab->nexthosts[0]][j]);
      } else {
        fprintf(stderr, "Undefined add_mode %d!\n", mydollytab->add_mode);
        exit(1);
      }
    } else {
      assert(i < 1);
      strcpy(hn, mydollytab->hostring[mydollytab->nexthosts[i]]);
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
    
    if(mydollytab->flag_v) {
      fprintf(stderr, "Connecting to host %s... ", hn);
      fflush(stderr);
    }
    
    /* Wait until we connected to everything... */
    dataok  = ctrlok = 0;
    do {
      dataout[i] = socket(PF_INET, SOCK_STREAM, 0);
      if(dataout[i] == -1) {
        perror("Opening output data socket");
        exit(1);
      }

      if((mydollytab->nr_childs > 1) || (i == 0)) {
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
        exit(1);
      }

      if(mydollytab->segsize > 0) {
	/* Attempt to set TCP_MAXSEG */
        fprintf(stderr, "Set TCP_MAXSEG to %d bytes\n", mydollytab->segsize);
        if(setsockopt(dataout[i], IPPROTO_TCP, TCP_MAXSEG,
		      &mydollytab->segsize, sizeof(int)) < 0) {
	  (void) fprintf(stderr, "setsockopt: TCP_MAXSEG failed! errno = %d\n", 
			 errno);
      exit(1);
	}
      }
      if(setsockopt(dataout[i], SOL_SOCKET, SO_SNDBUF,&SCKBUFSIZE,sizeof(SCKBUFSIZE)) < 0) {
          (void) fprintf(stderr, "setsockopt: SO_SNDBUF failed! errno = %d\n", errno);
          exit(556);
      }
      getsockopt(dataout[i], SOL_SOCKET, SO_RCVBUF,
           (char *) &send_size, (void *) &sizeofint);
      fprintf(stderr, "Send buffer %d is %d bytes\n", i, send_size);

      ///* Setup data port */
      addrdata.sin_family = hent->h_addrtype;
      addrdata.sin_port = htons(dataport);
      bcopy(hent->h_addr, &addrdata.sin_addr, hent->h_length);

      if((mydollytab->nr_childs > 1) || (i == 0)) {
        /* Setup control port */
        addrctrl.sin_family = hent->h_addrtype;
        addrctrl.sin_port = htons(ctrlport);
        bcopy(hent->h_addr, &addrctrl.sin_addr, hent->h_length);
      }
      
      if(!dataok) {
        retry_count = 0;
        do {
          ret = connect(dataout[i],
                  (struct sockaddr *)&addrdata, sizeof(addrdata));
          retry_count++;
        } while(retry_count < max_retries && ret == -1 && errno != ECONNREFUSED);
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
          if(mydollytab->add_nr > 0) {
            ret = write(dataout[i], &i, sizeof(i));
            if(ret == -1) {
              perror("Write fd-nr in open_outsocks()");
              exit(1);
            }
          }
          if(mydollytab->flag_v) {
            fprintf(stderr, "data ");
            fflush(stderr);
          }
        }
      }
      if(!ctrlok) {
	if((mydollytab->nr_childs > 1) || (i == 0)) {
    retry_count = 0;
    do {
      ret = connect(ctrlout[i],
        (struct sockaddr *)&addrctrl, sizeof(addrctrl));
      retry_count++;
    } while(retry_count < max_retries && ret == -1 && errno != ECONNREFUSED);
	  if((ret == -1) && (errno == ECONNREFUSED)) {
	    close(ctrlout[i]);
	  } else if(ret == -1) {
	    perror("connect");
	    exit(1);
	  } else {
	    ctrlok = 1;
	    if(mydollytab->flag_v) {
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
    if(mydollytab->flag_v) {
      fprintf(stderr, "\b.\n");
    }
  }
}




static void buildring(struct dollytab * mydollytab) {
  socklen_t size;
  int ret = 0;
  unsigned int i = 0,j = 0,nr = 0;
  unsigned int ready_mach = 0;  /* Number of ready machines */
  char msg[1024];
  char info_buf[1024];

  if(!mydollytab->meserver) {
    /* Open the input sockets and wait for connections... */
    open_insocks(mydollytab);
    if(mydollytab->flag_v) {
      fprintf(stderr, "Accepting...");
      fflush(stderr);
    }
    
    /* All except the first accept a connection now */
    ctrlin = accept(ctrlsock, NULL, &size);
    if(ctrlin == -1) {
      perror("accept input control socket");
      exit(1);
    }
    if(mydollytab->flag_v) {
      fprintf(stderr, "control...\n");
      fflush(stderr);
    }
    
    /* Clients should now read everything from the ctrl-socket. */
    getparams(ctrlin,mydollytab);
    if(mydollytab->flag_v) {
      print_params(mydollytab);
    }

    datain[0] = accept(datasock, NULL, &size);
    if(datain[0] == -1) {
      perror("accept input data socket");
      exit(1);
    }
    if(mydollytab->add_nr > 0) {
      ret = read(datain[0], &nr, sizeof(nr));
      if(ret == -1) {
        perror("First read for nr in buildring");
        exit(1);
      }
      assert(nr == 0);
      for(i = 0; i < mydollytab->add_nr; i++) {
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
    if(mydollytab->flag_v) {
      fprintf(stderr, "Connected data...done.\n");
    }
    /* The input sockets are now connected. */

    /* Give information back to server */
    sprintf(msg, "Host got parameters '%s'.\n", mydollytab->myhostname);
    ret = movebytes(ctrlin, WRITE, msg, strlen(msg),mydollytab);
    if((unsigned int) ret != strlen(msg)) {
      fprintf(stderr,
	      "Couldn't write got-parameters-message back to server "
	      "(sent %d instead of %zu bytes).\n",
	      ret, strlen(msg));
    }
  }

  if(mydollytab->meserver) {
    open_infile(1,mydollytab);
  } else {
    open_outfile(1,mydollytab);
  }

  /* All the machines except the leaf(s) need to open output sockets */
  if(!mydollytab->melast) {
    open_outsocks(mydollytab);
  }
  
  /* Finally, the first machine also accepts a connection */
  if(mydollytab->meserver) {
    char buf[mydollytab->t_b_size];
    ssize_t readsize;
    int fd, ret, maxsetnr = -1;
    fd_set real_set, cur_set;
    
    /* Send out dollytab */
    fd = open(dollytab, O_RDONLY);
    if(fd == -1) {
      perror("open dollytab");
      exit(1);
    }
    readsize = read(fd, buf, mydollytab->t_b_size);
    if(readsize == -1) {
      perror("read dollytab");
      exit(1);
    } else if(readsize == mydollytab->t_b_size) {
      fprintf(stderr, "Dollytab possibly too long, adjust program...\n");
      exit(1);
    } else if(readsize == 1448) {
      buf[1448] = ' ';
      readsize += 1;
    }
    for(i = 0; i < mydollytab->nr_childs; i++) {
      movebytes(ctrlout[i], WRITE, buf, readsize,mydollytab);
    }
    close(fd);


    /* Wait for backflow-information or the data socket connection */
    fprintf(stderr, "Waiting for ring to build...\n");
    FD_ZERO(&real_set);
    maxsetnr = -1;
    for(i = 0; i < mydollytab->nr_childs; i++) {
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
      for(j = 0; j < mydollytab->nr_childs; j++) {
        if(FD_ISSET(ctrlout[j], &cur_set)) {
          ret = read(ctrlout[j], info_buf, 1024);
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
            if(mydollytab->flag_v) {
              fprintf(stderr, info_buf);
            }
            while((p = strstr(p, "ready")) != NULL) {
              ready_mach++;
              p++;
            }
            fprintf(stderr,
              "Machines left to wait for: %d\n", mydollytab->hostnr - ready_mach);
          }
        }
      } /* For all childs */
    } while(ready_mach < mydollytab->hostnr);
  }

  if(mydollytab->flag_v) {
    fprintf(stderr, "Accepted.\n");
  }
  
  if(!mydollytab->meserver) {
    /* Send it further */
    if(!mydollytab->melast) {
      for(i = 0; i < mydollytab->nr_childs; i++) {
        movebytes(ctrlout[i], WRITE, mydollytab->dollybuf, mydollytab->dollybufsize,mydollytab);
      }
    }
    
    /* Give information back to server */
    sprintf(msg, "Host ready '%s'.\n", mydollytab->myhostname);
    ret = movebytes(ctrlin, WRITE, msg, strlen(msg),mydollytab);
    if((unsigned int) ret != strlen(msg)) {
      fprintf(stderr,
	      "Couldn't write ready-message back to server "
	      "(sent %d instead of %zu bytes).\n",
	      ret, strlen(msg));
    }
  }
}


static void usage(void) {
  fprintf(stderr, "\n");
  fprintf(stderr,
	  "Usage: dolly [-hVvSsnYR] [-c <size>] [-b <size>] [-u <size>] [-d] [-f configfile] "
	  "[-o logfile] [-t time] -I [inputfile] -O [outpufile] -H [hostnames]\n");
  fprintf(stderr, "\t-s: this is the server, check hostname\n");
  fprintf(stderr, "\t-S <hostname>: use hostname as server\n");
  fprintf(stderr, "\t-R: resolve the hostnames to ipv4 addresses\n");
  fprintf(stderr, "\t-6: resolve the hostnames to ipv6 addresses\n");
  fprintf(stderr, "\t-v: verbose\n");
  fprintf(stderr, "\t-b <size>, where size is the size of block to transfer (default 4096)\n");
  fprintf(stderr, "\t-u <size>, size of the buffer (multiple of 4K)\n");
  fprintf(stderr, "\t-c <size>, where size is uncompressed size of "
	  "compressed inputfile\n\t\t(for statistics only)\n");

  fprintf(stderr, "\t-f <configfile>, where <configfile> is the "
	  "configuration file with all\n\t\tthe required information for "
	  "this run. Required on server only.\n");
  fprintf(stderr, "\t-o <logfile>: Write some statistical information  "
	  "in <logfile>\n");
  fprintf(stderr, "\t-r <n>: Retry to connect to mode n times\n");
  fprintf(stderr, "\t-a <timeout>: Lets dolly terminate if it could not transfer\n\t\tany data after <timeout> seconds.\n");
  fprintf(stderr, "\t-n: Do not sync before exit. Dolly exits sooner.\n");
  fprintf(stderr, "\t    Data may not make it to disk if power fails soon after dolly exits.\n");
  fprintf(stderr, "\t-h: Print this help and exit\n");
  fprintf(stderr, "\t-q: Suppresss \"ignored signal\" messages\n");
  fprintf(stderr, "\t-V: Print version number and exit\n");
  fprintf(stderr, "\tFollowing options can be used instead of a dollytab and\n");
  fprintf(stderr, "\timply the -S or -s option which must me prceeded.\n");
  fprintf(stderr, "\t-H: comma seperated list of the hosts to send to\n");
  fprintf(stderr, "\t-I: input file\n");
  fprintf(stderr, "\t-O: output file (just - for output to stdout)\n");
  fprintf(stderr, "version: %s\n",version_string);
  fprintf(stderr, "\nDolly was part of the ETH Patagonia cluster project, ");
  fprintf(stderr, "\n");
  exit(1);
}

int main(int argc, char *argv[]) {
  int c;
  unsigned int i;
  int flag_f = 0, flag_cargs = 0, generated_dolly = 0, me = -2;
  FILE *df;
  char *mnname = NULL, *tmp_str, *host_str, *a_str, *sp, *ip_addr;
  size_t nr_hosts = 0;
  int fd;
  struct dollytab* mydollytab = (struct dollytab*)malloc(sizeof(struct dollytab));
  struct sockaddr_in sock_address;
  init_dollytab(mydollytab);


  /* Parse arguments */
  while(1) {
    c = getopt(argc, argv, "a:b:c:f:r:u:vqo:S:shnR46:V:I:O:Y:H:");
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
    case 'R':
      if(mydollytab->resolve != 6 || mydollytab->resolve != 4) {
        mydollytab->resolve = 1;
      }
      break;
    case '6':
      mydollytab->resolve = 6;
      break;
    case '4':
      mydollytab->resolve = 4;
      break;
    case 'v':
      /* Verbose */
      mydollytab->flag_v = 1;
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
      mydollytab->compressed_in = 1;
      maxcbytes = atoi(optarg);
      break;
    case 'b':
      mydollytab->t_b_size = atoi(optarg);
      break;
    case 'u':
      buffer_size = atoi(optarg);
      break;
    case 'n':
      dosync = 0;
      break;
    case 's':
      /* This machine is the server. */
      mydollytab->meserver = 1;
      break;
    case 'S':
      /* This machine is the server - don't check hostname. */
      mydollytab->meserver = 1;
      if(strcmp(optarg,"-") < 0) {
        fprintf(stderr,"'%s' is not a valid servername\n",optarg);
        exit(1);
      }
      strncpy(mydollytab->servername,optarg,strlen(optarg));
      break;
    case 'a':
      i = atoi(optarg);
      if(i <= 0) {
        fprintf(stderr, "Timeout of %d doesn't make sense.\n", i);
        exit(1);
      }
      timeout = i;
      if(mydollytab->flag_v) {
        fprintf(stderr, "Will set timeout to %d seconds.\n", timeout);
      }
      signal(SIGALRM, alarm_handler);
      break;
    case  'r':
      max_retries = atoi(optarg);
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
    case 'I':
      if(strlen(optarg) > 255) {
        fprintf(stderr, "Name of input-file too long.\n");
        exit(1);
      }
      if (mydollytab->meserver == 0) {
        fprintf(stderr,"the -S/-s must preceed the -I option\n");
        exit(1);
      }
      strncpy(mydollytab->infile,optarg,strlen(optarg)); 
      flag_cargs = 1;
      break;

    case 'O':
      if(strlen(optarg) > 255) {
        fprintf(stderr, "Name of output-file too long.\n");
        exit(1);
      }
      if (mydollytab->meserver == 0) {
        fprintf(stderr,"the -S/-s must preceed the -O option\n");
        exit(1);
      }
      strncpy(mydollytab->outfile,optarg,strlen(optarg)); 
      flag_cargs = 1;
      break;

    case 'Y':
      mydollytab->hyphennormal = 1;
      break;

    case 'H':
      if (mydollytab->meserver == 0) {
        fprintf(stderr,"the -S/-s must preceed the -H option\n");
        exit(1);
      }
      /* copying string as it is modified*/
      a_str = strdup(optarg);
      tmp_str = a_str;
      while(*tmp_str) {
        if(*host_delim == *tmp_str) {
          nr_hosts++;
        }
        tmp_str++;
      }
      nr_hosts++;
      mydollytab->hostnr = nr_hosts;
      mydollytab->hostring = (char**) malloc(nr_hosts * sizeof(char *));
      /* now find the first host */
      host_str = strtok(a_str,host_delim);
      nr_hosts = 0;
      /* check if have to resolve the hostnames */
      while(host_str != NULL) {
        if(mydollytab->resolve == 0 && 
           inet_pton(AF_INET,host_str,&(sock_address.sin_addr)) == 1 &&
           inet_pton(AF_INET6,host_str,&(sock_address.sin_addr)) == 1) {
          mydollytab->hostring[nr_hosts] = (char *)malloc(strlen(host_str)+1);
          strncpy(mydollytab->hostring[nr_hosts], host_str,strlen(host_str));
        } else { 
          /* get memory for ip address */
          ip_addr = (char*)malloc(sizeof(char)*256);
          if(resolve_host(host_str,ip_addr,mydollytab->resolve)) {
            fprintf(stderr,"Could not resolve the host '%s'\n",host_str);
            exit(1);
          }
          mydollytab->hostring[nr_hosts] = (char *)malloc(strlen(ip_addr)+1);
          if(!mydollytab->hostring[nr_hosts]) {
            fprintf(stderr,"Could not get memory for hostring!\n");
            exit(1);
          }
          strcpy(mydollytab->hostring[nr_hosts],ip_addr);
          free(ip_addr);
        }
        host_str = strtok(NULL,host_delim);
        if(strcmp(mydollytab->hostring[nr_hosts], mydollytab->myhostname) == 0) {
          me = nr_hosts;
        } else if(!mydollytab->hyphennormal) {
          /* Check if the hostname is correct, but a different interface is used */
          if((sp = strchr(mydollytab->hostring[nr_hosts], '-')) != NULL) {
            if(strncmp(mydollytab->hostring[nr_hosts], mydollytab->myhostname, sp - mydollytab->hostring[nr_hosts]) == 0) {
              me = nr_hosts;
            }
          }
        }
        nr_hosts++;
      }
      /* Build up topology */
      mydollytab->nr_childs = 0;
      for(i = 0; i < mydollytab->fanout; i++) {
        if(mydollytab->meserver) {
          if(i + 1 <= mydollytab->hostnr) {
            mydollytab->nexthosts[i] = i;
            mydollytab->nr_childs++;
          }
        } else {
          if((me + 1) * mydollytab->fanout + 1 + i <= mydollytab->hostnr) {
            mydollytab->nexthosts[i] = (me + 1) * mydollytab->fanout + i;
            mydollytab->nr_childs++;
          }
        }
      }
      /* In a tree, we might have multiple last machines. */
      if(mydollytab->nr_childs == 0) {
        mydollytab->melast = 1;
      }

      free(a_str);
      /* make sure that we are the server */
      mydollytab->meserver = 1;
      flag_cargs = 1;
      break;
      
    default:
      fprintf(stderr, "Unknown option '%c'.\n", c);
      exit(1);
    }
    if(flag_cargs) {
      /* only use HOST when servername or ip is not explictly set */
      if(strcmp(mydollytab->servername,"") == 0) {
        mnname = getenv("HOST");
        if(mydollytab->resolve != 0) {
          ip_addr = (char*)malloc(sizeof(char)*256);
          if(resolve_host(mnname,ip_addr,mydollytab->resolve)) {
            fprintf(stderr,"Could resolve the server address '%s'\n",mydollytab->servername);
            exit(1);
          }
          strncpy(mydollytab->myhostname,ip_addr,strlen(ip_addr));
          strncpy(mydollytab->servername,ip_addr,strlen(ip_addr));
          free(ip_addr);
        } else {
          strncpy(mydollytab->myhostname,mnname,strlen(mnname));
          strncpy(mydollytab->servername,mnname,strlen(mnname));
        }
      } else {
        /* check if we allready have a valid ip address */
        if(inet_pton(AF_INET,mydollytab->servername,&(sock_address.sin_addr)) == 0 &&
           inet_pton(AF_INET6,mydollytab->servername,&(sock_address.sin_addr)) == 0 &&
           mydollytab->resolve != 0) {
          ip_addr = (char*)malloc(sizeof(char)*256);
          if(resolve_host(mydollytab->servername,ip_addr,mydollytab->resolve)) {
            fprintf(stderr,"Could resolve the server address '%s'\n",mydollytab->servername);
            exit(1);
          }
          strncpy(mydollytab->servername,ip_addr,strlen(ip_addr));
          strncpy(mydollytab->myhostname,ip_addr,strlen(ip_addr));
          free(ip_addr);
        } else {
          strncpy(mydollytab->myhostname,mydollytab->servername,strlen(mydollytab->servername));
        }
      }

    }
    if(flag_f && !flag_cargs) {
      /* Open the config-file */
      df = fopen(optarg, "r");
      if(df == NULL) {
        char errstr[256];
        sprintf(errstr, "fopen dollytab '%s'", optarg);
        perror(errstr);
        exit(1);
      }
      parse_dollytab(df,mydollytab);
      fclose(df);
    }
  }

  /* Did we get the parameters we need? */
  if(mydollytab->meserver && !flag_f && !flag_cargs) {
    fprintf(stderr, "Missing parameter -f <configfile>\n");
    exit(1);
  }
  if(flag_cargs) {
    if(strlen(mydollytab->outfile) == 0) {
      fprintf(stderr,"outfile via '-O FILE' must be set\n");
      exit(1);
    }
    if(strlen(mydollytab->infile) == 0) {
      fprintf(stderr,"inputfile via '-I [FILE|-]' must be set\n");
      exit(1);
    }
    if(strlen(dollytab) == 0) {
      generated_dolly = 1;
      strcpy(dollytab,"/tmp/dollygenXXXXXX");
      fd = mkstemp(dollytab);
      df = fdopen(fd,"w");
      if(df == NULL) {
        printf("Could not open temporary dollytab");
      }
      fprintf(df,"infile %s\n",mydollytab->infile);
      fprintf(df,"outfile %s\n",mydollytab->outfile);
      fprintf(df,"server %s\n",mydollytab->myhostname);
      fprintf(df,"firstclient %s\n",mydollytab->hostring[0]);
      fprintf(df,"lastclient %s\n",mydollytab->hostring[nr_hosts-1]);
      fprintf(df,"clients %i\n",mydollytab->hostnr);
      for(i = 0; i < mydollytab->hostnr; i++) {
        fprintf(df,"%s\n",mydollytab->hostring[i]);
        fprintf(stderr,"writing '%s'\n'",mydollytab->hostring[i]);
      }
      fprintf(df,"endconfig\n");
      fclose(df);
    }
  }

  if(mydollytab->meserver && mydollytab->flag_v) {
    print_params(mydollytab);
  }

  /* try to open standard terminal output */
  /* if it fails, redirect stdtty to stderr */
  stdtty = fopen("/dev/tty","a");
  if (stdtty == NULL) {
      stdtty = stderr;
    }

  if(mydollytab->flag_v) {
    fprintf(stderr, "\nTrying to build ring...\n");
  }
  
  alarm(timeout);

  buildring(mydollytab);
  
  if(mydollytab->meserver) {
    fprintf(stdtty, "Server: Sending data...\n");
  } else {    
    if(mydollytab->flag_v) {
      fprintf(stdtty, "Receiving...\n");
    }
  }

  transmit(mydollytab);
  /* remove the generated dollytab */
  if(generated_dolly) {
    unlink(dollytab);
  }
  close(datain[0]);
  close(ctrlin);
  close(datasock);
  close(ctrlsock);
  for(i = 0; i < mydollytab->nr_childs; i++) {
    close(ctrlout[i]);
    close(dataout[i]);
  }
  if(mydollytab->flag_v) {
    fprintf(stderr, "\n");
  }

  fclose(stdtty);
  for(i = 0; i < mydollytab->hostnr; i++) {
    free(mydollytab->hostring[i]);
  }
  free(mydollytab->dollybuf);
  free(mydollytab->hostring);
  free(mydollytab);
 
  exit(0);
}
