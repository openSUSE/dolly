#ifndef SOCKS_H
#define SOCKS_H

#include "dollytab.h"

void open_insocks(void);
void open_insystemdsocks(struct dollytab * mydollytab);
void open_outsocks(struct dollytab * mydollytab);
void buildring(struct dollytab * mydollytab);
void getparams(int socket, struct dollytab * mydollytab);
void init_sockets(void);
void close_sockets(void);

#endif