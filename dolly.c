#include "dolly.h"

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
  if(!dummy_mode) {
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
  } else {
    fprintf(stderr, "dummy filesize = %d MB\n", mydollytab->dummysize/1024/1024);
  }
  fprintf(stderr, "using data port %u\n", dataport);
  fprintf(stderr, "using ctrl port %u\n", ctrlport);
  fprintf(stderr, "myhostname = '%s'\n", mydollytab->myhostname);
  if(segsize > 0) {
    fprintf(stderr, "TCP segment size = %d\n", segsize);
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
    fprintf(stderr, "\t'%s'\n", hostring[i]);
  }
  fprintf(stderr, "Next hosts in ring:\n");
  if(mydollytab->nr_childs == 0) {
    fprintf(stderr, "\tnone.\n");
  } else {
    for(i = 0; i < mydollytab->nr_childs; i++) {
      fprintf(stderr, "\t%s (%d)\n",
	      hostring[mydollytab->nexthosts[i]], mydollytab->nexthosts[i]);
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
  fprintf(stderr, "Using transfer size %d bytes.\n", T_B_SIZE);
}

static void open_insocks(struct dollytab * mydollytab) {
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
  if(mydollytab->flag_v) { fprintf(stderr, "Buffer size: %d\n", SCKBUFSIZE); }
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
  int i;
  int optval;
  int max;
  char hn[256+32];
  char *dsndbuf = NULL;
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
      strcpy(hn, hostring[mydollytab->nexthosts[i]]);
    } else if(mydollytab->add_nr > 0) {
      if(mydollytab->add_mode == 1) {
	strcpy(hn, hostring[mydollytab->nexthosts[0]]);
	if(i > 0) {
	  strcat(hn, mydollytab->add_name[i - 1]);
	}
      } else if(mydollytab->add_mode == 2) {
	if(i == 0) {
	  strcpy(hn, hostring[mydollytab->nexthosts[0]]);
	} else {
	  int j = 0;
	  while(!isdigit(hostring[mydollytab->nexthosts[0]][j])) {
	    hn[j] = hostring[mydollytab->nexthosts[0]][j];
	    j++;
	  }
	  hn[j] = 0;
	  strcat(hn, mydollytab->add_name[i - 1]);
	  strcat(hn, &hostring[mydollytab->nexthosts[0]][j]);
	}
      } else {
	fprintf(stderr, "Undefined add_mode %d!\n", mydollytab->add_mode);
	exit(1);
      }
    } else if (mydollytab->add_primary) {
      assert(i < 1);
      
      if(mydollytab->add_mode == 1) {
	strcpy(hn, hostring[mydollytab->nexthosts[0]]);
	strcat(hn, mydollytab->add_name[0]);
      } else if(mydollytab->add_mode == 2) {
	int j = 0;
	while(!isdigit(hostring[mydollytab->nexthosts[0]][j])) {
	  hn[j] = hostring[mydollytab->nexthosts[0]][j];
	  j++;
	}
	hn[j] = 0;
	strcat(hn, mydollytab->add_name[0]);
	strcat(hn, &hostring[mydollytab->nexthosts[0]][j]);
      } else {
        fprintf(stderr, "Undefined add_mode %d!\n", mydollytab->add_mode);
        exit(1);
      }
    } else {
      assert(i < 1);
      strcpy(hn, hostring[mydollytab->nexthosts[i]]);
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
    dsndbuf = NULL;
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

/*
 * If "try_hard" is 1, call must be succesful.
 * If try_hard is 1 and an input file can't be opened, the program terminates.
 * If try_hard is not 1 and an input file can't be opened, -1 is returend.
 */
static int open_infile(int try_hard,struct dollytab * mydollytab) {
  char name[256+16];

  /* Close old input file if there is one */
  if(input != -1) {
    if(close(input) == -1) {
      perror("close() in open_infile()");
      exit(1);
    }
  }
  if(mydollytab->input_split != 0) {
    sprintf(name, "%s_%d", mydollytab->infile, input_nr);
  } else {
    strcpy(name, mydollytab->infile);
  }
  
  /* Files for input/output */
  if(!mydollytab->compressed_out) {
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

static int open_outfile(int try_hard,struct dollytab * mydollytab) {
  char name[256+16];
  int is_device = 0;
  int is_pipe = 0;
  /* Close old output file, if there is one. */
  if(output != -1) {
    if(close(output) == -1) {
      perror("close() in open_outfile()");
      exit(1);
    }
  }
  if(mydollytab->output_split != 0) {
    sprintf(name, "%s_%d", mydollytab->outfile, output_nr);
  } else {
    strcpy(name, mydollytab->outfile);
  }
  /* check if file is under /dev, if not open even if the file does not exist. */
  if(strcmp("/dev/",name) > 0 ) {
    is_device = 1;
  }
  if(strcmp("-",name) == 0) {
    is_pipe = 1;
  }
  /* Setup the output files/pipes. */
  if(!mydollytab->compressed_in) {
    /* Output is to a file */
    if(!mydollytab->compressed_out && (mydollytab->output_split == 0) && is_device && !is_pipe) {
      /* E.g. partition-to-partition cloning */
      output = open(name, O_WRONLY);
    } else if(!mydollytab->compressed_out && !is_device && !is_pipe) {
      /* E.g. file to file cloning */
      output = open(name, O_WRONLY | O_CREAT, 0644);
    } else if(is_pipe) {
      output = 1;
    } else {
      /* E.g. partition-to-compressed-archive cloning */
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

static int movebytes(int fd, int dir, char *addr, unsigned int n,struct dollytab * mydollytab) {
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
      if(((unsigned int)ret < n) && mydollytab->compressed_out && mydollytab->meserver) {
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

static void buildring(struct dollytab * mydollytab) {
  socklen_t size;
  int ret;
  unsigned int i, nr;
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

  if(!dummy_mode) {
    if(mydollytab->meserver) {
      open_infile(1,mydollytab);
    } else {
      open_outfile(1,mydollytab);
    }
  }

  /* All the machines except the leaf(s) need to open output sockets */
  if(!mydollytab->melast) {
    open_outsocks(mydollytab);
  }
  
  /* Finally, the first machine also accepts a connection */
  if(mydollytab->meserver) {
    char buf[T_B_SIZE];
    ssize_t readsize;
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
      for(i = 0; i < mydollytab->nr_childs; i++) {
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
        movebytes(ctrlout[i], WRITE, dollybuf, dollybufsize,mydollytab);
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

/* The main transmitting function */
static void transmit(struct dollytab * mydollytab) {
  char *buf_addr, *buf;
  unsigned long long t, transbytes = 0, lastout = 0;
  unsigned int bytes = T_B_SIZE;
  int ret = 1, maxsetnr = 0;
  unsigned long td = 0, tdlast = 0;
  unsigned int i = 0, a = 0;
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
  if(!mydollytab->meserver) {
    FD_ZERO(&real_set);
    FD_SET(datain[0], &real_set);
    FD_SET(ctrlin, &real_set);
    ADJ_MAXSET(datain[0]);
    ADJ_MAXSET(ctrlin);
    if(!mydollytab->melast) {
      for(i = 0; i < mydollytab->nr_childs; i++) {
        FD_SET(ctrlout[i], &real_set);  /* Check ctrlout too for backflow */
        ADJ_MAXSET(ctrlout[i]);
      }
    }
    maxsetnr++;
  } else {
    FD_ZERO(&real_set);
    for(i = 0; i < mydollytab->nr_childs; i++) {
      FD_SET(ctrlout[i], &real_set);
      ADJ_MAXSET(ctrlout[i]);
    }
    maxsetnr++;
  }
#undef ADJ_MAXSET
  
  gettimeofday(&tv1, NULL);
  tv2 = tv1;
  
  while((mydollytab->meserver && (ret > 0)) || (!mydollytab->meserver && (t > 0))) {
    /* The server writes data as long has it can to the data stream.
       When there's nothing left, it writes the actual number of bytes
       to the control stream (as long long int).
    */
    if(mydollytab->meserver) {
      /*
       * Server part
       */
      if(!dummy_mode) {
        ret = movebytes(input, READ, buf, bytes,mydollytab);
      } else {
        if((mydollytab->dummysize > 0) && ((maxbytes + bytes) > mydollytab->dummysize)) {
          ret = mydollytab->dummysize - maxbytes;
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
        if(mydollytab->add_nr == 0) {
          for(i = 0; i < mydollytab->nr_childs; i++) {
            movebytes(dataout[i], WRITE, buf, ret,mydollytab);
          }
        } else {
          static unsigned int cur_out = 0;
          movebytes(dataout[cur_out], WRITE, buf, ret,mydollytab);
          cur_out++;
          if(cur_out > mydollytab->add_nr) {
            cur_out = 0;
          }
        }
      } else {
        /* Here: ret <= 0 */
        int res = -1;
        if(mydollytab->input_split) {
          res = open_infile(0,mydollytab);
        }
        if(!mydollytab->input_split || (res < 0)) {
          if(mydollytab->flag_v) {
            fprintf(stderr, "\nRead %llu bytes from file(s).\n", maxbytes);
          }
          if(mydollytab->add_nr == 0) {
            for(i = 0; i < mydollytab->nr_childs; i++) {
              (void)fprintf(stderr, "Writing maxbytes = %lld to ctrlout\n",
                maxbytes);
              movebytes(ctrlout[i], WRITE, (char *)&maxbytes, 8,mydollytab);
              shutdown(dataout[i], 2);
            }
          } else {
            (void)fprintf(stderr, "Writing maxbytes = %lld to ctrlout\n",
              maxbytes);
            movebytes(ctrlout[0], WRITE, (char *)&maxbytes, 8,mydollytab);
            for(i = 0; i <= mydollytab->add_nr; i++) {
              shutdown(dataout[i], 2);
            }
          }
      } else {
        /* Next input file opened */
        ret = 100000;
        continue;
      } /* end mydollytab->input_split */
    }
      //if(mydollytab->flag_v && (maxbytes - lastout >= 10000000)) {
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
	    if(!mydollytab->melast) {
	      for(i = 0; i < mydollytab->nr_childs; i++) {
		movebytes(ctrlout[i], WRITE, (char *)&maxbytes, 8,mydollytab);
	      }
	    }
	    t = maxbytes - transbytes;
	    if(mydollytab->flag_v) {
	      fprintf(stderr,"\nMax. bytes will be %llu bytes. %llu bytes left.\n", maxbytes, t);
	    }
	    FD_CLR(ctrlin, &real_set);
	    FD_CLR(ctrlin, &cur_set);
	  } else if(FD_ISSET(datain[0], &cur_set)) {
	    /* There is data to be read from the net */
	    bytes = (t >= T_B_SIZE ? T_B_SIZE : t);
	    ret = movebytes(datain[0], READ, buf, bytes,mydollytab);
	    if(!dummy_mode) {
	      if(!mydollytab->output_split) {
		movebytes(output, WRITE, buf, ret,mydollytab);
	      } else {
		/* Check if output file needs to be split. */
		if((transbytes / mydollytab->output_split)
		   != ((transbytes + ret) / mydollytab->output_split)) {
		  size_t old_part, new_part;
		  old_part = ret - (transbytes + ret) % mydollytab->output_split;
		  new_part = ret - old_part;
		  movebytes(output, WRITE, buf, old_part,mydollytab);
		  open_outfile(1,mydollytab);
		  movebytes(output, WRITE, buf + old_part, new_part,mydollytab);
		} else {
		  movebytes(output, WRITE, buf, ret,mydollytab);
		}
	      } /* end input_split */
	    }
	    if(!mydollytab->melast) {
	      for(i = 0; i < mydollytab->nr_childs; i++) {
          movebytes(dataout[i], WRITE, buf, ret,mydollytab);
	      }
	    }
	    transbytes += ret;
	    t -= ret;
	    FD_CLR(datain[0], &cur_set);
	    /* Handle additional network interfaces, if available */
	    for(a = 1; a <= mydollytab->add_nr; a++) {
	      bytes = (t >= T_B_SIZE ? T_B_SIZE : t);
	      ret = movebytes(datain[a], READ, buf, bytes,mydollytab);
	      if(!dummy_mode) {
          movebytes(output, WRITE, buf, ret,mydollytab);
	      }
	      if(!mydollytab->melast) {
          movebytes(dataout[a], WRITE, buf, bytes,mydollytab);
	      }
	      transbytes += ret;
	      t -= ret;
	    }
	  } else { /* FD_ISSET(ctrlin[]) */
	    int foundfd = 0;

	    for(i = 0; i < mydollytab->nr_childs; i++) {
	      if(FD_ISSET(ctrlout[i], &cur_set)) {
		/* Backflow of control-information, just pass it on */
		ret = read(ctrlout[i], buf, T_B_SIZE);
		if(ret == -1) {
		  perror("read backflow in transmit");
		  exit(1);
		}
		movebytes(ctrlin, WRITE, buf, ret,mydollytab);
		foundfd++;
		FD_CLR(ctrlout[i], &cur_set);
	      }
	    }
	    /* if nothing found */
	    if(foundfd == 0) {
	      fprintf(stderr,
		      "select returned without any ready fd, ret = %d.\n",
		      ret);
	      for(i = 0;(int) i < maxsetnr; i++) {
		if(FD_ISSET(i, &cur_set)) {
		  unsigned int j;
		  fprintf(stderr, "  file descriptor %d is set.\n", i);
		  for(j = 0; j < mydollytab->nr_childs; j++) {
		    if(FD_ISSET(ctrlout[j], &cur_set)) {
		      fprintf(stderr, "  (fd %d = ctrlout[%d])\n", i, j);
		    }
		  }
		  for(j = 0; j <= mydollytab->add_nr; j++) {
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
      if(mydollytab->flag_v && (transbytes - lastout >= 10000000)) {
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
  
  if(mydollytab->meserver) {
    fprintf(stdtty, "\rSent MB: %.0f.       \n", (float)maxbytes/1000000);
 
    if(flag_log){
      logfd = fopen(logfile, "a");
      if(logfd == NULL) {
        perror("open logfile");
        exit(1);
      }
      if(!dummy_mode) {
	if(mydollytab->compressed_in) {
	  fprintf(logfd, "compressed ");
	}
	fprintf(logfd, "infile = '%s'\n", mydollytab->infile);
	if(mydollytab->compressed_out) {
	  fprintf(logfd, "compressed ");
	}
	fprintf(logfd, "outfile = '%s'\n", mydollytab->outfile);
      } else {
	if(mydollytab->flag_v) {
	  fprintf(logfd, "Transfered block : %d MB\n", mydollytab->dummysize/1024/1024);
	} else {
	  fprintf(logfd, " %8d",
		  (unsigned int) mydollytab->dummysize > 0 ?(unsigned int) (mydollytab->dummysize/1024/1024) : (unsigned int)(maxbytes/1024LL/1024LL));
	}
      }
      if(mydollytab->flag_v) {
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
      
      if(mydollytab->flag_v) {
        fprintf(logfd, "Server : '%s'\n", mydollytab->myhostname);
        fprintf(logfd, "Fanout = %d\n", mydollytab->fanout);
        fprintf(logfd, "Nr of childs = %d\n", mydollytab->nr_childs);
        fprintf(logfd, "Nr of hosts = %d\n", mydollytab->hostnr);
      } else {
        fprintf(logfd, " %8d", mydollytab->hostnr);
      }
    }
  } else {
    fprintf(stderr, "Transfered MB: %.0f, MB/s: %.3f \n\n",
	    (float)transbytes/1000000, (float)transbytes/td);
    fprintf(stdtty, "\n");
  }
  
  close(output);
  if(dosync){
    sync();
    if(mydollytab->flag_v) {
      fprintf(stderr, "Synced.\n");
    }
  }
  if(mydollytab->meserver) {
    for(i = 0; i < mydollytab->nr_childs; i++) {
      if(mydollytab->flag_v) {
        fprintf(stderr, "Waiting for child %d.\n",i);
      }
      ret = movebytes(ctrlout[i], READ, buf, 8,mydollytab);
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
	    (double)maxbytes * mydollytab->hostnr / td);
    if(maxcbytes != 0) {
      fprintf(stderr, "Bytes written on each node: %llu\n", maxcbytes);
      fprintf(stderr, "MBytes/s written: %0.3f\n",
	      (double)maxcbytes / td);
      fprintf(stderr, "Aggregate MBytes/s written: %0.3f\n",
	      (double)maxcbytes * mydollytab->hostnr / td);
    }
    if(flag_log) {
      if(mydollytab->flag_v) {
	fprintf(logfd, "Time: %lu.%03lu\n", td / 1000000, td % 1000000);
	fprintf(logfd, "MBytes/s: %0.3f\n", (double)maxbytes / td);
	fprintf(logfd, "Aggregate MBytes/s: %0.3f\n",
		(double)maxbytes * mydollytab->hostnr / td);
	if(maxcbytes != 0) {
	  fprintf(logfd, "Bytes written on each node: %llu\n", maxcbytes);
	  fprintf(logfd, "MBytes/s written: %0.3f\n",
		  (double)maxcbytes / td);
	  fprintf(logfd, "Aggregate MBytes/s written: %0.3f\n",
		  (double)maxcbytes * mydollytab->hostnr / td);
	}
      } else {
	fprintf(logfd, "%4lu.%06lu  ", td / 1000000, td % 1000000);
	fprintf(logfd, "%4.6f  ", (double)maxbytes / td);
	fprintf(logfd, "%4.6f  ",
		(double)maxbytes * mydollytab->hostnr / td);
	if(maxcbytes != 0) {
	  fprintf(logfd, "Bytes written on each node: %llu\n", maxcbytes);
	  fprintf(logfd, "MBytes/s written: %0.3f\n",
		  (double)maxcbytes / td);
	  fprintf(logfd, "Aggregate MBytes/s written: %0.3f\n",
		  (double)maxcbytes * mydollytab->hostnr / td);
	}
	fprintf(logfd, "\n");
      }
      fclose(logfd);
    }
    
  } else if(!mydollytab->melast) {
    /* All clients except the last just transfer 8 bytes in the backflow */
    unsigned long long ll;
    for(i = 0; i < mydollytab->nr_childs; i++) {
      movebytes(ctrlout[i], READ, (char *)&ll, 8,mydollytab);
    }
    movebytes(ctrlin, WRITE, (char *)&ll, 8,mydollytab);
  } else if(mydollytab->melast) {
    movebytes(ctrlin, WRITE, (char *)&maxbytes, 8,mydollytab);
  }
  if(mydollytab->flag_v) {
    fprintf(stderr, "Transmitted.\n");
  }
  free(buf_addr);
  if(maxbytes == 0) {
    exitloop = 1;
  }
  
}

static void usage(void) {
  fprintf(stderr, "\n");
  fprintf(stderr,
	  "Usage: dolly [-hVvSsnYR] [-c <size>] [-b <size>] [-u <size>] [-d] [-f configfile] "
	  "[-o logfile] [-t time] -I [inputfile] -O [outpufile] -H [hostnames]\n");
  fprintf(stderr, "\t-s: this is the server, check hostname\n");
  fprintf(stderr, "\t-S: this is the server, do not check hostname\n");
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
  fprintf(stderr,
	  "\t-d: dummy-mode. Dolly just sends data over the net,\n\t\twithout "
	  "disk accesses. This ist mostly used to test switches.\n");
  fprintf(stderr, "\t-o <logfile>: Write some statistical information  "
	  "in <logfile>\n");
  fprintf(stderr, "\t-t <time>, where <time> is the run-time in seconds of this dummy-mode\n");
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
  init_dollytab(mydollytab);


  /* Parse arguments */
  while(1) {
    c = getopt(argc, argv, "a:b:c:f:r:u:vqo:SshndtR46:V:I:O:Y:H:");
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
      mydollytab->meserver = 1;
      break;

    case 'S':
      /* This machine is the server - don't check hostname. */
      mydollytab->meserver = 2;
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
    case 'C':
      flag_cargs = 1;
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
      hostring = (char**) malloc(nr_hosts * sizeof(char *));
      /* now find the first host */
      host_str = strtok(a_str,host_delim);
      nr_hosts = 0;
      /* check if have to resolve the hostnames */
      while(host_str != NULL) {
        if(mydollytab->resolve == 0) {
          hostring[nr_hosts] = (char *)malloc(strlen(host_str)+1);
          strcpy(hostring[nr_hosts], host_str);
        } else { 
          /* get memory for ip address */
          ip_addr = (char*)malloc(sizeof(char)*256);
          resolve_host(host_str,ip_addr,mydollytab->resolve);
          hostring[nr_hosts] = (char *)malloc(strlen(ip_addr)+1);
          strcpy(hostring[nr_hosts], ip_addr);
          free(ip_addr);
        }
        host_str = strtok(NULL,host_delim);
        if(strcmp(hostring[nr_hosts], mydollytab->myhostname) == 0) {
          me = nr_hosts;
        } else if(!mydollytab->hyphennormal) {
          /* Check if the hostname is correct, but a different interface is used */
          if((sp = strchr(hostring[nr_hosts], '-')) != NULL) {
            if(strncmp(hostring[nr_hosts], mydollytab->myhostname, sp - hostring[nr_hosts]) == 0) {
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
    if (flag_cargs) {
      mnname = getenv("HOST");
      (void)strcpy(mydollytab->myhostname, mnname);

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
      parse_dollytab(df,mydollytab);
      fclose(df);
    }
  }

  /* Did we get the parameters we need? */
  if(mydollytab->meserver && !flag_f && !flag_cargs) {
    fprintf(stderr, "Missing parameter -f <configfile> or -C for commandline arguments.\n");
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
      fprintf(df,"firstclient %s\n",hostring[0]);
      fprintf(df,"lastclient %s\n",hostring[nr_hosts-1]);
      fprintf(df,"clients %i\n",mydollytab->hostnr);
      for(i = 0; i < mydollytab->hostnr; i++) {
        fprintf(df,"%s\n",hostring[i]);
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

  do {
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

    if(!exitloop) {
      transmit(mydollytab);
    }
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
  } while (!mydollytab->meserver && dummy_mode && !exitloop);
 
  fclose(stdtty);
  free(mydollytab);
 
  exit(0);
}
