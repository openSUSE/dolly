#include "movebytes.h"
#include "dolly.h"

int movebytes(int fd, int dir, char *addr, unsigned int n,struct dollytab * mydollytab) {
  int ret, bytes;
  static int child_done = 0;
  
  bytes = 0;

  if(child_done && (dir == READ)) {
    child_done = 0;
    return 0;
  }
  while(0 != n) {
    if(dir == WRITE) {
      ret = write(fd, addr, n);
    } else if(dir == READ) {
      fflush(stderr);
      ret = read(fd, addr, n);
      if(((unsigned int)ret < n) && mydollytab->compressed_out && mydollytab->meserver) {
        int wret, status;
        sleep(1);
        wret = waitpid(in_child_pid, &status, WNOHANG);
        if(wret == -1) {
          perror("waitpid");
        } else if(wret == 0) {
          fprintf(stderr, "waitpid returned 0\n");
        } else {
          if(WIFEXITED(status)) {
            if(WEXITSTATUS(status) == 0) {
              child_done++;
              return ret;
            } else {
              fprintf(stderr, "Child terminated with return value %d\n",
                WEXITSTATUS(status));
            }
          }
        }
        (void) fprintf(stderr,"read returned %d\n", ret);
      }
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

