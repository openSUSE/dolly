#include "dollytab.h"
#include "utils.h"
#include <sys/stat.h>
/* init the dollytab struct */
void init_dollytab(struct dollytab * mdt) {
  mdt->flag_v = 0;
  memset(mdt->myhostname,'\0',sizeof(mdt->myhostname));
  memset(mdt->servername,'\0',sizeof(mdt->servername));
  memset(mdt->infile,'\0',sizeof(mdt->infile));
  memset(mdt->outfile,'\0',sizeof(mdt->infile));
  memset(mdt->directory_list,'\0',sizeof(mdt->directory_list));
  memset(mdt->password, '\0', sizeof(mdt->password));

  memset(mdt->nexthosts,0,sizeof(mdt->nexthosts));
  memset(mdt->add_name,'\0',sizeof(mdt->add_name));
  mdt->meserver = 0;
  mdt->output_split = 0;
  mdt->input_split = 0;
  mdt->add_nr = 0;
  mdt->add_primary = 0;
  mdt->add_mode = 0;
  mdt->nr_childs = 0;
  mdt->hostnr = 0;
  mdt->melast = 0;
  mdt->hyphennormal = 0;
  mdt->resolve = 0;
  mdt->dollybufsize = 0;
  mdt->t_b_size = 4096;
  mdt->hostring = NULL;
  mdt->directory_mode = 0;
  mdt->total_bytes = 0;
  mdt->infiles = NULL;
  mdt->num_infiles = 0;
  mdt->excludes = (char**) safe_malloc(sizeof(char *));
  mdt->excludes[0] = strdup("/proc");
  mdt->num_excludes = 1;
  mdt->password_required = 0;
}

void free_dollytab(struct dollytab *mdt) {
  if (!mdt) return;
  if (mdt->infiles && mdt->num_infiles > 0) {
    for (size_t i = 0; i < mdt->num_infiles; i++) {
      free(mdt->infiles[i]);
    }
    free(mdt->infiles);
    mdt->infiles = NULL;
    mdt->num_infiles = 0;
  }
  if (mdt->excludes && mdt->num_excludes > 0) {
    for (size_t i = 0; i < mdt->num_excludes; i++) {
      free(mdt->excludes[i]);
    }
    free(mdt->excludes);
    mdt->excludes = NULL;
    mdt->num_excludes = 0; 
  }
  if (mdt->hostring && mdt->hostnr > 0) {
    for (size_t i = 0; i < mdt->hostnr; i++) {
      free(mdt->hostring[i]);
    }
    free(mdt->hostring);
    mdt->hostring = NULL;
    mdt->hostnr = 0;
  }
  if (mdt->dollybuf != NULL) {
    free(mdt->dollybuf);
    mdt->dollybuf = NULL;
  }
  if (mdt->hostring != NULL) {
    mdt->hostring = NULL;
  }
}

