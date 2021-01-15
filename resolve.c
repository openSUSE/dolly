/* resolves the given hostname to ip address 
 * following conditions are met
 * dollytab.resolve = 1 -> use the first resolved ip address
 * dollytab.resolve = 4 -> force ipv4
 * dollytab.resolve = 6 -> force ipv6
 */
int resolve_host(char *hostname , char *ip, int mode) {
	int sockfd;  
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
		strcpy(ip , inet_ntoa( h->sin_addr ) );
	}
	freeaddrinfo(servinfo); // all done with this structure
	return 0;
}
