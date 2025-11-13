// socks.c

#include "dolly.h"
#include "dollytab.h"
#include "sha256.h"

#include "files.h"
#include "movebytes.h"

#include "socks.h"
#include "resolve.h"

/* File descriptors for file I/O */
int input = -1, output = -1;

/* Numbers for splitted input/output files */
unsigned int input_nr = 0, output_nr = 0;

const unsigned int systemdport = 9996;

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

int flag_log = 0;
char logfile[256] = "";

/* Pipe descriptor in case data must be uncompressed before write */
int pd[2];
/* Pipe descriptor in case input data must be compressed */
int id[2]; 

/* PIDs of child processes */
int in_child_pid = 0, out_child_pid = 0;

void init_sockets(void) {
  int i;
  datasock = -1;
  ctrlin = -1;
  ctrlsock = -1;
  for (i = 0; i < MAXFANOUT; i++) {
    datain[i] = -1;
    dataout[i] = -1;
    ctrlout[i] = -1;
  }
}

void close_sockets(void) {
  unsigned int i;

  for (i = 0; i < MAXFANOUT; i++) {
    if (datain[i] != -1) {
      close(datain[i]);
      datain[i] = -1;
    }
  }

  if (ctrlin != -1) {
    close(ctrlin);
    ctrlin = -1;
  }
  if (datasock != -1) {
    close(datasock);
    datasock = -1;
  }
  if (ctrlsock != -1) {
    close(ctrlsock);
    ctrlsock = -1;
  }

  for (i = 0; i < MAXFANOUT; i++) {
    if (ctrlout[i] != -1) {
      close(ctrlout[i]);
      ctrlout[i] = -1;
    }
    if (dataout[i] != -1) {
      close(dataout[i]);
      dataout[i] = -1;
    }
  }
}

