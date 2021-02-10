#ifndef RESOLVE_H
#define RESOLVE_H
#include<stdio.h> //printf
#include<string.h> //memset
#include<stdlib.h> //for exit(0);
#include<sys/socket.h>
#include<errno.h> //For errno - the error number
#include<netdb.h>	//hostent
#include<arpa/inet.h>
/* resolves the given hostname to ip address 
 * following conditions are met
 * dollytab.resolve = 1 -> use the first resolved ip address
 * dollytab.resolve = 4 -> force ipv4
 * dollytab.resolve = 6 -> force ipv6
 */
int resolve_host(char *hostname , char *ip, int mode);
/* same but replaces the hostname */
int resolve_host_replace(char *hostname , int mode);
/* get the ip address for the interface used on the default route */
int get_default_ip(char *hostname , int mode);
#endif
