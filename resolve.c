#include "resolve.h"
#include<ifaddrs.h>
/* resolves the given hostname to ip address 
 * following conditions are met
 * dollytab.resolve = 1 -> use the first resolved ip address
 * dollytab.resolve = 4 -> force ipv4
 * dollytab.resolve = 6 -> force ipv6
 */
int resolve_host(char *hostname , char *ip, int mode) {
  if (!hostname || !ip) {
    fprintf(stderr, "resolve_host: hostname or ip buffer is NULL.\n");
    return 1;
  }
  if (mode != 0 && mode != 4 && mode != 6) {
    fprintf(stderr, "resolve_host: invalid mode (%d). Expected 0, 4, or 6.\n", mode);
    return 1;
  }
  struct addrinfo hints, *servinfo, *p;
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = (mode == 4) ? AF_INET : (mode == 6) ? AF_INET6 : AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if ( (rv = getaddrinfo( hostname , NULL , &hints , &servinfo)) != 0) 
    {
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
      return 1;
    }

  // use the first resolved result
  for(p = servinfo; p != NULL; p = p->ai_next)
    {
      struct sockaddr_in *h = (struct sockaddr_in *) p->ai_addr;
      strncpy(ip, inet_ntoa(h->sin_addr), 255);
      ip[255] = '\0';
      break;
    }
  freeaddrinfo(servinfo); // all done with this structure
  return 0;
}

int get_default_ip(char *hostname , int mode)  {
  /*
   * Find local ip used as source ip in ip packets.
   * Read the /proc/net/route file
   */
  FILE *fhandle;
  char line[100] , *interface , *c;
  interface = NULL;
  fhandle = fopen("/proc/net/route" , "r");
  if(fhandle == NULL) {
    perror("fopen /proc/net/route");
    return -1;
  }
  while(fgets(line , 100 , fhandle)) {
    interface = strtok(line , " \t");
    c = strtok(NULL , " \t");
    if(interface!=NULL && c!=NULL) {
      if(strcmp(c , "00000000") == 0) {
        break;
      }
    }
  }
  fclose(fhandle);
  /* which family do we require , AF_INET or AF_INET6 */
  int fm = AF_UNSPEC;
  if(mode == 4) {
    fm = AF_INET;
  } else if (mode == 6) {
    fm = AF_INET6;
  }
  struct ifaddrs *ifaddr, *ifa;
  int family , s;
  char host[NI_MAXHOST];
  if (getifaddrs(&ifaddr) == -1) {
    perror("getifaddrs");
    exit(EXIT_FAILURE);
  }
  /* Walk through linked list, maintaining head pointer so we can free list later */
  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) {
      continue;
    }
    family = ifa->ifa_addr->sa_family;
    if(interface != NULL && strcmp(ifa->ifa_name, interface) == 0) {
      if (family == fm) {
	s = getnameinfo( ifa->ifa_addr, (family == AF_INET) ? 
			 sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6) , 
			 host , NI_MAXHOST , NULL , 0 , NI_NUMERICHOST);
	if (s != 0) {
	  printf("getnameinfo() failed: %s\n", gai_strerror(s));
	  exit(EXIT_FAILURE);
	}
	strncpy(hostname, host, NI_MAXHOST - 1);
	hostname[NI_MAXHOST - 1] = '\0';
      }
    }
  }
  freeifaddrs(ifaddr);
  return 0;
}

struct addrinfo *resolve_host_addrinfo(const char *hostname) {
  struct addrinfo *result;
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET; // Only IPv4
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0; // Any protocol

  int rv;
  if ((rv = getaddrinfo(hostname, NULL, &hints, &result)) != 0) {
    fprintf(stderr, "getaddrinfo for host '%s' error: %s\n",
            hostname, gai_strerror(rv));
    exit(1);
  }
  return result;
}