void open_insocks(struct dollytab * mydollytab) {
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
  int sckbufsize = SCKBUFSIZE;
  if(setsockopt(datasock, SOL_SOCKET, SO_RCVBUF, &sckbufsize, sizeof(sckbufsize)) < 0) {
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

void open_insystemdsocks(struct dollytab * mydollytab) {
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
    if (inet_pton(AF_INET, mydollytab->hostring[i], &serverAddr.sin_addr) <= 0) {
      fprintf(stderr, "inet_pton failed for address %s\n", mydollytab->hostring[i]);
      exit(1);
    }
    /* Connect to the systemd socket of all client nodes to start the dolly service */
    addr_size = sizeof serverAddr;
    Retval = connect(clientSocket, (struct sockaddr *) &serverAddr, addr_size);
    if (Retval < 0) {
      fprintf(stderr, "Connection failed to %s ! dolly service wont be started !\n (Please start the dolly.socket with systemd)\n (To always enable it: systemctl enable dolly.socket)\n", mydollytab->hostring[i]);
    }
    close(clientSocket);
  }
}

void open_outsocks(struct dollytab * mydollytab) {
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
      strncpy(hn, mydollytab->hostring[mydollytab->nexthosts[i]], sizeof(hn) - 1);
    } else if(mydollytab->add_nr > 0) {
      if(mydollytab->add_mode == 1) {
    	strncpy(hn, mydollytab->hostring[mydollytab->nexthosts[0]], sizeof(hn) - 1);
    	if(i > 0) {
    	  strncat(hn, mydollytab->add_name[i - 1], sizeof(hn) - strlen(hn) - 1);
    	}
      } else if(mydollytab->add_mode == 2) {
    	if(i == 0) {
    	  strncpy(hn, mydollytab->hostring[mydollytab->nexthosts[0]], sizeof(hn) - 1);
    	} else {
    	  int j = 0;
    	  while(!isdigit(mydollytab->hostring[mydollytab->nexthosts[0]][j])) {
    	    hn[j] = mydollytab->hostring[mydollytab->nexthosts[0]][j];
    	    j++;
    	  }
    	  hn[j] = 0;
    	  strncat(hn, mydollytab->add_name[i - 1], sizeof(hn) - strlen(hn) - 1);
    	  strncat(hn, &mydollytab->hostring[mydollytab->nexthosts[0]][j], sizeof(hn) - strlen(hn) - 1);
    	}
      } else {
    	fprintf(stderr, "Undefined add_mode %d!\n", mydollytab->add_mode);
    	exit(1);
      }
    } else if (mydollytab->add_primary) {
      assert(i < 1);

      if(mydollytab->add_mode == 1) {
	strncpy(hn, mydollytab->hostring[mydollytab->nexthosts[0]], sizeof(hn) - 1);
	strncat(hn, mydollytab->add_name[0], sizeof(hn) - strlen(hn) - 1);
      } else if(mydollytab->add_mode == 2) {
	int j = 0;
	while(!isdigit(mydollytab->hostring[mydollytab->nexthosts[0]][j])) {
	  hn[j] = mydollytab->hostring[mydollytab->nexthosts[0]][j];
	  j++;
	}
	hn[j] = 0;
	strncat(hn, mydollytab->add_name[0], sizeof(hn) - strlen(hn) - 1);
	strncat(hn, &mydollytab->hostring[mydollytab->nexthosts[0]][j], sizeof(hn) - strlen(hn) - 1);
      } else {
	fprintf(stderr, "Undefined add_mode %d!\n", mydollytab->add_mode);
	exit(1);
      }
    } else {
      assert(i < 1);
      strncpy(hn, mydollytab->hostring[mydollytab->nexthosts[i]], sizeof(hn) - 1);
    }
    hn[sizeof(hn) - 1] = '\0';

    struct addrinfo *result = resolve_host_addrinfo(hn);

    if(mydollytab->flag_v) {
      fprintf(stderr, "Connecting to client %s\n", hn);
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
      int sckbufsize = SCKBUFSIZE;
      if(setsockopt(dataout[i], SOL_SOCKET, SO_SNDBUF, &sckbufsize, sizeof(sckbufsize)) < 0) {
    	(void) fprintf(stderr, "setsockopt: SO_SNDBUF failed! errno = %d\n", errno);
    	exit(556);
      }
      getsockopt(dataout[i], SOL_SOCKET, SO_RCVBUF,
    		 (char *) &send_size, (void *) &sizeofint);
      //fprintf(stderr, "Send buffer %u is %d bytes\n", i, send_size);

      ///* Setup data port */
      addrdata.sin_family = AF_INET;
      addrdata.sin_port = htons(dataport);
      memcpy(&addrdata.sin_addr, &((struct sockaddr_in *)result->ai_addr)->sin_addr, sizeof(struct in_addr));

      if((mydollytab->nr_childs > 1) || (i == 0)) {
	/* Setup control port */
	addrctrl.sin_family = AF_INET;
	addrctrl.sin_port = htons(ctrlport);
	memcpy(&addrctrl.sin_addr, &((struct sockaddr_in *)result->ai_addr)->sin_addr, sizeof(struct in_addr));
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
	  close(dataout[i]);
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
	    fprintf(stderr, "Data socket ready.\n");
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
    	    close(ctrlout[i]);
    	    exit(1);
    	  } else {
    	    ctrlok = 1;
    	    if(mydollytab->flag_v) {
    	      fprintf(stderr, "Data control ready.\n");
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

    freeaddrinfo(result);
  }
}

void buildring(struct dollytab * mydollytab) {
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

    unsigned char client_thinks_password_is_required = mydollytab->password_required;
    unsigned char server_requires_password;

    if (read(ctrlin, &server_requires_password, sizeof(server_requires_password)) != sizeof(server_requires_password)) {
        fprintf(stderr, "Failed to receive password requirement from server.\n");
        exit(1);
    }

    if (client_thinks_password_is_required != server_requires_password) {
        fprintf(stderr, "Error: Password configuration mismatch.\n");
        if (client_thinks_password_is_required) {
            fprintf(stderr, "Client has a password, but server does not require one.\n");
        } else {
            fprintf(stderr, "Server requires a password, but client does not have one (use -P).\n");
        }
        exit(1);
    }

    mydollytab->password_required = server_requires_password;
    if(mydollytab->password_required) {
      //fprintf(stderr, "I am a client sending the token\n");
      unsigned char password_hash[SHA256_DIGEST_LENGTH];
      unsigned char nonce[SHA256_DIGEST_LENGTH];
      unsigned char client_response_hash[SHA256_DIGEST_LENGTH];

      //fprintf(stderr, "Client: Raw password: %s\n", mydollytab->password);
      // Hash the client's password once
      hash_data((unsigned char *)mydollytab->password, strlen(mydollytab->password), password_hash);

      // Receive nonce from server
      if (receive_sha256_key(ctrlin, nonce) != 0) {
	fprintf(stderr, "Failed to receive nonce from server.\n");
	exit(1);
      }
      /*fprintf(stderr, "Client: Received nonce: ");
      for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
	fprintf(stderr, "%02x", nonce[k]);
      }
      fprintf(stderr, "\n");
      */

      // Calculate client's response hash (password_hash + nonce)
      hash_data_with_nonce(password_hash, SHA256_DIGEST_LENGTH, nonce, SHA256_DIGEST_LENGTH, client_response_hash);
      /*fprintf(stderr, "Client: Generated response hash: ");
      for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
	fprintf(stderr, "%02x", client_response_hash[k]);
      }
      fprintf(stderr, "\n");*/

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
      fprintf(stderr, "I accepted data connection.\n");
    }
    /* The input sockets are now connected. */

    /* Give information back to server */
    sprintf(msg, "Client %s configured with parameters. Ready to proceed.\n", mydollytab->myhostname);
    ret = movebytes(ctrlin, WRITE, msg, strlen(msg),mydollytab);
    if((unsigned int) ret != strlen(msg)) {
      fprintf(stderr,
    	      "Couldn't write got-parameters-message back to server "
    	      "(sent %d instead of %zu bytes).\n",
    	      ret, strlen(msg));
    }
  }

    if(mydollytab->meserver && mydollytab->password_required) {
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

      for(i = 0; i < mydollytab->nr_childs; i++) {
        movebytes(ctrlout[i], WRITE, (char *)&mydollytab->password_required, sizeof(mydollytab->password_required), mydollytab);
      }

      if (mydollytab->password_required) {
	unsigned int child_idx = 0;
	unsigned char server_password_hash[SHA256_DIGEST_LENGTH];
	unsigned char nonce[SHA256_DIGEST_LENGTH];

	generate_nonce(nonce);

	//fprintf(stderr, "Server: Raw password: %s\n", mydollytab->password);
	// Hash the server's password once
	hash_data((unsigned char *)mydollytab->password, strlen(mydollytab->password), server_password_hash);

	while (child_idx < mydollytab->nr_childs) {
    	  unsigned char client_response_hash[SHA256_DIGEST_LENGTH];
    	  unsigned char expected_response_hash[SHA256_DIGEST_LENGTH];

	  // Send nonce to client
	  if (send_sha256_key(ctrlout[child_idx], nonce) != 0) {
	    fprintf(stderr, "Failed to send nonce to client.\n");
	    exit(1);
	  }
	  /*fprintf(stderr, "Server: Sent nonce to client %u: ", child_idx);
	  for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
	    fprintf(stderr, "%02x", nonce[k]);
	  }
	  fprintf(stderr, "\n"); */

    	  // Receive client's response hash
    	  if (receive_sha256_key(ctrlout[child_idx], client_response_hash) != 0) {
    	    fprintf(stderr, "Failed to receive client response hash.\n");
    	    exit(1);
    	  }
	  /*fprintf(stderr, "Server: Received client response hash from client %u: ", child_idx);
	  for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
	    fprintf(stderr, "%02x", client_response_hash[k]);
	  }
	  fprintf(stderr, "\n");*/

	  // Calculate expected response hash (server_password_hash + nonce)
	  hash_data_with_nonce(server_password_hash, SHA256_DIGEST_LENGTH, nonce, SHA256_DIGEST_LENGTH, expected_response_hash);
	  /*fprintf(stderr, "Server: Expected response hash for client %u: ", child_idx);
	  for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
	    fprintf(stderr, "%02x", expected_response_hash[k]);
	  }
	  fprintf(stderr, "\n");*/

    	  if (verify_sha256_key(expected_response_hash, client_response_hash)) {
	    fprintf(stderr, "Server: Password verification successful for client %s.\n", mydollytab->hostring[mydollytab->nexthosts[child_idx]]);
    	    write(ctrlout[child_idx], "OK", 3);
    	    child_idx++;
    	  } else {
	    fprintf(stderr, "Server: Password verification failed for client %s.\n", mydollytab->hostring[mydollytab->nexthosts[child_idx]]);
    	    char fail_msg[256 + 13]; // "AUTH_FAILED:" + hostname + null terminator
    	    snprintf(fail_msg, sizeof(fail_msg), "AUTH_FAILED:%s", mydollytab->hostring[mydollytab->nexthosts[child_idx]]);
    	    write(ctrlout[child_idx], fail_msg, strlen(fail_msg) + 1); // +1 for null terminator
    	    fprintf(stderr, "Authentication failed for client: %s. Exiting.\n", fail_msg);
    	    close(ctrlout[child_idx]);
    	    close(dataout[child_idx]);
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

    /*if(mydollytab->flag_v) {
      fprintf(stderr, "Accepted.\n");
    }*/

    if(!mydollytab->meserver) {
      /* Send it further */
      if(!mydollytab->melast) {
        for(i = 0; i < mydollytab->nr_childs; i++) {
          movebytes(ctrlout[i], WRITE, (char *)&mydollytab->password_required, sizeof(mydollytab->password_required), mydollytab);
        }
    	if (mydollytab->password_required) {
	  unsigned int child_idx = 0;
	  unsigned char server_password_hash[SHA256_DIGEST_LENGTH];
	  unsigned char nonce[SHA256_DIGEST_LENGTH];

	  generate_nonce(nonce);

	  //fprintf(stderr, "Intermediate Server: Raw password: %s\n", mydollytab->password);
	  // Hash the server's password once
	  hash_data((unsigned char *)mydollytab->password, strlen(mydollytab->password), server_password_hash);

	  while (child_idx < mydollytab->nr_childs) {
    	    unsigned char client_response_hash[SHA256_DIGEST_LENGTH];
    	    unsigned char expected_response_hash[SHA256_DIGEST_LENGTH];

	    // Send nonce to client
	    if (send_sha256_key(ctrlout[child_idx], nonce) != 0) {
	      fprintf(stderr, "Failed to send nonce to client.\n");
	      exit(1);
	    }
	    /*fprintf(stderr, "Intermediate Server: Sent nonce to client %u: ", child_idx);
	    for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
	      fprintf(stderr, "%02x", nonce[k]);
	    }
	    fprintf(stderr, "\n");*/

    	    // Receive client's response hash
    	    if (receive_sha256_key(ctrlout[child_idx], client_response_hash) != 0) {
    	      fprintf(stderr, "Failed to receive client response hash.\n");
    	      exit(1);
    	    }
	    /*fprintf(stderr, "Intermediate Server: Received client response hash from client %u: ", child_idx);
	    for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
	      fprintf(stderr, "%02x", client_response_hash[k]);
	    }
	    fprintf(stderr, "\n");*/

	    // Calculate expected response hash (server_password_hash + nonce)
	    hash_data_with_nonce(server_password_hash, SHA256_DIGEST_LENGTH, nonce, SHA256_DIGEST_LENGTH, expected_response_hash);
	    /*fprintf(stderr, "Intermediate Server: Expected response hash for client %u: ", child_idx);
	    for (int k = 0; k < SHA256_DIGEST_LENGTH; k++) {
	      fprintf(stderr, "%02x", expected_response_hash[k]);
	    }
	    fprintf(stderr, "\n");*/

    	    if (verify_sha256_key(expected_response_hash, client_response_hash)) {
	      fprintf(stderr, "Intermediate Server: Password verification successful for client %s.\n", mydollytab->hostring[mydollytab->nexthosts[child_idx]]);
    	      write(ctrlout[child_idx], "OK", 3);
    	      child_idx++;
    	    } else {
	      fprintf(stderr, "Intermediate Server: Password verification failed for client %s.\n", mydollytab->hostring[mydollytab->nexthosts[child_idx]]);
    	      char fail_msg[256 + 13]; // "AUTH_FAILED:" + hostname + null terminator
    	      snprintf(fail_msg, sizeof(fail_msg), "AUTH_FAILED:%s", mydollytab->hostring[mydollytab->nexthosts[child_idx]]);
    	      write(ctrlout[child_idx], fail_msg, strlen(fail_msg) + 1); // +1 for null terminator
    	      fprintf(stderr, "Authentication failed for client: %s. Exiting.\n", fail_msg);
    	      close(ctrlout[child_idx]);
    	      close(dataout[child_idx]);
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
      sprintf(msg, "Client %s ready.\n", mydollytab->myhostname);
      ret = movebytes(ctrlin, WRITE, msg, strlen(msg),mydollytab);
      if((unsigned int) ret != strlen(msg)) {
    	fprintf(stderr,
    		"Couldn't write ready-message back to server "
    		"(sent %d instead of %zu bytes).\n",
    		ret, strlen(msg));
      }
    }
}
