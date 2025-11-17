const char version_string[] = "0.70.4 17-NOV-2025";

#include <dirent.h>
#include "dolly.h"
#include "dollytab.h"
#include "socks.h"
#include "utils.h"
#include "transmit.h"
#include "resolve.h"
#include "sha256.h"
#include "ping.h"
#include <pthread.h>

struct ping_thread_args {
  char *hostname;
  char *ip_addr;
  int reachable;
  int resolved;
  int resolve_option;
};

void *ping_thread_func(void *arg) {
  struct ping_thread_args *args = (struct ping_thread_args *)arg;
  args->ip_addr = (char*)safe_malloc(sizeof(char)*256);
  struct sockaddr_in sock_address;

  if(args->resolve_option == 0 &&
     inet_pton(AF_INET, args->hostname, &(sock_address.sin_addr)) == 1 &&
     inet_pton(AF_INET6, args->hostname, &(sock_address.sin_addr)) == 1) {
    strcpy(args->ip_addr, args->hostname);
    args->resolved = 1;
  } else {
    if(resolve_host(args->hostname, args->ip_addr, args->resolve_option)) {
      args->resolved = 0;
    } else {
      args->resolved = 1;
    }
  }

  if(args->resolved) {
    args->reachable = is_host_reachable(args->ip_addr);
  } else {
    args->reachable = 0;
  }

  return NULL;
}

/* Clients need the ports before they can listen, so we use defaults. */
const unsigned int dataport = 9998;
const unsigned int ctrlport = 9997;
const char* host_delim = ",";

FILE *stdtty;           /* file pointer to the standard terminal tty */

char dollytab[256];
static int generated_dolly = 0;
static struct dollytab* mydollytab_for_cleanup = NULL;

static void cleanup_handler(void) {
  if (generated_dolly) {
    unlink(dollytab);
  }
  if (mydollytab_for_cleanup) {
    close_sockets();
  }
}

static void signal_handler(int signum) {
  fprintf(stderr, "\nCaught signal %d. Terminating.\n", signum);
  exit(128 + signum);
}

/* PIDs of child processes */

/* Handles timeouts by terminating the program. */
static void alarm_handler() {
  fprintf(stderr, "Timeout reached (was set to %d seconds).\nTerminating.\n", timeout);
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
    fprintf(stderr, "| %-34s | %-34s |\n", "Parameter", "Value");
    fprintf(stderr, "| %-34s | %-34s |\n", "----------------------------------", "----------------------------------");
    fprintf(stderr, "| %-34s | %-34s |\n", "Hostname", mydollytab->myhostname);
    fprintf(stderr, "| %-34s | %-34s |\n", "Server Role", (mydollytab->meserver ? "Yes" : "No"));
    fprintf(stderr, "| %-34s | %-34s |\n", "Last client", (mydollytab->melast ? "Yes" : "No"));
    fprintf(stderr, "| %-34s | %-34u |\n", "Control Port", ctrlport);
    fprintf(stderr, "| %-34s | %-34u |\n", "Data Port", dataport);
    if (mydollytab->meserver) {
      fprintf(stderr, "| %-34s | %-34s |\n", "Input File", mydollytab->infile);
      fprintf(stderr, "| %-34s | %-34s |\n", "Output File", mydollytab->outfile);
      fprintf(stderr, "| %-34s | %-34s |\n", "Directory List", mydollytab->directory_list);
    }
    if (!mydollytab->meserver) {
      fprintf(stdtty, "| %-34s | %-34u |\n", "I'm number", mydollytab->hostnr);
    }
    fprintf(stderr, "| %-34s | %-34u |\n", "Number of Childs", mydollytab->nr_childs);
    fprintf(stderr, "| %-34s | %-34u |\n", "Clients in Ring (excluding server)", mydollytab->hostnr);
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
  fprintf(stderr, "Dolly clones data to one or multiple nodes/clients in parallel, saving time.\n");
  fprintf(stderr, "Without -s or -S, dolly runs as a client.\n\n");

  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  dolly [-hPVvSsnYR6d] [-f configfile] [-o logfile]\n");
  fprintf(stderr, "       [-a timeout] [-I inputfile]/[-D inputdir, inputdir2] [-O outputfile]\n");
  fprintf(stderr, "       [-H node1,node2,...] [-X excludedir,excludedir2] [-P password]\n\n");

  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -R                Resolve hostnames to IPv4 addresses\n");
  fprintf(stderr, "  -6                Resolve hostnames to IPv6 addresses\n");
  fprintf(stderr, "  -V                Print version and exit\n");
  fprintf(stderr, "  -h                Print this help and exit\n");
  fprintf(stderr, "  -v                Verbose mode\n");
  fprintf(stderr, "  -q                Suppress 'ignored signal' messages\n");
  fprintf(stderr, "  -P                Password for simple auth process\n");
  fprintf(stderr, "  -o <logfile>      Write statistics to <logfile>\n");
  fprintf(stderr, "  -a <timeout>      Terminate if no data transfer after <timeout> seconds\n");
  fprintf(stderr, "  -r <n>            Retry connection to node <n> times\n");
  fprintf(stderr, "  -n                Do not sync before exit (faster, but risk of data loss on power failure)\n");
  fprintf(stderr, "  -Y                instructs Dolly to treat the '-' character in hostnames as any other character.\n\n");

  fprintf(stderr, "Server Mode (alternative to dollytab):\n");
  fprintf(stderr, "  -s                Run as server (check hostname; not required if -H or -I is used)\n");
  fprintf(stderr, "  -S <hostname>     Use <hostname> as server\n");
  fprintf(stderr, "  -H <hosts>        Comma-separated list of target hosts\n");
  fprintf(stderr, "  -K <hostname>     Kill a waiting dolly client (Comma-separated list)\n");
  fprintf(stderr, "  -I <inputfile>    Input file\n");
  fprintf(stderr, "  -D <inputdir>     Comma-separated list of directories to send\n");
  fprintf(stderr, "  -X <excludedir>   Comma-separated list of directories to exclude (e.g., /proc,/sys)\n");
  fprintf(stderr, "  -O <outputfile>   Output file (use '-' for stdout; defaults to input filename)\n");
  fprintf(stderr, "  -f <configfile>   Configuration file (required on server)\n");
  fprintf(stderr, "  -d                Connect to systemd socket on client nodes (port 9996)\n\n");

  fprintf(stderr, "Examples:\n");
  fprintf(stderr, "  # Client mode:\n");
  fprintf(stderr, "  dolly -v P password\n\n");
  fprintf(stderr, "  # Server mode:\n");
  fprintf(stderr, "  dolly -vs -P password -H sle15sp32,sle15sp33,sle15sp34 -I files.tgz -O /tmp/files.tgz\n");
  fprintf(stderr, "    Copy files.tgz to /tmp/files.tgz on specified clients (verbose)\n\n");
  fprintf(stderr, "  dolly -d -H sle15sp32,sle15sp33,sle15sp34 -I /tmp/files.tgz\n");
  fprintf(stderr, "    Use systemd socket to copy /tmp/files.tgz to clients\n");
  fprintf(stderr, "  dolly -P password -K sle15sp32,sle15sp33,sle15sp34\n");
  fprintf(stderr, "    Kill any running dolly with same auth password on clients\n");

  exit(1);
}