/* Parses the config-file. The path to the file is given in dollytab */
void parse_dollytab(FILE *df,struct dollytab * mydollytab) {
  char str[256];
  char *sp, *sp2;
  unsigned int i;
  int me = -2;
  int family, name_status;
  int fm = AF_INET;
  char *mname = NULL;
  char host[NI_MAXHOST];
  struct ifaddrs *ifaddr, *ifa;
  /* Read the parameters... */
  /* First we want to know the input filename */
  if(fgets(str, sizeof(str), df) == NULL) {
    fprintf(stderr, "errno = %d\n", errno);
    perror("fgets for infile");
    exit(1);
  }
  sp2 = str;
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
  snprintf(mydollytab->infile, sizeof(mydollytab->infile), "%.*s", (int)(sp - sp2), sp2);

  struct stat st_in;
  if (stat(mydollytab->infile, &st_in) == 0) {
    if (S_ISDIR(st_in.st_mode)) {
      mydollytab->directory_mode = 1;
      mydollytab->infiles = (char**) safe_malloc(sizeof(char*));
      mydollytab->infiles[0] = strdup(mydollytab->infile);
      mydollytab->num_infiles = 1;
    }
  }

  sp++;
  if(strcmp(sp, "split") == 0) {
    mydollytab->input_split = 1;
  }
    
  /* Then we want to know the output filename */
  if (fgets(str, sizeof(str), df) == NULL) {
    perror("fgets for outfile");
    exit(1);
  }
  sp2 = str;
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
  strncpy(mydollytab->outfile, sp2, sp - sp2);
  mydollytab->outfile[sp - sp2] = '\0';

  struct stat st_out;
  if (mydollytab->directory_mode) {
    if (stat(mydollytab->outfile, &st_out) != 0 || !S_ISDIR(st_out.st_mode)) {
      fprintf(stderr, "Error: When infile is a directory, outfile must be an existing directory.\n");
      exit(1);
    }
  } else {
    if (stat(mydollytab->outfile, &st_out) == 0 && S_ISDIR(st_out.st_mode)) {
      fprintf(stderr, "Error: When infile is a file, outfile cannot be a directory.\n");
      exit(1);
    }
  }

  if (mydollytab->outfile[0] != '/') {
    char temp_outfile[sizeof(mydollytab->outfile) + 1];
    snprintf(temp_outfile, sizeof(temp_outfile), "/%s", mydollytab->outfile);
    strcpy(mydollytab->outfile, temp_outfile);
  }
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
    case 'T': size *= 1024LL*1024LL*1024LL*1024LL; break;
    case 'G': size *= 1024LL*1024LL*1024LL; break;
    case 'M': size *= 1024LL*1024LL; break;
    case 'k': size *= 1024LL; break;
    default:
      fprintf(stderr, "Unknown multiplier '%c' for split size.\n", *s);
      break;
    }
    mydollytab->output_split = size;
    str[sp - str - 1] = '\0';
  }
  
  /* Get the optional extra network interfaces */
  /* Form of the line: add <nr_extra_interfaces>:<postfix>{:<postfix>} */
  if((strncmp("add ", str, 4) == 0) || (strncmp("add2 ", str, 5) == 0)) {
    char *s1, *s2;
    int max = 0, j;

    if(strncmp("add ", str, 4) == 0) {
      mydollytab->add_mode = 1;
    }
    if(strncmp("add2 ", str, 5) == 0) {
      mydollytab->add_mode = 2;
    }
    if(mydollytab->add_mode == 0) {
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
      mydollytab->add_primary = 1;
      s1 = s2 + 1;
      s2++;
      while((*s2 != ':' && *s2 != '\n' && *s2 != 0)) s2++;
      if(*s2 == 0) {
        fprintf(stderr, "Error in add line: Preliminary end.\n");
        exit(1);
      }
      *s2 = 0;
      strcpy(mydollytab->add_name[0], s1);
    } else {
      for(j = 0; j < max; j++) {
	s1 = s2 + 1;
	s2++;
	while((*s2 != ':' && *s2 != '\n' && *s2 != 0)) s2++;
	if(*s2 == 0) {
	  fprintf(stderr, "Error in add line: Preliminary end.\n");
	  exit(1);
	}
	*s2 = 0;
	strcpy(mydollytab->add_name[j], s1);
      }
    }
    mydollytab->add_nr = max;
    if(fgets(str, sizeof(str), df) == NULL) {
      perror("fgets after add");
      exit(1);
    }
  }
  
 
  if(fgets(str, sizeof(str), df) == NULL) {
    perror("fgets after add");
    exit(1);
  }


  /*
   * The parameter "hyphennormal" means that the hyphen '-' is treated
   * as any other character. The default is to treat it as a separator
   * between base hostname and the number of the node.
   */
  if(strncmp("hyphennormal", str, 12) == 0) {
    mydollytab->hyphennormal = 1;
    if(fgets(str, sizeof(str), df) == NULL) {
      perror("fgets after hyphennormal");
      exit(1);
    }
  }
  if(strncmp("hypheninterface", str, 15) == 0) {
    mydollytab->hyphennormal = 1;
        if(fgets(str, sizeof(str), df) == NULL) {
          perror("fgets after hypheninterface");
          exit(1);
        }
      }
      
      /* Read in all the participating hosts. */
      mydollytab->hostnr = 0;
      mydollytab->hostring = NULL;

  // The 'str' variable already contains the first line to be processed (first host or endconfig)
  do {
    if (strncmp("endconfig", str, 9) == 0) {
      break;
    }
    // Trim newline
    if (str[strlen(str) - 1] == '\n') {
      str[strlen(str) - 1] = '\0';
    }
    // Ignore empty lines
    if (strlen(str) == 0) {
        // Read next line and continue
        if (fgets(str, sizeof(str), df) == NULL) {
            break; // End of file
        }
        continue;
    }

    if (mydollytab->hostnr >= MAXHOSTS) {
      fprintf(stderr, "I think %u numbers of hosts doesn't make much sense.\n",
              mydollytab->hostnr);
      exit(1);
    }

    mydollytab->hostring = (char **)safe_realloc(mydollytab->hostring, (mydollytab->hostnr + 1) * sizeof(char *));
    mydollytab->hostring[mydollytab->hostnr] = (char *)safe_malloc(strlen(str) + 1);
    strcpy(mydollytab->hostring[mydollytab->hostnr], str);
    mydollytab->hostnr++;
  } while (fgets(str, sizeof(str), df) != NULL); // Read next line for next iteration


  if ((mydollytab->hostnr < 1) || (mydollytab->hostnr > MAXHOSTS)) {
    fprintf(stderr, "I think %u numbers of hosts doesn't make much sense.\n",
            mydollytab->hostnr);
    exit(1);
  }

  /* get a list to all availabel interfaces */
  if (getifaddrs(&ifaddr) == -1) {
    perror("getifaddrs");
    exit(EXIT_FAILURE);
  }

  for (i = 0; i < mydollytab->hostnr; i++) {
    /* Try to find next host in ring */
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == NULL) {
        continue;
      }
      family = ifa->ifa_addr->sa_family;
      if (family == fm) {
        name_status = getnameinfo(ifa->ifa_addr, (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
        if (name_status != 0) {
          printf("getnameinfo() failed: %s\n", gai_strerror(name_status));
          exit(EXIT_FAILURE);
        }
        if (strcmp(mydollytab->hostring[i], host) == 0) {
          me = i;
          strcpy(mydollytab->myhostname, host);
        } else if (!mydollytab->hyphennormal) {
          /* Check if the hostname is correct, but a different interface is used */
          if ((sp = strchr(mydollytab->hostring[i], '-')) != NULL) {
            if (strncmp(mydollytab->hostring[i], host, sp - mydollytab->hostring[i]) == 0) {
              me = i;
              strcpy(mydollytab->myhostname, host);
            }
          }
        }
      }
    }
    /* check if hostname was in the configuration and not an ip */
    if (me == -2) {
      mname = getenv("MYNODENAME");
      if (mname != NULL) {
        if (strcmp(mydollytab->hostring[i], mname) == 0) {
          strcpy(mydollytab->myhostname, mname);
          me = i;
          if (i == mydollytab->hostnr - 1) {
            mydollytab->melast = 1;
          }
        }
      }
    }
    if (me == -2) {
      mname = getenv("HOSTNAME");
      if (mname != NULL) {
        if (strcmp(mydollytab->hostring[i], mname) == 0) {
          strcpy(mydollytab->myhostname, mname);
          me = i;
          if (i == mydollytab->hostnr - 1) {
            mydollytab->melast = 1;
          }
        }
      }
    }
  }
  freeifaddrs(ifaddr);
  if (!mydollytab->meserver && (me == -2)) {
    fprintf(stderr, "Couldn't find myself '%s' in hostlist.\n", mydollytab->myhostname);
    exit(1);
  }

  /* Build up topology */
  mydollytab->nr_childs = 0;

  if (mydollytab->meserver) {
    if (1 <= mydollytab->hostnr) {
      mydollytab->nexthosts[0] = 0;
      mydollytab->nr_childs++;
    }
  } else {
    if ((unsigned int)((me + 1) + 1) <= mydollytab->hostnr) {
      mydollytab->nexthosts[0] = (me + 1);
      mydollytab->nr_childs++;
    }
  }
  /* In a tree, we might have multiple last machines. */
  if (mydollytab->nr_childs == 0) {
    mydollytab->melast = 1;
  }

  /* Did we reach the end? */
  if (strncmp("endconfig", str, 9) != 0) {
    fprintf(stderr, "Missing 'endconfig' in config-file.\n");
    exit(1);
  }
  if(mydollytab->flag_v) {
    fprintf(stderr, "done.\n");
  }
  if(mydollytab->flag_v) {
    if(!mydollytab->meserver) {
      fprintf(stderr, "I'm number %d\n", me);
    }
  }
}

