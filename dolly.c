const char version_string[] = "0.70.0, 05-NOV-2025";

#include <dirent.h>
#include "dolly.h"
#include "dollytab.h"
#include "socks.h"
#include "utils.h"
#include "transmit.h"


#include "ping.h"

/* Clients need the ports before they can listen, so we use defaults. */
const unsigned int dataport = 9998;
const unsigned int ctrlport = 9997;
const char* host_delim = ",";

FILE *stdtty;           /* file pointer to the standard terminal tty */

/* PIDs of child processes */

/* Handles timeouts by terminating the program. */
static void alarm_handler() {
  fprintf(stderr, "Timeout reached (was set to %d seconds).\nTerminating.\n",
          timeout);
  exit(1);
}

/* This functions prints all the parameters before starting.
   It's mostly used for debugging. */
void print_params(struct dollytab* mydollytab) {
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
    //fprintf(stderr, "| %-36s | %-40u |\n", "Fanout", mydollytab->fanout);
    fprintf(stderr, "| %-36s | %-40u |\n", "Number of Childs", mydollytab->nr_childs);
    fprintf(stderr, "| %-36s | %-40u |\n", "Clients in Ring (excluding server)", mydollytab->hostnr);
    fprintf(stderr, "\n");
  }

  if(mydollytab->nr_childs == 0) {
    fprintf(stderr, "I don't have a client.\n");
  } else {
    for(i = 0; i < mydollytab->nr_childs; i++) {
      fprintf(stderr, "Next client: %s (%d)\n",
	      mydollytab->hostring[mydollytab->nexthosts[i]], mydollytab->nexthosts[i]);
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
  fprintf(stderr, "  dolly [-hPVvSsnYR6d] [-c <size>] [-f configfile]\n");
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
  char *mnname = NULL, *tmp_str;
  char *host_str, *ip_addr;
  size_t nr_hosts = 0;
  int fd;
  struct dollytab* mydollytab = (struct dollytab*)safe_malloc(sizeof(struct dollytab));
  struct sockaddr_in sock_address;
  init_dollytab(mydollytab);


  /* Parse arguments */
  while(1) {
    c = getopt(argc, argv, "a:c:f:r:vqo:S:shdnR46:VI:O:Y:H:D:P:X:");
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
      char *tmp_str_d = a_str;
      unsigned int num_dirs = 0;
      while(*tmp_str_d) {
        if(*host_delim == *tmp_str_d) {
          num_dirs++;
        }
        tmp_str_d++;
      }
      num_dirs++;
      mydollytab->num_infiles = num_dirs;
      mydollytab->infiles = (char**) safe_malloc(num_dirs * sizeof(char *));

      char *dir_str = strtok(a_str, host_delim);
      num_dirs = 0;
      while(dir_str != NULL) {
        mydollytab->infiles[num_dirs] = (char *)safe_malloc(strlen(dir_str) + 1);
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
      char *a_str = strdup(optarg);
      tmp_str = a_str;
      while(*tmp_str) {
        if(*host_delim == *tmp_str) {
          nr_hosts++;
        }
        tmp_str++;
      }
      nr_hosts++;
      
      char **reachable_hosts = (char**) safe_malloc(nr_hosts * sizeof(char *));
      size_t reachable_nr_hosts = 0;

      // For Host Reachability Status table
      char **host_ips = (char**) safe_malloc(nr_hosts * sizeof(char *));
      char **host_statuses = (char**) safe_malloc(nr_hosts * sizeof(char *));
      size_t table_nr_hosts = 0;

      /* now find the first host */
      host_str = strtok(a_str,host_delim);
      
      while(host_str != NULL) {
        ip_addr = (char*)safe_malloc(sizeof(char)*256);
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
	  reachable_hosts[reachable_nr_hosts] = (char *)safe_malloc(strlen(ip_addr)+1);
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
      mydollytab->hostring = safe_malloc(mydollytab->hostnr * sizeof(char *));
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
      free(a_str);
      break;

    case 'X': {
      char *a_str_x = strdup(optarg);
      char *tmp_str_x = a_str_x;
      unsigned int num_excludes = mydollytab->num_excludes;
      while(*tmp_str_x) {
        if(*host_delim == *tmp_str_x) {
          num_excludes++;
        }
        tmp_str_x++;
      }
      num_excludes++;
      mydollytab->excludes = (char**) realloc(mydollytab->excludes, num_excludes * sizeof(char *));

      char *exclude_str = strtok(a_str_x, host_delim);
      while(exclude_str != NULL) {
        mydollytab->excludes[mydollytab->num_excludes] = (char *)safe_malloc(strlen(exclude_str) + 1);
        strcpy(mydollytab->excludes[mydollytab->num_excludes], exclude_str);
        mydollytab->num_excludes++;
        exclude_str = strtok(NULL, host_delim);
      }
      free(a_str_x);
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
	ip_addr = (char*)safe_malloc(sizeof(char)*256);
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
	ip_addr = (char*)safe_malloc(sizeof(char)*256);
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
