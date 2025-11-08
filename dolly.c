#include <dirent.h>
#include "dolly.h"
#include "dollytab.h"
#include "sha256.h"
#include "transmit.h"
#include "files.h"
#include "movebytes.h"
#include "ping.h"

/* Clients need the ports before they can listen, so we use defaults. */
const unsigned int dataport = 9998;
const unsigned int ctrlport = 9997;
const unsigned int systemdport = 9996;
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

unsigned long long maxcbytes = 0;/*     --  "  --  in compressed mode */
unsigned long long maxbytes = 0; /* max bytes to transfer */
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

  //fprintf(stderr, "infile = '%s'", mydollytab->infile);
  if(mydollytab->input_split != 0) {
    fprintf(stderr, ", splitted in parts.\n");
  } else {
    fprintf(stderr, "\n");
  }

  if (mydollytab->flag_v) {
    fprintf(stderr, "\n### Server Configuration and Details\n");
    fprintf(stderr, "| %-36s | %-40s |\n", "Parameter", "Value");
    fprintf(stderr, "| %-36s | %-40s |\n", "------------------------------------", "----------------------------------------");
    fprintf(stderr, "| %-36s | %-40s |\n", "Hostname", mydollytab->myhostname);
    fprintf(stderr, "| %-36s | %-40s |\n", "Server Role", (mydollytab->meserver ? "I'm the server" : "I'm not the server"));
    fprintf(stderr, "| %-36s | %-40s |\n", "Last client", (mydollytab->melast ? "I'm the last client" : "I'm not the last client"));
    fprintf(stderr, "| %-36s | %-40u |\n", "Control Port", ctrlport);
    fprintf(stderr, "| %-36s | %-40u |\n", "Data Port", dataport);
    if (mydollytab->meserver) {
      fprintf(stderr, "| %-36s | %-40s |\n", "Input File", mydollytab->infile);
      fprintf(stderr, "| %-36s | %-40s |\n", "Output File", mydollytab->outfile);
      fprintf(stderr, "| %-36s | %-40s |\n", "Directory List", mydollytab->directory_list);
    }
    if (!mydollytab->meserver) {
      fprintf(stdtty, "| %-36s | %-40u |\n", "I'm number", mydollytab->hostnr);
    }
    fprintf(stderr, "| %-36s | %-40u |\n", "Fanout", mydollytab->fanout);
    fprintf(stderr, "| %-36s | %-40u |\n", "Number of Childs", mydollytab->nr_childs);
    fprintf(stderr, "| %-36s | %-40u |\n", "Clients in Ring (excluding server)", mydollytab->hostnr);
    fprintf(stderr, "| %-36s | %-40u |\n", "Transfer Size", (unsigned int) mydollytab->t_b_size);
    fprintf(stderr, "\n");
  }

  if(mydollytab->nr_childs == 0) {
    fprintf(stderr, "\tnone.\n");
  } else {
    for(i = 0; i < mydollytab->nr_childs; i++) {
      fprintf(stderr, "\t%s (%d)\n",
	      mydollytab->hostring[mydollytab->nexthosts[i]], mydollytab->nexthosts[i]);
    }
  }
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
    fprintf(stderr, "Set TCP_MAXSEG to %u bytes\n", mydollytab->segsize);
    if(setsockopt(datasock, IPPROTO_TCP, TCP_MAXSEG,
		  &mydollytab->segsize, sizeof(int)) < 0) {
      (void) fprintf(stderr,"setsockopt: TCP_MAXSEG failed! errno=%d\n", errno);
      // exit(1);
    }
  }
  
  /* Attempt to set input BUFFER sizes */

  if(setsockopt(datasock, SOL_SOCKET, SO_RCVBUF, &SCKBUFSIZE,sizeof(SCKBUFSIZE)) < 0) {
    (void) fprintf(stderr, "setsockopt: SO_RCVBUF failed! errno = %d\n",
		   errno);
    exit(556);
  }
  getsockopt(datasock, SOL_SOCKET, SO_RCVBUF,
	     (char *) &recv_size, (void *) &sizeofint);

  
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

