#include "resolve.h"
#include<ifaddrs.h>
/* resolves the given hostname to ip address 
 * following conditions are met
 * dollytab.resolve = 1 -> use the first resolved ip address
 * dollytab.resolve = 4 -> force ipv4
 * dollytab.resolve = 6 -> force ipv6
 */
int resolve_host(char *hostname , char *ip, int mode) {
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_in *h;
	int rv;

	memset(&hints, 0, sizeof hints);
  if(mode == 4) {
    hints.ai_family = AF_INET;
  } else if (mode == 6) {
    hints.ai_family = AF_INET6;
  } else {
    hints.ai_family = AF_UNSPEC;
  }
	hints.ai_socktype = SOCK_STREAM;

	if ( (rv = getaddrinfo( hostname , NULL , &hints , &servinfo)) != 0) 
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) 
	{
		h = (struct sockaddr_in *) p->ai_addr;
		strcpy(ip , inet_ntoa( h->sin_addr ));
	}
	freeaddrinfo(servinfo); // all done with this structure
	return 0;
}

int resolve_host_replace(char *hostname , int mode) {
  char tmp_str[256];
  int retval = 0;
  retval = resolve_host(hostname,tmp_str,mode);
  if(retval == 0) {
    strncpy(hostname,tmp_str,256);
  }
  return retval;
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
		if(strcmp( ifa->ifa_name , interface) == 0) {
			if (family == fm) {
				s = getnameinfo( ifa->ifa_addr, (family == AF_INET) ? 
            sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6) , 
            host , NI_MAXHOST , NULL , 0 , NI_NUMERICHOST);
				if (s != 0) {
					printf("getnameinfo() failed: %s\n", gai_strerror(s));
					exit(EXIT_FAILURE);
				}
				strncpy(hostname,host,strlen(host));
			}
		}
	}
	freeifaddrs(ifaddr);
	return 0;
}

  
