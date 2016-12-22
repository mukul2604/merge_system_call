/**
 * Author : Mukul Sharma (muksharma@cs.stonybrook.edu)
 * Copyright(C) 2016, Stony Brook University
 */

#include <asm/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <ctype.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>
#include "sys_xmergesort.h"
#ifndef __NR_xmergesort
#error xmergesort system call not defined
#endif

#define help_str                                                                    \
  "Possible invalid use. Help:\n"                                                   \
  "./xmergesort [-uaitdh] outfile.txt file1.txt file2.txt\n"                        \
  " -u and -a both are exclusive\n"                                                 \
  " -u: output sorted records; if duplicates found, output only one copy\n"         \
  " -a: output all records, even if there are duplicates\n"                         \
  " -i: compare records case-insensitive (case sensitive by default)\n"             \
  " -t: if any input file is found NOT to be sorted, stop and return an\n"          \
  "          error; otherwise continue to output records ONLY if any\n"             \
  "          are found that are in ascending order to what you've found so far\n"   \
  " -d: return the number of sorted records\n"                                      \
  " -h: help\n"
 
void usage(void) {
	printf(help_str);
	return;
}
/**
 * To check EXTRA CREDIT code,
 * Uncomment this below macro
 * #define EXTRA_CREDIT
 */
int main(int argc, char *argv[])
{
	int rc;
	int opt;
  u_int rec_counts = 0, total_recs = 0;
	margs_t margs;
#ifdef EXTRA_CREDIT
  const char *temp_outp;
#endif
	op_type option = 0;	
	while ((opt = getopt(argc, argv, "uaitdh")) != -1) {
		switch (opt) {
		case 'u':
			option |= FLAG_UNIQUE_REC;
			break;
		case 'a':
			option |= FLAG_ALL_REC;
			break;
		case 'i':
			option |= FLAG_IGNORE_CASE;
			break;
		case 't':
			option |= FLAG_CHECK_SORTED;
			break;
		case 'd':
			option |= FLAG_RET_CNT;
			break;
		case 'h':
			option |= FLAG_HELP;
			break;
		default:
			usage();
			return 0;
			break;
		}
	}

	if (((option & FLAG_ALL_REC) && (option & FLAG_UNIQUE_REC)) || !option ||
      (option & FLAG_HELP)) {
		usage();
		return -1;
	}
  margs.flags = option;
	margs.outfile = argv[optind++];
#ifdef EXTRA_CREDIT
  temp_outp = margs.outfile;
#endif
	margs.infile1 = argv[optind++];
	margs.infile2 = argv[optind++];
  margs.records = &rec_counts; 	
  rc = syscall(__NR_xmergesort, &margs);
	if (rc < 0) {
    perror("Result");
    exit(rc); 
	} else {
    total_recs += rec_counts;
    perror("Result");
    if (option & FLAG_RET_CNT) {
      printf("Total records written: %d\n", total_recs);
    }
  }
#ifdef EXTRA_CREDIT 
  while (optind < argc) {
      margs.infile1 = margs.outfile;
      margs.infile2 = argv[optind++];
      printf("%s\t%s\n", margs.infile1, margs.infile2);
      margs.outfile = "temp";
      margs.records = &rec_counts;
      rc = syscall(__NR_xmergesort, &margs);
      if (rc < 0) {
          perror("Result");
          remove(temp_outp);
          exit(rc);
      } else {
           total_recs += rec_counts;
      } 
  }
  perror("Result");
  remove(temp_outp);
  rc = rename("temp", temp_outp);
  if (option & FLAG_RET_CNT) {
    printf("Total records written: %d\n", total_recs);
  }
#endif
  exit(rc); 
}