int main(int argc, char *argv[]) {
  int c;
  unsigned int i;
  int flag_f = 0, flag_cargs = 0, me = -2;
  FILE *df;
  char *mnname = NULL;
  char *ip_addr;
  int fd;
  struct dollytab* mydollytab = (struct dollytab*)safe_malloc(sizeof(struct dollytab));
  struct sockaddr_in sock_address;
  int kill_mode = 0;
  char *kill_hosts = NULL;

  init_dollytab(mydollytab);
  mydollytab_for_cleanup = mydollytab;

  atexit(cleanup_handler);
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  init_sockets();

  /* Parse arguments */
  while(1) {
    c = getopt(argc, argv, "a:c:f:r:vqo:S:shdnR46:VI:O:Y:H:D:P:X:K:");
    if(c == -1) break;
    switch(c) {
    case 'K':
      kill_mode = 1;
      kill_hosts = strdup(optarg);
      break;
    case 'f':
      /* Where to find the config-file. */
      if(strlen(optarg) > 255) {
	fprintf(stderr, "Name of config-file too long.\n");
	exit(1);
      }
      snprintf(dollytab, sizeof(dollytab), "%s", optarg);
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

      snprintf(mydollytab->directory_list, sizeof(mydollytab->directory_list), "%s", optarg);
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
      snprintf(mydollytab->password, sizeof(mydollytab->password), "%s", optarg);
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
      snprintf(mydollytab->infile, sizeof(mydollytab->infile), "%s", optarg);
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
	snprintf(mydollytab->outfile, sizeof(mydollytab->outfile), "%s", optarg);
      }
      flag_cargs = 1;
      break;

    case 'Y':
      mydollytab->hyphennormal = 1;
      break;

    case 'H':
      /* as -H is used automatically set this machine as the server. */
      mydollytab->meserver = 1;

      int host_count;
      char** expanded_hosts = expand_host_range(optarg, &host_count);
      char **reachable_hosts = (char**) safe_malloc(host_count * sizeof(char *));
      size_t reachable_nr_hosts = 0;

      // For Host Reachability Status table
      char **host_ips = (char**) safe_malloc(host_count * sizeof(char *));
      char **host_statuses = (char**) safe_malloc(host_count * sizeof(char *));
      size_t table_nr_hosts = 0;

      pthread_t *threads = (pthread_t *)safe_malloc(host_count * sizeof(pthread_t));
      struct ping_thread_args *args = (struct ping_thread_args *)safe_malloc(host_count * sizeof(struct ping_thread_args));

      for (int j = 0; j < host_count; ++j) {
        args[j].hostname = expanded_hosts[j];
        args[j].resolve_option = mydollytab->resolve;
        pthread_create(&threads[j], NULL, ping_thread_func, &args[j]);
      }

      for (int j = 0; j < host_count; ++j) {pthread_join(threads[j], NULL);

	if (args[j].resolved) {
	  if (args[j].reachable) {
	    if (!is_ip_in_list(args[j].ip_addr, reachable_hosts, reachable_nr_hosts)) {
	      reachable_hosts[reachable_nr_hosts] = (char *)safe_malloc(strlen(args[j].ip_addr) + 1);
	      strcpy(reachable_hosts[reachable_nr_hosts], args[j].ip_addr);
	      reachable_nr_hosts++;

	      host_ips[table_nr_hosts] = strdup(args[j].ip_addr);
	      host_statuses[table_nr_hosts] = strdup("Reachable");
	      table_nr_hosts++;
	    } else {
	      if (mydollytab->flag_v) {
		fprintf(stderr, "Client '%s' (%s) is a duplicate and already in the list. Skipping.\n", args[j].hostname, args[j].ip_addr);
	      }
	      host_ips[table_nr_hosts] = strdup(args[j].ip_addr);
	      host_statuses[table_nr_hosts] = strdup("Reachable (dup)");
	      table_nr_hosts++;
	    }
	  } else {
	    if (mydollytab->flag_v) {
	      fprintf(stderr, "Client '%s' (%s) is unreachable. Skipping.\n", args[j].hostname, args[j].ip_addr);
	    }
	    host_ips[table_nr_hosts] = strdup(args[j].ip_addr);
	    host_statuses[table_nr_hosts] = strdup("Unreachable");
	    table_nr_hosts++;
	  }
	} else {
	  fprintf(stderr, "Could not resolve the host '%s'\n", args[j].hostname);
	  host_ips[table_nr_hosts] = strdup(args[j].hostname);
	  host_statuses[table_nr_hosts] = strdup("Unresolvable");
	  table_nr_hosts++;
	}        free(args[j].ip_addr);
      }

      free(threads);
      free(args);

      for (int j = 0; j < host_count; ++j) {
	free(expanded_hosts[j]);
      }
      free(expanded_hosts);

      int unreachable_count = 0;
      for (i = 0; i < table_nr_hosts; i++) {
        if (strcmp(host_statuses[i], "Unreachable") == 0 || strcmp(host_statuses[i], "Unresolvable") == 0) {unreachable_count++;
        }
      }

      if (unreachable_count > 0) {
        fprintf(stderr, "\n### Unreachable Clients\n");
        fprintf(stderr, "| Client IP       | Status      |\n");
        fprintf(stderr, "| --------------- | ----------- |\n");
        for (i = 0; i < table_nr_hosts; i++) {
          if (strcmp(host_statuses[i], "Unreachable") == 0 || strcmp(host_statuses[i], "Unresolvable") == 0) {fprintf(stderr, "| %-15s | %-11s |\n", host_ips[i], host_statuses[i]);
          }
        }
        fprintf(stderr, "\n");
      }

      fprintf(stderr, "\n### Reachable Clients\n");
      if (unreachable_count == 0) {
	fprintf(stderr, "All clients are reachable.\n\n");
      }
      fprintf(stderr, "| Client IP       |\n");
      fprintf(stderr, "| --------------- |\n");
      for (i = 0; i < table_nr_hosts; i++) {
        if (strcmp(host_statuses[i], "Reachable") == 0) {
          fprintf(stderr, "| %-15s |\n", host_ips[i]);
        }
      }
      fprintf(stderr, "\n");

      // Free memory for host_ips and host_statuses
      for (i = 0; i < table_nr_hosts; i++) {
	free(host_ips[i]);
	free(host_statuses[i]);
      }
      free(host_ips);
      free(host_statuses);

      mydollytab->hostnr = reachable_nr_hosts;
      mydollytab->hostring = safe_malloc(mydollytab->hostnr * sizeof(char *));
      for (i = 0; i < reachable_nr_hosts; i++) {mydollytab->hostring[i] = reachable_hosts[i];
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
      if(mydollytab->meserver) {
  	if(1 <= mydollytab->hostnr) {
  	  mydollytab->nexthosts[0] = 0;
  	  mydollytab->nr_childs++;
  	}
      } else {
  	if((unsigned int)((me + 1) + 1) <= mydollytab->hostnr) {
  	  mydollytab->nexthosts[0] = (me + 1);
  	  mydollytab->nr_childs++;
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
    /* only use HOSTNAME when servername or ip is not explictly set */
    if(strcmp(mydollytab->servername,"") == 0) {
      mnname = getenv("HOSTNAME");
      if (mnname == NULL) {
        fprintf(stderr, "Error: HOSTNAME environment variable not set. Please set it in the service file or ensure it's available.\n");
        //exit(1);
      }
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

  if (kill_mode) {
    char *a_str_k = kill_hosts;
    char *host_str_k = strtok(a_str_k, host_delim);
    int success = 0;
    int total = 0;

    while(host_str_k != NULL) {
      total++;
      char ip_addr_k[256];
      int sock_k;
      struct sockaddr_in serv_addr_k;

      if (resolve_host(host_str_k, ip_addr_k, mydollytab->resolve)) {
        fprintf(stderr, "Could not resolve the host '%s'\n", host_str_k);
        host_str_k = strtok(NULL, host_delim);
        continue;
      }

      if ((sock_k = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        host_str_k = strtok(NULL, host_delim);
        continue;
      }

      serv_addr_k.sin_family = AF_INET;
      serv_addr_k.sin_port = htons(ctrlport);

      if (inet_pton(AF_INET, ip_addr_k, &serv_addr_k.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address/ Address not supported for %s\n", host_str_k);
        host_str_k = strtok(NULL, host_delim);
        continue;
      }

      if (connect(sock_k, (struct sockaddr *)&serv_addr_k, sizeof(serv_addr_k)) < 0) {
        fprintf(stderr, "Connection Failed to %s. Is dolly running on the client?\n", host_str_k);
      } else {
        if (write(sock_k, &mydollytab->password_required, sizeof(mydollytab->password_required)) != sizeof(mydollytab->password_required)) {
          fprintf(stderr, "Failed to send password requirement to %s.\n", host_str_k);
        } else {
          if (mydollytab->password_required) {
            unsigned char server_password_hash[SHA256_DIGEST_LENGTH];
            unsigned char nonce[SHA256_DIGEST_LENGTH];
            unsigned char client_response_hash[SHA256_DIGEST_LENGTH];
            unsigned char expected_response_hash[SHA256_DIGEST_LENGTH];

            generate_nonce(nonce);
            hash_data((unsigned char *)mydollytab->password, strlen(mydollytab->password), server_password_hash);

            if (send_sha256_key(sock_k, nonce) != 0) {
              fprintf(stderr, "Failed to send nonce to %s.\n", host_str_k);
            } else {
              if (receive_sha256_key(sock_k, client_response_hash) != 0) {
                fprintf(stderr, "Failed to receive client response from %s.\n", host_str_k);
              } else {
                hash_data_with_nonce(server_password_hash, SHA256_DIGEST_LENGTH, nonce, SHA256_DIGEST_LENGTH, expected_response_hash);
                if (verify_sha256_key(expected_response_hash, client_response_hash)) {
                  if (write(sock_k, "OK", 3) < 0) {
                    perror("write OK");
                  }
                  fprintf(stderr, "Successfully authenticated with %s, sending kill signal...\n", host_str_k);
                  success++;
                } else {
                  fprintf(stderr, "Password verification failed for %s.\n", host_str_k);
                  char fail_msg[256 + 13];
                  snprintf(fail_msg, sizeof(fail_msg), "AUTH_FAILED:%s", host_str_k);
                  if (write(sock_k, fail_msg, strlen(fail_msg) + 1) < 0) {
                    perror("write fail_msg");
                  }
                }
              }
            }
          } else {
            fprintf(stderr, "Successfully connected to %s (no password), sending kill signal...\n", host_str_k);
            success++;
          }
        }
      }
      close(sock_k);
      host_str_k = strtok(NULL, host_delim);
    }

    fprintf(stderr, "Done. Killed %d out of %d clients.\n", success, total);
    free(a_str_k);
    exit(0);
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
      fprintf(stderr, "\nAt least one client is needed, use the -H parameter\n");
      exit(1);
    }
    fprintf(stderr, "\nStart the dolly client on all clients...\n");
    open_insystemdsocks(mydollytab);
  }

  alarm(timeout);
  buildring(mydollytab);

  if(mydollytab->meserver) {
    fprintf(stdtty, "Server: Sending data...\n");
  }

  transmit(mydollytab);

  if(mydollytab->flag_v) {
    fprintf(stderr, "\n");
  }

  fclose(stdtty);
  free_dollytab(mydollytab);
  free(mydollytab);
  exit(0);
}