static void open_insystemdsocks(struct dollytab * mydollytab) {
  int clientSocket;
  unsigned int i;
  int Retval=-1;
  struct sockaddr_in serverAddr;
  socklen_t addr_size;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(systemdport);
  if(mydollytab->flag_v) {
    fprintf(stderr, "Start dolly client using systemd socket on %u hosts, port %i:\n", mydollytab->hostnr, systemdport);
  }
  for(i = 0; i < mydollytab->hostnr; i++) {
    clientSocket = socket(PF_INET, SOCK_STREAM, 0);
    if(mydollytab->flag_v) {
      fprintf(stderr, "\t'%s'\n", mydollytab->hostring[i]);
    }
    serverAddr.sin_addr.s_addr = inet_addr(mydollytab->hostring[i]);
    /* Connect to the systemd socket of all client nodes to start the dolly service */
    addr_size = sizeof serverAddr;
    Retval = connect(clientSocket, (struct sockaddr *) &serverAddr, addr_size);
    if (Retval < 0) {
      fprintf(stderr, "Connection failed to %s ! dolly service wont be started !\n (Please do a: systemctl start dolly.socket)\n (To always enable it: systemctl enable dolly.socket)\n", mydollytab->hostring[i]);
    }
    close(clientSocket);
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
    //(void)fprintf(stderr,"DEBUG gethostbyname on >%s<\n",hn);
    if(hent == NULL) {
      char str[strlen(hn)+34];
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
      fprintf(stderr, "Connecting to host %s...\n", hn);
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
        fprintf(stderr, "Set TCP_MAXSEG to %u bytes\n", mydollytab->segsize);
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
      //fprintf(stderr, "Send buffer %u is %d bytes\n", i, send_size);

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

  open_insocks(mydollytab);

  if (mydollytab->meserver) {
    if (!mydollytab->melast) {
      open_outsocks(mydollytab);
    }
  }

  if(!mydollytab->meserver) {
    fprintf(stderr, "DEBUG not a server\n");
    /* Open the input sockets and wait for connections... */
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

    if(mydollytab->password_required) {
      fprintf(stderr, "I am a client sending the token\n");
      unsigned char password_hash[SHA256_DIGEST_LENGTH];
      unsigned char nonce[SHA256_DIGEST_LENGTH];
      unsigned char client_response_hash[SHA256_DIGEST_LENGTH];

      fprintf(stderr, "Client: Raw password: %s\n", mydollytab->password);
      // Hash the client's password once
      hash_data((unsigned char *)mydollytab->password, strlen(mydollytab->password), password_hash);

      // Receive nonce from server
      if (receive_sha256_key(ctrlin, nonce) != 0) {
        fprintf(stderr, "Failed to receive nonce from server.\n");
        exit(1);
      }
      fprintf(stderr, "Client: Received nonce: ");
      for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
        fprintf(stderr, "%02x", nonce[k]);
      }
      fprintf(stderr, "\n");

      // Calculate client's response hash (password_hash + nonce)
      hash_data_with_nonce(password_hash, SHA256_DIGEST_LENGTH, nonce, SHA256_DIGEST_LENGTH, client_response_hash);
      fprintf(stderr, "Client: Generated response hash: ");
      for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
        fprintf(stderr, "%02x", client_response_hash[k]);
      }
      fprintf(stderr, "\n");

      // Send client's response hash to server
      send_sha256_key(ctrlin, client_response_hash);
      char ack[256 + 13 + 1]; // "AUTH_FAILED:" + hostname + null terminator
      memset(ack, 0, sizeof(ack));
      read(ctrlin, ack, sizeof(ack));
      if (strcmp(ack, "OK") != 0) {
	if (strncmp(ack, "AUTH_FAILED:", 13) == 0) {
	  fprintf(stderr, "Authentication failed for client: %s\n", ack + 13);
	} else {
	  fprintf(stderr, "Invalid password\n");
	}
	exit(1);
	fprintf(stderr, "DEBUG: This message should NOT appear if exit(1) works.\n");
      }
    }

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

  if(mydollytab->password_required) {
    if(mydollytab->meserver) {
      fprintf(stderr, "I am the server, doing authentication\n");
    }
    if(mydollytab->meserver) {
      open_infile(1,mydollytab);
    } else {
      open_outfile(mydollytab);
    }

    /* All the machines except the leaf(s) need to open output sockets */
    if(!mydollytab->melast && !mydollytab->meserver) {
      open_outsocks(mydollytab);
    }

    /* Finally, the first machine also accepts a connection */
    if(mydollytab->meserver) {
      char buf[mydollytab->t_b_size];
      ssize_t readsize;
      int fd, maxsetnr = -1;
      fd_set real_set, cur_set;

      if (mydollytab->password_required) {
        unsigned int i = 0;
        unsigned char server_password_hash[SHA256_DIGEST_LENGTH];
        unsigned char nonce[SHA256_DIGEST_LENGTH];

        generate_nonce(nonce);

        fprintf(stderr, "Server: Raw password: %s\n", mydollytab->password);
        // Hash the server's password once
        hash_data((unsigned char *)mydollytab->password, strlen(mydollytab->password), server_password_hash);

        while (i < mydollytab->nr_childs) {
	  unsigned char client_response_hash[SHA256_DIGEST_LENGTH];
	  unsigned char expected_response_hash[SHA256_DIGEST_LENGTH];

          // Send nonce to client
          if (send_sha256_key(ctrlout[i], nonce) != 0) {
            fprintf(stderr, "Failed to send nonce to client.\n");
            exit(1);
          }
          fprintf(stderr, "Server: Sent nonce to client %u: ", i);
          for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
            fprintf(stderr, "%02x", nonce[k]);
          }
          fprintf(stderr, "\n");

	  // Receive client's response hash
	  if (receive_sha256_key(ctrlout[i], client_response_hash) != 0) {
	    fprintf(stderr, "Failed to receive client response hash.\n");
	    exit(1);
	  }
          fprintf(stderr, "Server: Received client response hash from client %u: ", i);
          for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
            fprintf(stderr, "%02x", client_response_hash[k]);
          }
          fprintf(stderr, "\n");

          // Calculate expected response hash (server_password_hash + nonce)
          hash_data_with_nonce(server_password_hash, SHA256_DIGEST_LENGTH, nonce, SHA256_DIGEST_LENGTH, expected_response_hash);
          fprintf(stderr, "Server: Expected response hash for client %u: ", i);
          for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
            fprintf(stderr, "%02x", expected_response_hash[k]);
          }
          fprintf(stderr, "\n");

	  if (verify_sha256_key(expected_response_hash, client_response_hash)) {
            fprintf(stderr, "Server: Password verification successful for client %u.\n", i);
	    write(ctrlout[i], "OK", 3);
	    i++;
	  } else {
            fprintf(stderr, "Server: Password verification failed for client %u.\n", i);
	    char fail_msg[256 + 13]; // "AUTH_FAILED:" + hostname + null terminator
	    snprintf(fail_msg, sizeof(fail_msg), "AUTH_FAILED:%s", mydollytab->hostring[mydollytab->nexthosts[i]]);
	    write(ctrlout[i], fail_msg, strlen(fail_msg) + 1); // +1 for null terminator
	    fprintf(stderr, "Authentication failed for client: %s. Exiting.\n", fail_msg);
	    close(ctrlout[i]);
	    close(dataout[i]);
	    exit(1);
	  }
        }
      }

      for(i = 0; i < mydollytab->nr_childs; i++) {
	movebytes(ctrlout[i], WRITE, (char *)&mydollytab->directory_mode, sizeof(mydollytab->directory_mode), mydollytab);
      }

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
	      fprintf(stderr, "Client %s disconnected due to an authentication failure in the ring. Check log on this client.\n", mydollytab->hostring[mydollytab->nexthosts[j]]);
	      fprintf(stderr, "read returned 0 from backflow in buildring. Client disconnected.\n");
	      close(ctrlout[j]);
	      close(dataout[j]);
	      exit(1);
	    } else {
	      char *p;
            
	      p = info_buf;
	      info_buf[ret] = 0;	
	      if(mydollytab->flag_v) {
		fprintf(stderr, "%s", info_buf);
	      }
	      while((p = strstr(p, "ready")) != NULL) {
		ready_mach++;
		p++;
	      }
	      if(mydollytab->flag_v) {
		fprintf(stderr,
			"Machines left to wait for: %u\n", mydollytab->hostnr - ready_mach);
	      }
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
	if (mydollytab->password_required) {
          unsigned int i = 0;
          unsigned char server_password_hash[SHA256_DIGEST_LENGTH];
          unsigned char nonce[SHA256_DIGEST_LENGTH];

          generate_nonce(nonce);

          fprintf(stderr, "Intermediate Server: Raw password: %s\n", mydollytab->password);
          // Hash the server's password once
          hash_data((unsigned char *)mydollytab->password, strlen(mydollytab->password), server_password_hash);

          while (i < mydollytab->nr_childs) {
	    unsigned char client_response_hash[SHA256_DIGEST_LENGTH];
	    unsigned char expected_response_hash[SHA256_DIGEST_LENGTH];

            // Send nonce to client
            if (send_sha256_key(ctrlout[i], nonce) != 0) {
              fprintf(stderr, "Failed to send nonce to client.\n");
              exit(1);
            }
            fprintf(stderr, "Intermediate Server: Sent nonce to client %u: ", i);
            for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
              fprintf(stderr, "%02x", nonce[k]);
            }
            fprintf(stderr, "\n");

	    // Receive client's response hash
	    if (receive_sha256_key(ctrlout[i], client_response_hash) != 0) {
	      fprintf(stderr, "Failed to receive client response hash.\n");
	      exit(1);
	    }
            fprintf(stderr, "Intermediate Server: Received client response hash from client %u: ", i);
            for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
              fprintf(stderr, "%02x", client_response_hash[k]);
            }
            fprintf(stderr, "\n");

            // Calculate expected response hash (server_password_hash + nonce)
            hash_data_with_nonce(server_password_hash, SHA256_DIGEST_LENGTH, nonce, SHA256_DIGEST_LENGTH, expected_response_hash);
            fprintf(stderr, "Intermediate Server: Expected response hash for client %u: ", i);
            for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
              fprintf(stderr, "%02x", expected_response_hash[k]);
            }
            fprintf(stderr, "\n");

	    if (verify_sha256_key(expected_response_hash, client_response_hash)) {
              fprintf(stderr, "Intermediate Server: Password verification successful for client %u.\n", i);
	      write(ctrlout[i], "OK", 3);
	      i++;
	    } else {
              fprintf(stderr, "Intermediate Server: Password verification failed for client %u.\n", i);
	      char fail_msg[256 + 13]; // "AUTH_FAILED:" + hostname + null terminator
	      snprintf(fail_msg, sizeof(fail_msg), "AUTH_FAILED:%s", mydollytab->hostring[mydollytab->nexthosts[i]]);
	      write(ctrlout[i], fail_msg, strlen(fail_msg) + 1); // +1 for null terminator
	      fprintf(stderr, "Authentication failed for client: %s. Exiting.\n", fail_msg);
	      close(ctrlout[i]);
	      close(dataout[i]);
	      exit(0);
	    }
          }
	}
	for(i = 0; i < mydollytab->nr_childs; i++) {
	  movebytes(ctrlout[i], WRITE, (char *)&mydollytab->directory_mode, sizeof(mydollytab->directory_mode), mydollytab);
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
}

