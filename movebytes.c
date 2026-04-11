#include "movebytes.h"
#include "dolly.h"
#include "socks.h"

int movebytes(int fd, int dir, char *addr, unsigned int n, struct dollytab * mydollytab) {
  int ret, bytes;
  (void)mydollytab;

  bytes = 0;

  while(0 != n) {
    if(dir == WRITE) {
      ret = write(fd, addr, n);
    } else if(dir == READ) {
      ret = read(fd, addr, n);
    } else {
      fprintf(stderr, "Bad direction in movebytes!\n");
      ret = 0;
    }
    if(ret == -1) {
#ifdef DOLLY_NONBLOCK
      if(errno == EAGAIN) {
        continue;
      }
#endif /* DOLLY_NONBLOCK */
      perror("movebytes read/write");
      fprintf(stderr, "\terrno = %d\n", errno);
      exit(1);
    } else if(ret == 0) {
      return bytes;
    } else {
      addr += ret;
      n -= ret;
      bytes += ret;
    }
  }
  return bytes;
}

