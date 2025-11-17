#ifndef MOVEBYTES_H
#define MOVEBYTES_H
#include "dollytab.h"

#define READ 0
#define WRITE 1

int movebytes(int fd, int dir, char *addr, unsigned int n,struct dollytab * mydollytab);

#endif