static void usage(void) {
  fprintf(stderr, "\n");
  fprintf(stderr, "Dolly v%s - Parallel disk/partition/data cloning tool\n", version_string);
  fprintf(stderr, "------------------------------------------------------\n");
  fprintf(stderr, "Dolly clones data to one or multiple nodes in parallel, saving time.\n");
  fprintf(stderr, "Without -s or -S, dolly runs as a client.\n\n");

  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  dolly [-hPVvSsnYR6d] [-c <size>] [-b <size>] [-u <size>] [-f configfile]\n");
  fprintf(stderr, "       [-o logfile] [-a timeout] [-I inputfile] [-D inputdir] [-O outputfile]\n");
  fprintf(stderr, "       [-H node1,node2,...] [-X excludedir]\n\n");

  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -s                Run as server (check hostname; not required if -H or -I is used)\n");
  fprintf(stderr, "  -S <hostname>     Use <hostname> as server\n");
  fprintf(stderr, "  -R                Resolve hostnames to IPv4 addresses\n");
  fprintf(stderr, "  -6                Resolve hostnames to IPv6 addresses\n");
  fprintf(stderr, "  -V                Print version and exit\n");
  fprintf(stderr, "  -D <inputdir>     Send a directory instead of a file\n");
  fprintf(stderr, "  -I <inputfile>    Input file\n");
  fprintf(stderr, "  -X <excludedir>   Comma-separated list of directories to exclude (e.g., /proc,/sys)\n");
  fprintf(stderr, "  -h                Print this help and exit\n");
  fprintf(stderr, "  -d                Connect to systemd socket on client nodes (port 9996)\n");
  fprintf(stderr, "  -v                Verbose mode\n");
  fprintf(stderr, "  -q                Suppress 'ignored signal' messages\n");
  fprintf(stderr, "  -P                Password for simple auth process\n");
  fprintf(stderr, "  -f <configfile>   Configuration file (required on server)\n");
  fprintf(stderr, "  -o <logfile>      Write statistics to <logfile>\n");
  fprintf(stderr, "  -a <timeout>      Terminate if no data transfer after <timeout> seconds\n");
  fprintf(stderr, "  -r <n>            Retry connection to node <n> times\n");
  fprintf(stderr, "  -n                Do not sync before exit (faster, but risk of data loss on power failure)\n");
  fprintf(stderr, "  -Y                instructs Dolly to treat the '-' character in hostnames as any other character.\n\n");

  fprintf(stderr, "Network Transfer Options:\n");
  fprintf(stderr, "  -b <size>         Block size for transfer (default: 4096)\n");
  fprintf(stderr, "  -u <size>         Buffer size (multiple of 4K)\n\n");

  fprintf(stderr, "Server Mode (alternative to dollytab):\n");
  fprintf(stderr, "  -H <hosts>        Comma-separated list of target hosts\n");
  fprintf(stderr, "  -O <outputfile>   Output file (use '-' for stdout; defaults to input filename)\n\n");

  fprintf(stderr, "Examples:\n");
  fprintf(stderr, "  # Client mode:\n");
  fprintf(stderr, "  dolly -v\n\n");
  fprintf(stderr, "  # Server mode:\n");
  fprintf(stderr, "  dolly -vs -H sle15sp32,sle15sp33,sle15sp34 -I files.tgz -O /tmp/files.tgz\n");
  fprintf(stderr, "    Copy files.tgz to /tmp/files.tgz on specified nodes (verbose)\n\n");
  fprintf(stderr, "  dolly -d -H sle15sp32,sle15sp33,sle15sp34 -I /tmp/files.tgz\n");
  fprintf(stderr, "    Use systemd socket to copy /tmp/files.tgz to nodes\n");

  exit(1);
}

