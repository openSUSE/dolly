#include "transmit.h"
#include "dolly.h"
#include "movebytes.h"
#include "files.h"
/* The main transmitting function */
void transmit(struct dollytab * mydollytab) {
  char *buf_addr, *buf;
  unsigned long long t, transbytes = 0, lastout = 0;
  unsigned int bytes = mydollytab->t_b_size;
  int ret = 1, maxsetnr = 0;
  unsigned long td = 0, tdlast = 0;
  unsigned int i = 0,j = 0, a = 0;
  FILE *logfd = NULL;
  struct timeval tv1, tv2, tv3;
  fd_set real_set, cur_set;

  buf_addr = (char *)malloc(2 * (mydollytab->t_b_size-1));
  buf = (char *)((unsigned long)(buf_addr + mydollytab->t_b_size-1) & (~(mydollytab->t_b_size-1)));

  t = 0x7fffffff;
  t <<= 32;
  t |= 0xffffffff;
  maxbytes = 0;
#define ADJ_MAXSET(a) if((a)>maxsetnr){maxsetnr=(a);}
  if(!mydollytab->meserver) {
    FD_ZERO(&real_set);
    FD_SET(datain[0], &real_set);
    FD_SET(ctrlin, &real_set);
    ADJ_MAXSET(datain[0]);
    ADJ_MAXSET(ctrlin);
    if(!mydollytab->melast) {
      for(i = 0; i < mydollytab->nr_childs; i++) {
        FD_SET(ctrlout[i], &real_set);  /* Check ctrlout too for backflow */
        ADJ_MAXSET(ctrlout[i]);
      }
    }
    maxsetnr++;
  } else {
    FD_ZERO(&real_set);
    for(i = 0; i < mydollytab->nr_childs; i++) {
      FD_SET(ctrlout[i], &real_set);
      ADJ_MAXSET(ctrlout[i]);
    }
    maxsetnr++;
  }
#undef ADJ_MAXSET
  
  gettimeofday(&tv1, NULL);
  tv2 = tv1;
  
  while((mydollytab->meserver && (ret > 0)) || (!mydollytab->meserver && (t > 0))) {
    /* The server writes data as long has it can to the data stream.
       When there's nothing left, it writes the actual number of bytes
       to the control stream (as long long int).
    */
    if(mydollytab->meserver) {
      /*
       * Server part
       */
      ret = movebytes(input, READ, buf, bytes,mydollytab);
      maxbytes += ret;
      if(ret > 0) {
        if(mydollytab->add_nr == 0) {
          for(i = 0; i < mydollytab->nr_childs; i++) {
            movebytes(dataout[i], WRITE, buf, ret,mydollytab);
          }
        } else {
          static unsigned int cur_out = 0;
          movebytes(dataout[cur_out], WRITE, buf, ret,mydollytab);
          cur_out++;
          if(cur_out > mydollytab->add_nr) {
            cur_out = 0;
          }
        }
      } else {
        /* Here: ret <= 0 */
        int res = -1;
        if(mydollytab->input_split) {
          res = open_infile(0,mydollytab);
        }
        if(!mydollytab->input_split || (res < 0)) {
          if(mydollytab->flag_v) {
            fprintf(stderr, "\nRead %llu bytes from file(s).\n", maxbytes);
          }
          if(mydollytab->add_nr == 0) {
            for(i = 0; i < mydollytab->nr_childs; i++) {
              if(mydollytab->flag_v) {
                (void)fprintf(stderr, "Writing maxbytes = %llu to ctrlout\n",
                  maxbytes);
              }
              movebytes(ctrlout[i], WRITE, (char *)&maxbytes, 8,mydollytab);
              shutdown(dataout[i], 2);
            }
          } else {
            if(mydollytab->flag_v) {
              (void)fprintf(stderr, "Writing maxbytes = %llu to ctrlout\n",
                maxbytes);
            }
            movebytes(ctrlout[0], WRITE, (char *)&maxbytes, 8,mydollytab);
            for(i = 0; i <= mydollytab->add_nr; i++) {
              shutdown(dataout[i], 2);
            }
          }
      } else {
        /* Next input file opened */
        ret = 100000;
        continue;
      } /* end mydollytab->input_split */
    }
      //if(mydollytab->flag_v && (maxbytes - lastout >= 10000000)) {
      if(maxbytes - lastout >= 10000000) {
        tv3=tv2;
        gettimeofday(&tv2, NULL);
        td = (tv2.tv_sec*1000000 + tv2.tv_usec)
          - (tv1.tv_sec*1000000 + tv1.tv_usec);
        tdlast = (tv2.tv_sec*1000000 + tv2.tv_usec)
          - (tv3.tv_sec*1000000 + tv3.tv_usec);
        fprintf(stdtty,
          "\rSent MB: %.0f, MB/s: %.3f, Current MB/s: %.3f      ",
          (float)maxbytes/1000000,
          (float)maxbytes/td,(float)(maxbytes - lastout)/tdlast);
        fflush(stdtty);
        lastout = maxbytes;
      }
    } else {
      /*
       * Client part
       */
      unsigned int i, nr_descr;
      cur_set = real_set;
      ret = select(maxsetnr, &cur_set, NULL, NULL, NULL);
      if(ret == -1) {
        if(errno != EINTR) {
        /* MATHOG: (on above "if" statement)
         * Some signal was received, don't have a handler, ignore it.
         */
        perror("select");
        exit(1);
        }
        ret = 0;
      }
      if(ret < 1) {
	if(verbignoresignals) {
	  /* fr: Shouldn't that be a bit further up? */
	  (void)fprintf(stderr,
			"\nIgnoring unhandled signal (select() returned %d.\n",
			ret);
	}
      } else {
	nr_descr = ret;
	for(i = 0; i < nr_descr; i++) {
	  if(FD_ISSET(ctrlin, &cur_set)) {
	    char mybuf[128];

	    ret = read(ctrlin, mybuf, 128);
	    if(ret == -1) {
	      perror("read from ctrlin in transfer");
	      exit(1);
	    }
	    if(ret == 0) {
	      fprintf(stderr,
		      "Got 0 from ctrlin, but there should be something there!\n"
		      "Probably transmission interrupted, terminating.\n");
	      exit(1);
	    }
	    if(ret != 8) {
	      fprintf(stderr, "Got %d bytes from ctrlin in transfer, "
		      "expected 8.\n", ret);
	    }
	    maxbytes = *(unsigned long long *)&mybuf;
	    if(!mydollytab->melast) {
	      for(j = 0; j < mydollytab->nr_childs; j++) {
          movebytes(ctrlout[i], WRITE, (char *)&maxbytes, 8,mydollytab);
	      }
	    }
	    t = maxbytes - transbytes;
	    if(mydollytab->flag_v) {
	      fprintf(stderr,"\nMax. bytes will be %llu bytes. %llu bytes left.\n", maxbytes, t);
	    }
	    FD_CLR(ctrlin, &real_set);
	    FD_CLR(ctrlin, &cur_set);
	  } else if(FD_ISSET(datain[0], &cur_set)) {
	    /* There is data to be read from the net */
	    bytes = ((ssize_t) t >= mydollytab->t_b_size ? mydollytab->t_b_size : (ssize_t) t);
	    ret = movebytes(datain[0], READ, buf, bytes,mydollytab);
	      if(!mydollytab->output_split) {
          movebytes(output, WRITE, buf, ret,mydollytab);
	      } else {
          /* Check if output file needs to be split. */
          if((transbytes / mydollytab->output_split)
             != ((transbytes + ret) / mydollytab->output_split)) {
            size_t old_part, new_part;
            old_part = ret - (transbytes + ret) % mydollytab->output_split;
            new_part = ret - old_part;
            movebytes(output, WRITE, buf, old_part,mydollytab);
            open_outfile(1,mydollytab);
            movebytes(output, WRITE, buf + old_part, new_part,mydollytab);
          } else {
            movebytes(output, WRITE, buf, ret,mydollytab);
          }
	      } /* end input_split */
	    if(!mydollytab->melast) {
	      for(i = 0; i < mydollytab->nr_childs; i++) {
          movebytes(dataout[i], WRITE, buf, ret,mydollytab);
	      }
	    }
	    transbytes += ret;
	    t -= ret;
	    FD_CLR(datain[0], &cur_set);
	    /* Handle additional network interfaces, if available */
	    for(a = 1; a <= mydollytab->add_nr; a++) {
	      bytes = ((ssize_t)t >= mydollytab->t_b_size ? mydollytab->t_b_size :(ssize_t) t);
	      ret = movebytes(datain[a], READ, buf, bytes,mydollytab);
        movebytes(output, WRITE, buf, ret,mydollytab);
	      if(!mydollytab->melast) {
          movebytes(dataout[a], WRITE, buf, bytes,mydollytab);
	      }
	      transbytes += ret;
	      t -= ret;
	    }
	  } else { /* FD_ISSET(ctrlin[]) */
	    int foundfd = 0;

	    for(j = 0; j < mydollytab->nr_childs; j++) {
	      if(FD_ISSET(ctrlout[j], &cur_set)) {
          /* Backflow of control-information, just pass it on */
          ret = read(ctrlout[j], buf, mydollytab->t_b_size);
          if(ret == -1) {
            perror("read backflow in transmit");
            exit(1);
          }
          movebytes(ctrlin, WRITE, buf, ret,mydollytab);
          foundfd++;
          FD_CLR(ctrlout[j], &cur_set);
	      }
	    }
	    /* if nothing found */
	    if(foundfd == 0) {
	      fprintf(stderr,
		      "select returned without any ready fd, ret = %d.\n",
		      ret);
	      for(i = 0;(int) i < maxsetnr; i++) {
		if(FD_ISSET(i, &cur_set)) {
		  unsigned int j;
		  fprintf(stderr, "  file descriptor %u is set.\n", i);
		  for(j = 0; j < mydollytab->nr_childs; j++) {
		    if(FD_ISSET(ctrlout[j], &cur_set)) {
		      fprintf(stderr, "  (fd %u = ctrlout[%u])\n", i, j);
		    }
		  }
		  for(j = 0; j <= mydollytab->add_nr; j++) {
		    if(FD_ISSET(datain[j], &cur_set)) {
		      fprintf(stderr, "  (fd %u = datain[%u])\n", i, j);
		    }
		  }
		}
	      }
	      exit(1);
	    }
	  }
	}
      }
      if(mydollytab->flag_v && (transbytes - lastout >= 10000000)) {
	tv3=tv2;
	gettimeofday(&tv2, NULL);
	td = (tv2.tv_sec*1000000 + tv2.tv_usec)
	  - (tv1.tv_sec*1000000 + tv1.tv_usec);
	tdlast = (tv2.tv_sec*1000000 + tv2.tv_usec)
	  - (tv3.tv_sec*1000000 + tv3.tv_usec);
	fprintf(stdtty, "\rTransfered MB: %.0f, MB/s: %.3f, Current MB/s: %.3f      ", (float)transbytes/1000000, (float)transbytes/td,(float)(transbytes - lastout)/tdlast);
	fflush(stdtty);
	lastout = transbytes;
      }
    }
    alarm(0);  /* We did something, so turn off the timeout-alarm */
  } /* end while */
  gettimeofday(&tv2, NULL);
  td = (tv2.tv_sec*1000000 + tv2.tv_usec) - (tv1.tv_sec*1000000 + tv1.tv_usec);
  
  if(mydollytab->meserver) {
    if(mydollytab->flag_v) {
      fprintf(stdtty, "\rSent MB: %.0f.       \n", (float)maxbytes/1000000);
    }
 
    if(flag_log){
      logfd = fopen(logfile, "a");
      if(logfd == NULL) {
        perror("open logfile");
        exit(1);
      }
      if(mydollytab->compressed_in) {
        fprintf(logfd, "compressed ");
      }
      fprintf(logfd, "infile = '%s'\n", mydollytab->infile);
      if(mydollytab->compressed_out) {
        fprintf(logfd, "compressed ");
      }
      fprintf(logfd, "outfile = '%s'\n", mydollytab->outfile);
      if(mydollytab->flag_v) {
        if(mydollytab->segsize > 0) {
          fprintf(logfd, "TCP segment size : %u Byte (%u Byte eth)\n", 
            mydollytab->segsize,mydollytab->segsize+54);
        } else {
          fprintf(logfd,
            "Standard TCP segment size : 1460 Bytes (1514 Byte eth)\n");
        }
      } else {
        if(mydollytab->segsize > 0) {
          fprintf(logfd, " %8u", mydollytab->segsize);
        } else {
          fprintf(logfd, " %8d", 1460);
        }
      }
      
      if(mydollytab->flag_v) {
        fprintf(logfd, "Server : '%s'\n", mydollytab->myhostname);
        fprintf(logfd, "Fanout = %u\n", mydollytab->fanout);
        fprintf(logfd, "Nr of childs = %u\n", mydollytab->nr_childs);
        fprintf(logfd, "Nr of hosts = %u\n", mydollytab->hostnr);
      } else {
        fprintf(logfd, " %8u", mydollytab->hostnr);
      }
    }
  } else {
    fprintf(stderr, "Transfered MB: %.0f, MB/s: %.3f \n\n",
	    (float)transbytes/1000000, (float)transbytes/td);
    fprintf(stdtty, "\n");
  }
  
  close(output);
  if(dosync){
    sync();
    if(mydollytab->flag_v) {
      fprintf(stderr, "Synced.\n");
    }
  }
  if(mydollytab->meserver) {
    for(i = 0; i < mydollytab->nr_childs; i++) {
      if(mydollytab->flag_v) {
        fprintf(stderr, "Waiting for child %u.\n",i);
      }
      ret = movebytes(ctrlout[i], READ, buf, 8,mydollytab);
      if(ret != 8) {
        fprintf(stderr,
          "Server got only %d bytes back from client %u instead of 8\n",
          ret, i);
      }
    }
    buf[8] = 0;
    if(*(unsigned long long *)buf != maxbytes) {
      fprintf(stderr, "*** ERROR *** Didn't get correct maxbytes back!\n");
	      /* create unneeded error, so removing as only used for debugging 
	      "Got %lld (0x%016llx) instead of %lld (0x%016llx)\n",
	      *(unsigned long long *)&buf,
	      *(unsigned long long *)&buf,
	      maxbytes, maxbytes);
	      */
    } else {
      fprintf(stderr, "Transfert to all client nodes done.\n");
    }
    if(mydollytab->flag_v) {
      fprintf(stderr, "Time: %lu.%03lu\n", td / 1000000, td % 1000000);
      fprintf(stderr, "MBytes/s: %0.3f\n", (double)maxbytes / td);
      fprintf(stderr, "Aggregate MBytes/s: %0.3f\n",
	    (double)maxbytes * mydollytab->hostnr / td);
    }
    if(maxcbytes != 0) {
      fprintf(stderr, "Bytes written on each node: %llu\n", maxcbytes);
      fprintf(stderr, "MBytes/s written: %0.3f\n",
	      (double)maxcbytes / td);
      fprintf(stderr, "Aggregate MBytes/s written: %0.3f\n",
	      (double)maxcbytes * mydollytab->hostnr / td);
    }
    if(flag_log) {
      if(mydollytab->flag_v) {
        fprintf(logfd, "Time: %lu.%03lu\n", td / 1000000, td % 1000000);
        fprintf(logfd, "MBytes/s: %0.3f\n", (double)maxbytes / td);
        fprintf(logfd, "Aggregate MBytes/s: %0.3f\n",
        (double)maxbytes * mydollytab->hostnr / td);
        if(maxcbytes != 0) {
          fprintf(logfd, "Bytes written on each node: %llu\n", maxcbytes);
          fprintf(logfd, "MBytes/s written: %0.3f\n",
            (double)maxcbytes / td);
          fprintf(logfd, "Aggregate MBytes/s written: %0.3f\n",
            (double)maxcbytes * mydollytab->hostnr / td);
        }
      } else {
        fprintf(logfd, "%4lu.%06lu  ", td / 1000000, td % 1000000);
        fprintf(logfd, "%4.6f  ", (double)maxbytes / td);
        fprintf(logfd, "%4.6f  ",
          (double)maxbytes * mydollytab->hostnr / td);
        if(maxcbytes != 0) {
          fprintf(logfd, "Bytes written on each node: %llu\n", maxcbytes);
          fprintf(logfd, "MBytes/s written: %0.3f\n",
            (double)maxcbytes / td);
          fprintf(logfd, "Aggregate MBytes/s written: %0.3f\n",
            (double)maxcbytes * mydollytab->hostnr / td);
        }
        fprintf(logfd, "\n");
      }
      fclose(logfd);
    }
  } else if(!mydollytab->melast) {
    /* All clients except the last just transfer 8 bytes in the backflow */
    unsigned long long ll;
    for(i = 0; i < mydollytab->nr_childs; i++) {
      movebytes(ctrlout[i], READ, (char *)&ll, 8,mydollytab);
    }
    movebytes(ctrlin, WRITE, (char *)&ll, 8,mydollytab);
  } else if(mydollytab->melast) {
    movebytes(ctrlin, WRITE, (char *)&maxbytes, 8,mydollytab);
  }
  if(mydollytab->flag_v) {
    fprintf(stderr, "Transmitted.\n");
  }
  free(buf_addr);
}
