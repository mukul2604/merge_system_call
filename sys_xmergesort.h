/**
 * Author : Mukul Sharma (muksharma@cs.stonybrook.edu)
 * Copyright(C) 2016, Stony Brook University
 */

#ifndef _sys_xmergesort_
#define _sys_xmergesort_

typedef enum op_flags {
	FLAG_ALL_REC	 	  = 1 << 0,
	FLAG_UNIQUE_REC		= 1 << 1,
	FLAG_IGNORE_CASE 	= 1 << 2,
	FLAG_CHECK_SORTED	= 1 << 3,
	FLAG_RET_CNT	 	  = 1 << 4,
  FLAG_HELP         = 1 << 5
} op_type;

/* Parameter args*/
typedef struct merge_args {
	const char	*infile1;
	const char	*infile2;
	const char	*outfile;
	op_type		  flags;
	u_int	      *records;
} margs_t;

#endif
