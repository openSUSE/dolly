#include "files.h"
#include "socks.h"
#include "dolly.h"
/*
 * If "try_hard" is 1, call must be succesful.
 * If try_hard is 1 and an input file can't be opened, the program terminates.
 * If try_hard is not 1 and an input file can't be opened, -1 is returend.
 */
int open_infile(int try_hard,struct dollytab * mydollytab) {
  char name[256+16];

  if(mydollytab->directory_mode) {
    if(pipe(id) == -1) {
      perror("input pipe()");
      exit(1);
    }
    input = id[0];
    if((in_child_pid = fork()) == 0) {
      /* Here's the child. */
      close(id[0]);
      close(1);
      (void) !dup(id[1]);
      close(id[1]);
      // New logic to handle multiple directories and excludes
      int num_args = 4 + mydollytab->num_infiles + (mydollytab->num_excludes * 2);
      char **tar_args = malloc(num_args * sizeof(char *));
      int arg_idx = 0;
      tar_args[arg_idx++] = "tar";
      tar_args[arg_idx++] = "-Pcf";
      tar_args[arg_idx++] = "-";
      for (unsigned int i = 0; i < mydollytab->num_excludes; i++) {
	tar_args[arg_idx++] = "--exclude";
	tar_args[arg_idx++] = mydollytab->excludes[i];
      }
      for (unsigned int i = 0; i < mydollytab->num_infiles; i++) {
	tar_args[arg_idx++] = mydollytab->infiles[i];
      }
      tar_args[arg_idx] = NULL;
      if(execvp("tar", tar_args) == -1) {
	perror("execvp for tar in child");
	exit(1);
      }
    } else {
      /* Father */
      close(id[1]);
    }
    return 0;
  }

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
    strncpy(name, mydollytab->infile, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
  }
  
  /* Files for input/output */
  /* Input is from file */
  input = open(name, O_RDONLY);
  if(input == -1) {
    if(try_hard == 1) {
      char str[strlen(name)+18];
      sprintf(str, "open inputfile '%s'", name);
      perror(str);
      exit(1);
    } else {
      return -1;
    }
  }
  if (mydollytab->directory_mode == 0 && mydollytab->input_split == 0) {
    struct stat st;
    if (fstat(input, &st) == 0) {
      if (S_ISREG(st.st_mode)) {
	mydollytab->total_bytes = st.st_size;
      }
    }
  }
  input_nr++;
  return 0;
}

int open_outfile(struct dollytab * mydollytab) {
  char name[256+16];
  int is_device = 0;
  int is_pipe = 0;
  if(mydollytab->directory_mode) {
    if(pipe(pd) == -1) {
      perror("output pipe");
      exit(1);
    }
    output = pd[1];
    if((out_child_pid = fork()) == 0) {
      /* Here's the child! */
      close(pd[1]);
      close(0);      /* Close stdin */
      (void) !dup(pd[0]);    /* Duplicate pipe on stdin */
      close(pd[0]);  /* Close the unused end of the pipe */

      if (chdir(mydollytab->outfile) != 0) {
        perror("chdir to output directory"); exit(1);
      }
      if(execlp("tar", "tar", "-xf", "-", NULL) == -1) {
        perror("execlp for tar in child");
        exit(1);
      }
    } else {
      /* Father */
      close(pd[0]);
    }
    return 0;
  }

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
    strncpy(name, mydollytab->outfile, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
  }
  /* check if file is under /dev, if not open even if the file does not exist. */
  if(strcmp("/dev/",name) > 0 ) {
    is_device = 1;
  }
  if(strcmp("-",name) == 0) {
    is_pipe = 1;
  }
  /* Setup the output files/pipes. */
  /* Output is to a file */
  if((mydollytab->output_split == 0) && is_device && !is_pipe) {
    /* E.g. partition-to-partition cloning */
    output = open(name, O_WRONLY);
  } else if(!is_device && !is_pipe) {
    /* E.g. file to file cloning */
    output = open(name, O_WRONLY | O_CREAT, 0644);
  } else if(is_pipe) {
    output = 1;
  } else {
    /* E.g. partition-to-compressed-archive cloning */
    output = open(name, O_WRONLY | O_CREAT | O_EXCL, 0644);
  }
  if(output == -1) {
    char str[strlen(name)+19];
    snprintf(str, sizeof(str), "open outputfile '%s'", name);
    perror(str);
    exit(1);
  }
  output_nr++;
  return 0;
}
