#ifndef RESOLVE_H
#define RESOLVE_H
/* resolves the given hostname to ip address 
 * following conditions are met
 * dollytab.resolve = 1 -> use the first resolved ip address
 * dollytab.resolve = 4 -> force ipv4
 * dollytab.resolve = 6 -> force ipv6
 */
#include<stdio.h> //printf
#include<string.h> //memset
#include<stdlib.h> //for exit(0);
#include<sys/socket.h>
#include<errno.h> //For errno - the error number
#include<netdb.h>	//hostent
#include<arpa/inet.h>
int resolve_host(char *hostname , char *ip, int mode);
#endif
