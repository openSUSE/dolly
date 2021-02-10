#include "files.h"
#include "dolly.h"
/*
 * If "try_hard" is 1, call must be succesful.
 * If try_hard is 1 and an input file can't be opened, the program terminates.
 * If try_hard is not 1 and an input file can't be opened, -1 is returend.
 */
int open_infile(int try_hard,struct dollytab * mydollytab) {
  char name[256+16];

  /* Close old input file if there is one */
  if(input != -1) {
    if(close(input) == -1) {
      perror("close() in open_infile()");
      exit(1);
    }
  }
  if(mydollytab->input_split != 0) {
    sprintf(name, "%s_%u", mydollytab->infile, input_nr);
  } else {
    strcpy(name, mydollytab->infile);
  }
  
  /* Files for input/output */
  if(!mydollytab->compressed_out) {
    /* Input is from file */
    input = open(name, O_RDONLY);
    if(input == -1) {
      if(try_hard == 1) {
        char str[strlen(name)];
        sprintf(str, "open inputfile '%s'", name);
        perror(str);
        exit(1);
      } else {
        return -1;
      }
    }
  } else {
    /* Input should be compressed first. */
    if(access(name, R_OK) == -1) {
      if(try_hard == 1) {
        char str[strlen(name)];
        sprintf(str, "open inputfile '%s'", name);
        perror(str);
        exit(1);
      } else {
        return -1;
      }
    }
    if(pipe(id) == -1) {
      perror("input pipe()");
      exit(1);
    }
    input = id[0];
    if((in_child_pid = fork()) == 0) {
      int fd;
      /* Here's the child. */
      close(id[0]);
      close(1);
      dup(id[1]);
      close(id[1]);
      if((fd = open(name, O_RDONLY)) == -1) {
        exit(1);
      }
      close(0);
      dup(fd);
      close(fd);
      if(execl("/usr/bin/gzip", "gzip", "-cf", NULL) == -1) {
        perror("execl for gzip in child");
        exit(1);
      }
    } else {
      /* Father */
      close(id[1]);
    }
  } /* endif compressed_out */
  input_nr++;
  return 0;
}

int open_outfile(int try_hard,struct dollytab * mydollytab) {
  char name[256+16];
  int is_device = 0;
  int is_pipe = 0;
  /* Close old output file, if there is one. */
  if(output != -1) {
    if(close(output) == -1) {
      perror("close() in open_outfile()");
      exit(1);
    }
  }
  if(mydollytab->output_split != 0) {
    sprintf(name, "%s_%u", mydollytab->outfile, output_nr);
  } else {
    strcpy(name, mydollytab->outfile);
  }
  /* check if file is under /dev, if not open even if the file does not exist. */
  if(strcmp("/dev/",name) > 0 ) {
    is_device = 1;
  }
  if(strcmp("-",name) == 0) {
    is_pipe = 1;
  }
  /* Setup the output files/pipes. */
  if(!mydollytab->compressed_in) {
    /* Output is to a file */
    if(!mydollytab->compressed_out && (mydollytab->output_split == 0) && is_device && !is_pipe) {
      /* E.g. partition-to-partition cloning */
      output = open(name, O_WRONLY);
    } else if(!mydollytab->compressed_out && !is_device && !is_pipe) {
      /* E.g. file to file cloning */
      output = open(name, O_WRONLY | O_CREAT, 0644);
    } else if(is_pipe) {
      output = 1;
    } else {
      /* E.g. partition-to-compressed-archive cloning */
      output = open(name, O_WRONLY | O_CREAT | O_EXCL, 0644);
    }
    if(output == -1) {
      char str[strlen(name)];
      sprintf(str, "open outputfile '%s'", name);
      perror(str);
      exit(1);
    }
  } else { /* Compressed_In */
    if(access(name, W_OK) == -1) {
      if(try_hard == 1) {
        char str[strlen(name)];
        sprintf(str, "open outputfile '%s'", name);
        perror(str);
        exit(1);
      } else {
        return -1;
      }
    }
    /* Pipe to gunzip */
    if(pipe(pd) == -1) {
      perror("output pipe");
      exit(1);
    }
    output = pd[1];
    if((out_child_pid = fork()) == 0) {
      int fd;
      /* Here's the child! */
      close(pd[1]);
      close(0);      /* Close stdin */
      dup(pd[0]);    /* Duplicate pipe on stdin */
      close(pd[0]);  /* Close the unused end of the pipe */
      if((fd = open(name, O_WRONLY)) == -1) {
        if(errno == ENOENT) {
          fprintf(stderr, "Outputfile in child does not exist.\n");
        }
        perror("open outfile in child");
        exit(1);
      }
      close(1);
      dup(fd);
      close(fd);
      /* Now stdout is redirected to our file */
      if(execl("/usr/bin/gunzip", "gunzip", "-c", NULL) == -1) {
        perror("execl for gunzip in child");
        exit(1);
      }
    } else {
      /* Father */
      close(pd[0]);
    }
  }
  output_nr++;
  return 0;
}
