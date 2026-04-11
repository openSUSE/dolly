#include "ping.h"
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

int is_host_reachable(const char *hostname) {
  pid_t pid = fork();
  if(pid == 0) {
    /* child: redirect stdout/stderr to /dev/null to suppress ping output */
    int devnull = open("/dev/null", O_WRONLY);
    if(devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      close(devnull);
    }
    execlp("ping", "ping", "-c", "1", "-W", "1", hostname, NULL);
    _exit(1);
  } else if(pid < 0) {
    return 0; /* fork failed */
  }
  int status;
  waitpid(pid, &status, 0);
  return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 1 : 0;
}