int main(int argc, char *argv[]) {
  int c;
  unsigned int i;
  int flag_f = 0, flag_cargs = 0, generated_dolly = 0, me = -2;
  FILE *df;
  char *mnname = NULL, *tmp_str, *host_str, *a_str, *ip_addr;
  size_t nr_hosts = 0;
  int fd;
  struct dollytab* mydollytab = (struct dollytab*)malloc(sizeof(struct dollytab));
  struct sockaddr_in sock_address;
  init_dollytab(mydollytab);


  /* Parse arguments */
  while(1) {
    c = getopt(argc, argv, "a:b:c:f:r:u:vqo:S:shdnR46:VI:O:Y:H:D:P:X:");
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
      if(mydollytab->resolve != 6 && mydollytab->resolve != 4) {
        mydollytab->resolve = 1;
      }
      break;
    case '6':
      mydollytab->resolve = 6;
      break;
    case '4':
      mydollytab->resolve = 4;
      break;
    case 'D': {
      /* Directory mode */
      char *a_str = strdup(optarg);
      char *tmp_str = a_str;
      unsigned int num_dirs = 0;
      while(*tmp_str) {
        if(*host_delim == *tmp_str) {
          num_dirs++;
        }
        tmp_str++;
      }
      num_dirs++;
      mydollytab->num_infiles = num_dirs;
      mydollytab->infiles = (char**) malloc(num_dirs * sizeof(char *));

      char *dir_str = strtok(a_str, host_delim);
      num_dirs = 0;
      while(dir_str != NULL) {
        mydollytab->infiles[num_dirs] = (char *)malloc(strlen(dir_str) + 1);
        strcpy(mydollytab->infiles[num_dirs], dir_str);
        DIR* tocheck = opendir(mydollytab->infiles[num_dirs]);
        if (!tocheck) {
          fprintf(stderr, "Error ! %s is not a directory.\n", mydollytab->infiles[num_dirs]);
          exit(1);
        }
        closedir(tocheck);
        dir_str = strtok(NULL, host_delim);
        num_dirs++;
      }
      free(a_str);

      strcpy(mydollytab->directory_list, optarg);
      mydollytab->directory_mode = 1;
      mydollytab->meserver = 1;
      flag_cargs = 1;
      break;
    }
    case 'v':
      /* Verbose */
      mydollytab->flag_v = 1;
      break;
    case 'd':
      /* systemd socket activation */
      mydollytab->flag_d = 1;
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
      strcpy(mydollytab->servername,optarg);
      break;
    case 'P':
      if(strlen(optarg) > 255) {
        fprintf(stderr, "Password too long.\n");
        exit(1);
      }
      memset(mydollytab->password, 0, sizeof(mydollytab->password));
      strncpy(mydollytab->password, optarg, sizeof(mydollytab->password) - 1);
      mydollytab->password_required = 1;
      break;
    case 'a':
      i = atoi(optarg);
      if((int)i <= 0) {
        fprintf(stderr, "Timeout of %u doesn't make sense.\n", i);
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
      strcpy(mydollytab->infile,optarg);
      // check this is not a directory
      FILE* file = fopen(mydollytab->infile, "r");
      if (!file) {
        fprintf(stderr, "Error ! %s is not a file or a device.\n", mydollytab->infile);
        exit(1);
      }
      fclose(file);
      /* as -I is used automatically set this machine as the server. */
      mydollytab->meserver = 1;
      flag_cargs = 1;
      break;

    case 'O':
      if(strlen(optarg) > 255) {
        fprintf(stderr, "Name of output-file too long.\n");
        exit(1);
      }
      /* as -I is used automatically set this machine as the server. */
      mydollytab->meserver = 1;
      if (optarg[0] != '/') {
        char temp_outfile[sizeof(mydollytab->outfile) + 1];
        snprintf(temp_outfile, sizeof(temp_outfile), "/%s", optarg);
        strcpy(mydollytab->outfile, temp_outfile);
      } else {
        strcpy(mydollytab->outfile,optarg);
      }
      flag_cargs = 1;
      break;

    case 'Y':
      mydollytab->hyphennormal = 1;
      break;

    case 'H':
      /* as -H is used automatically set this machine as the server. */
      mydollytab->meserver = 1;

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
      
      char **reachable_hosts = (char**) malloc(nr_hosts * sizeof(char *));
      size_t reachable_nr_hosts = 0;

      // For Host Reachability Status table
      char **host_ips = (char**) malloc(nr_hosts * sizeof(char *));
      char **host_statuses = (char**) malloc(nr_hosts * sizeof(char *));
      size_t table_nr_hosts = 0;

      /* now find the first host */
      host_str = strtok(a_str,host_delim);
      
      while(host_str != NULL) {
        ip_addr = (char*)malloc(sizeof(char)*256);
        if(mydollytab->resolve == 0 && 
           inet_pton(AF_INET,host_str,&(sock_address.sin_addr)) == 1 &&
           inet_pton(AF_INET6,host_str,&(sock_address.sin_addr)) == 1) {
          strcpy(ip_addr, host_str);
        } else { 
          if(resolve_host(host_str,ip_addr,mydollytab->resolve)) {
            fprintf(stderr,"Could not resolve the host '%s'\n",host_str);
            host_str = strtok(NULL,host_delim);
            free(ip_addr);
            continue;
          }
        }

        if (is_host_reachable(ip_addr)) {
	  /*if(mydollytab->flag_v) {
	    fprintf(stderr, "Host '%s' (%s) is reachable. Adding to list.\n", host_str, ip_addr);
            }*/
	  reachable_hosts[reachable_nr_hosts] = (char *)malloc(strlen(ip_addr)+1);
	  if(!reachable_hosts[reachable_nr_hosts]) {
	    fprintf(stderr,"Could not get memory for hostring!\n");
	    exit(1);
	  }
	  strcpy(reachable_hosts[reachable_nr_hosts], ip_addr);
	  reachable_nr_hosts++;

	  // Store for table
	  host_ips[table_nr_hosts] = strdup(ip_addr);
	  host_statuses[table_nr_hosts] = strdup("Reachable");
	  table_nr_hosts++;
        } else {
	  if(mydollytab->flag_v) {
	    fprintf(stderr, "Client '%s' (%s) is unreachable. Skipping.\n", host_str, ip_addr);
	  }
	  // Store for table
	  host_ips[table_nr_hosts] = strdup(ip_addr);
	  host_statuses[table_nr_hosts] = strdup("Unreachable");
	  table_nr_hosts++;
        }
        free(ip_addr);
        host_str = strtok(NULL,host_delim);
      }

      free(a_str);

      if (mydollytab->flag_v) {
        fprintf(stderr, "\n### Client Reachability Status\n");
        fprintf(stderr, "| Client IP       | Status      |\n");
        fprintf(stderr, "| --------------- | ----------- |\n");
        for (i = 0; i < table_nr_hosts; i++) {
          fprintf(stderr, "| %-15s | %-11s |\n", host_ips[i], host_statuses[i]);
        }
        fprintf(stderr, "\n");
      }

      // Free memory for host_ips and host_statuses
      for (i = 0; i < table_nr_hosts; i++) {
        free(host_ips[i]);
        free(host_statuses[i]);
      }
      free(host_ips);
      free(host_statuses);

      mydollytab->hostnr = reachable_nr_hosts;
      mydollytab->hostring = malloc(mydollytab->hostnr * sizeof(char *));
      if (!mydollytab->hostring) {
        perror("malloc failed for hostring");
        exit(1);
      }
      for (i = 0; i < reachable_nr_hosts; i++) {
	mydollytab->hostring[i] = reachable_hosts[i];
      }
      free(reachable_hosts);

      // Re-evaluate 'me' based on the filtered hostring
      me = -2; // Reset me
      for (i = 0; i < mydollytab->hostnr; i++) {
	if (strcmp(mydollytab->hostring[i], mydollytab->myhostname) == 0) {
	  me = i;
	  break;
	} else if (!mydollytab->hyphennormal) {
	  char *sp = strchr(mydollytab->hostring[i], '-');
	  if (sp != NULL && strncmp(mydollytab->hostring[i], mydollytab->myhostname, sp - mydollytab->hostring[i]) == 0) {
	    me = i;
	    break;
	  }
	}
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

      /* make sure that we are the server */
      mydollytab->meserver = 1;
      flag_cargs = 1;
      break;

    case 'X': {
      char *a_str = strdup(optarg);
      char *tmp_str = a_str;
      unsigned int num_excludes = mydollytab->num_excludes;
      while(*tmp_str) {
        if(*host_delim == *tmp_str) {
          num_excludes++;
        }
        tmp_str++;
      }
      num_excludes++;
      mydollytab->excludes = (char**) realloc(mydollytab->excludes, num_excludes * sizeof(char *));

      char *exclude_str = strtok(a_str, host_delim);
      while(exclude_str != NULL) {
        mydollytab->excludes[mydollytab->num_excludes] = (char *)malloc(strlen(exclude_str) + 1);
        strcpy(mydollytab->excludes[mydollytab->num_excludes], exclude_str);
        mydollytab->num_excludes++;
        exclude_str = strtok(NULL, host_delim);
      }
      free(a_str);
      break;
    }

    default:
      fprintf(stderr, "Unknown option '%c'.\n", c);
      exit(1);
    }
    // always do hostname resolution
    //   if(flag_cargs) {
    /* only use HOST when servername or ip is not explictly set */
    if(strcmp(mydollytab->servername,"") == 0) {
      mnname = getenv("HOST");
      if(mydollytab->resolve != 0) {
	ip_addr = (char*)malloc(sizeof(char)*256);
	if(resolve_host(mnname,ip_addr,mydollytab->resolve)) {
	  fprintf(stderr,"Could resolve the server address '%s'\n",mydollytab->servername);
	  exit(1);
	}
	memcpy(mydollytab->myhostname,ip_addr,strlen(ip_addr));
	memcpy(mydollytab->servername,ip_addr,strlen(ip_addr));
	free(ip_addr);
      } else {
	memcpy(mydollytab->myhostname,mnname,strlen(mnname));
	memcpy(mydollytab->servername,mnname,strlen(mnname));
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
	memcpy(mydollytab->servername,ip_addr,strlen(ip_addr));
	memcpy(mydollytab->myhostname,ip_addr,strlen(ip_addr));
	free(ip_addr);
      } else {
	memcpy(mydollytab->myhostname,mydollytab->servername,strlen(mydollytab->servername));
      }
    }

    //    }
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
    if(strlen(mydollytab->infile) == 0 && mydollytab->num_infiles == 0) {
      fprintf(stderr,"inputfile via '-I [FILE|-]' or '-D [DIR]' must be set\n");
      exit(1);
    }
    if(mydollytab->directory_mode && strlen(mydollytab->outfile) == 0) {
      fprintf(stderr, "-O [outpufile] must be set when using -D\n");
      exit(1);
    }
    if(strlen(mydollytab->outfile) == 0) {
      fprintf(stderr,"outfile via '-O FILE' not set, will use '%s' name as target\n", mydollytab->infile);
      strcpy(mydollytab->outfile,mydollytab->infile);
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
      fprintf(df,"clients %u\n",mydollytab->hostnr);
      for(i = 0; i < mydollytab->hostnr; i++) {
        fprintf(df,"%s\n",mydollytab->hostring[i]);
        
      }
      fprintf(df,"endconfig\n");
      fclose(df);
    }
  }

  if(mydollytab->meserver && mydollytab->flag_v) {
    print_params(mydollytab);
  }

  if(!mydollytab->meserver) {
    fprintf(stderr,"I am a dolly client, waiting for a server...\n");
  }

  /* try to open standard terminal output */
  /* if it fails, redirect stdtty to stderr */
  stdtty = fopen("/dev/tty","a");
  if (stdtty == NULL) {
    stdtty = stderr;
  }

  if(mydollytab->flag_d) {
    if (mydollytab->hostnr < 1) {
      fprintf(stderr, "\nAt least one node is needed, use the -H parameter\n");
      exit(1);
    }
    fprintf(stderr, "\nStart the dolly client on all nodes...\n");
    open_insystemdsocks(mydollytab);
  }


  
  alarm(timeout);

  buildring(mydollytab);
  
  if(mydollytab->meserver) {
    fprintf(stdtty, "Server: Sending data...\n");
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
    //close(client_control_socks[i]);
  }
  if(mydollytab->flag_v) {
    fprintf(stderr, "\n");
  }

  fclose(stdtty);
  free_dollytab(mydollytab);
  free(mydollytab);
 
  exit(0);
}