/*
 * Clients read the parameters from the control-socket.
 * As they are already parsed from the config-file by the server,
 * we don't do much error-checking here.
 */
void getparams(int f,struct dollytab * mydollytab) {
  size_t readsize;
  ssize_t writesize;
  int fd, ret;
  FILE *dolly_df = NULL;
  char tmpfile[32] = "/tmp/dollytmpXXXXXX";



  if (read(f, &mydollytab->directory_mode, sizeof(mydollytab->directory_mode)) != sizeof(mydollytab->directory_mode)) {
    fprintf(stderr, "Failed to read directory_mode flag\n");
    exit(1);
  }

  mydollytab->dollybuf = (char *)safe_malloc(mydollytab->t_b_size);

  readsize = 0;
  do {
    ret = read(f, mydollytab->dollybuf + readsize, mydollytab->t_b_size);
    if(ret == -1) {
      perror("read in getparams while");
      exit(1);
    } else if((ssize_t) ret == mydollytab->t_b_size) {  /* This will probably not happen... */
      fprintf(stderr, "Ups, the transmitted config-file seems to long.\n"
	      "Please rewrite dolly.\n");
      exit(1);
    }
    readsize += ret;
  } while(ret == 1448);
  mydollytab->dollybufsize = readsize;

  /* Write everything to a file so we can use parse_dollytab(FILE *)
     afterwards.  */
  fd = mkstemp(tmpfile);
  if(fd == -1) {
    perror("Opening temporary file 'tmp' in getparams");
    exit(1);
  }
  writesize = write(fd, mydollytab->dollybuf, readsize);
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
  if(mydollytab->flag_v) {
    fprintf(stderr, "done.\n");
  }

  parse_dollytab(dolly_df,mydollytab); 
  fclose(dolly_df);
  close(fd);
  unlink(tmpfile);
}
