/**
 * Author : Mukul Sharma (muksharma@cs.stonybrook.edu)
 * Copyright(C) 2016, Stony Brook University
 */


#include <linux/linkage.h>
#include <linux/moduleloader.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>
#include "sys_xmergesort.h"

#define BUFFER_SIZE	PAGE_SIZE
#define MDBG printk(KERN_DEFAULT "DBG:%s:%s:%d\n", __FILE__, __func__, __LINE__)

#define SAFE_FREE(_buf_)				\
	do {					        \
		if((_buf_) != NULL) {			\
			kfree((_buf_));			\
			(_buf_) = NULL;			\
		}				        \
	} while(0)

#define SAFE_PUTNAME(_name_)	        		\
	do {		                      		\
		if ((_name_) && !IS_ERR((_name_))) {	\
			putname((_name_));		\
		}			                \
	} while(0)

#define SAFE_FILPCLOSE(_filp_)				\
	do {						\
		if ((_filp_) && !IS_ERR((_filp_))) { 	\
			filp_close((_filp_), NULL);	\
		}			                \
	} while(0)

#define SAFE_REMOVE(_filp_)			  		                    \
	do {						        	            \
		if ((_filp_) && !IS_ERR((_filp_))) { 	                	    \
			vfs_unlink(d_inode((_filp_)->f_path.dentry->d_parent),	    \
					(_filp_)->f_path.dentry, NULL);	            \
		}					                            \
	} while(0)

/**
 * Assuming max word length per line is 200 chars
 */
#define MAXWORD_LEN 200
u_int rec_total;

typedef enum cmp_res {
	APPEND_STR1 	      = 1 << 0,
	APPEND_STR2		      = 1 << 1,
	APPEND_STR1_UNIQ 		= 1 << 2,
	APPEND_STR2_UNIQ 		= 1 << 3,
	APPEND_STR1_ERR     = 1 << 4,
	APPEND_STR2_ERR     = 1 << 5,
} res_t;


asmlinkage extern long (*sysptr)(void *arg);

/**
 * compare_str - compare two strings
 * @str1: string1 to compare
 * @len1: length of string1
 * @str2: string2 to compare
 * @len2: length of string2
 * @flags: user flags
 * @prev: last string to be appended to output file
 *
 * returns merge operation type based on comparison result.
 */
	res_t
compare_str(char *str1, int len1, char *str2, int len2, int flags, char *prev)
{
	/*
	 * TODO: Avoid checking of flags 
	 * -i option
	 */ 
	if (flags & FLAG_IGNORE_CASE) { 
		if (strcasecmp(str1, str2) < 0) {
			if (strcasecmp(str1, prev) < 0) {
				return APPEND_STR1_ERR;
			}
			if (flags & FLAG_UNIQUE_REC)
				return APPEND_STR1_UNIQ;
			return APPEND_STR1;
		} else {
			if (strcasecmp(str2, prev) < 0) {
				return APPEND_STR2_ERR;
			}
			if (flags & FLAG_UNIQUE_REC)
				return APPEND_STR2_UNIQ;
			return APPEND_STR2;
		}
	} else {
		if (strcmp(str1, str2) < 0) {
			if (strcmp(str1, prev) < 0) {
				return APPEND_STR1_ERR;
			}
			if (flags & FLAG_UNIQUE_REC)
				return APPEND_STR1_UNIQ;
			return APPEND_STR1;
		} else {
			if (strcmp(str2, prev) < 0) {
				return APPEND_STR2_ERR;
			}
			if (flags & FLAG_UNIQUE_REC)
				return APPEND_STR2_UNIQ;
			return APPEND_STR2;
		}
	}	
}

/**
 * getstring - extract the string ending with '\n'
 * @buf: input buffer which contains read data
 * @offset: from the offset to extract in buffer
 * @string: string placeholder
 *
 * returns the length of extracted string.
 */
	int
getstring(const char *buf, int offset, char string[])
{
	int local_offset = offset;
	int count = 0;
	while (buf[local_offset] != '\n') {
		BUG_ON(local_offset >= BUFFER_SIZE);
		BUG_ON(count >= MAXWORD_LEN);
		string[count] = buf[local_offset];
		local_offset++;
		count++;
		if (count >= MAXWORD_LEN) {
			BUG_ON("Maximum string length supported per line:200");
			break;
		}
	}
	string[count] = '\0';
	return count+1;
}

/**
 * append_record - append the record to output buffer
 * @record: string to be appended to output buffer
 * @dest: output buffer
 * @dest_offset: offset in output buffer
 * @src_offset: offset in input buffer
 * @len: length of record
 *
 * void
 */
	static inline void
append_record(char *record, void *dest,  int *dest_offset,
		int *src_offset, int len)
{	
	sprintf(dest+ *dest_offset, "%s\n", record);
	*dest_offset += len;
	*src_offset += len;	
	rec_total++;
}

#define APPEND_UNIQUE_REC(record, record_len, src_offset, prev)				            \
	do {											    \
		equal = flags & FLAG_IGNORE_CASE ? !strcasecmp((record), (prev)) :		    \
		!strcmp((record), (prev));		                                            \
		if (equal){									    \
			(src_offset) += (record_len);						    \
		} else {									    \
			append_record((record), dest, &doffset, &(src_offset), (record_len));	    \
			memcpy((prev), (record), (record_len));					    \
		}									            \
	} while(0)

#define APPEND_REM_RECS(flags, err, src, src_offset, src_len, record, record_len, prev)  	\
	do {  											\
		while((src_offset) < (src_len)) {						\
			(record_len) = getstring((const char*)(src), (src_offset), (record));	\
												\
			equal = (flags) & FLAG_IGNORE_CASE ? strcasecmp((record), (prev)) :     \
			strcmp((record), (prev));                       			\
			if(((equal == 0) && ((flags) & FLAG_UNIQUE_REC)) || (equal < 0)) {	\
				if (((flags) & FLAG_CHECK_SORTED) && (equal < 0)) {             \
					*(err) = -1;                                            \
					goto ret;                                               \
				} else {                                                        \
					(src_offset) += (record_len);				\
				}                                                               \
			} else {							        \
				append_record((record), dest, &doffset,			        \
						&(src_offset), (record_len));		        \
				memcpy((prev), (record), (record_len));			        \
			}								        \
		}     							                        \
	} while (0)

#define APPEND_REC_ERROR(flags, err, offset, len)     			\
	do {                                                  		\
		if ((flags) & FLAG_CHECK_SORTED) {                  	\
			*(err) = -1;                                    \
			goto ret;                                       \
		} else {                                            	\
			offset += len;                                  \
		}                                                   	\
	} while(0)

/**
 * merge_records - extracts two strings from input buffers, compares and
 *                 append the correct string to outbut buffer based on user flags
 * @src1: input buffer1
 * @len1: length of data present in input buffer1
 * @src2: input buffer2                  
 * @len2: length of data present in input buffer2
 * @dest: output buffer
 * @flags: user options
 * @merge_err: pointer to merge_err variable
 * @prev: last appended string or record to output buffer
 *
 * returns number of bytes written to output buffer.
 */
int
merge_records(void *src1, int len1, void *src2, int len2, void *dest,int flags,
		int *merge_err, char *prev)
{
	char 	string1[MAXWORD_LEN], string2[MAXWORD_LEN];
	int 	slen1, slen2;
	int	  offset1 = 0, offset2 = 0, doffset = 0;
	int 	equal;
	if (src2 == NULL) {
		BUG_ON(len2 >= 0);
		goto remaining_records;
	}	

	while (offset1 < len1 &&  offset2 < len2) {
		/*
		 * TODO: add one optimization here,
		 * avoid the re-extract of the
		 * same string which was not merged in previous loop.
		 */
		slen1 = getstring((const char*)src1, offset1, string1);
		slen2 = getstring((const char*)src2, offset2, string2);
		switch (compare_str(string1, slen1, string2, slen2, flags, prev)) {
			case APPEND_STR1:
				append_record(string1, dest, &doffset,
						&offset1, slen1);
				memcpy(prev, string1, slen1); 	
				break;
			case APPEND_STR2:
				append_record(string2, dest, &doffset,
						&offset2, slen2);
				memcpy(prev, string2, slen2); 	
				break;
			case APPEND_STR1_UNIQ:
				APPEND_UNIQUE_REC(string1, slen1, offset1, prev);
				break;	
			case APPEND_STR2_UNIQ:
				APPEND_UNIQUE_REC(string2, slen2, offset2, prev);
				break;
			case APPEND_STR1_ERR:
				APPEND_REC_ERROR(flags, merge_err, offset1, slen1);
				break;
			case APPEND_STR2_ERR:
				APPEND_REC_ERROR(flags, merge_err, offset2, slen2);
				break;
			default:
				BUG();
				break;
		}
	}
	/* Dump remaining sorted content into outbuf*/
	if (offset2 >= len2 && offset1 < len1) {
remaining_records:
		APPEND_REM_RECS(flags, merge_err, src1, offset1, len1, string1, slen1, prev);
	}

	if (offset1 >= len1 && offset2 < len2) {
		APPEND_REM_RECS(flags, merge_err, src2, offset2, len2, string2, slen2, prev);
	}

ret:
	return doffset;
}

/**
 * adjusted_bytes - calculates the adjusted bytes based on '\n' character.
 *                  if record crosses the boundary of BUFFER_SIZE, ignores that
 *                  record for current proccessing, this ignored record will be
 *                  captured in next iteration.
 * @buf: input buffer
 * @bytes: length of valid data in input buffer
 * 
 * return adjusted length of valid data in buffer to be merged in current iteration.
 */
	static inline int 
adjusted_bytes(char *buf, int bytes)
{
	int cindex;

	if (bytes <= 0) 
		return bytes;

	cindex = bytes - 1;

	if (buf[cindex] == '\n') {
		return bytes;
	} else {
		/* This case is for the end of file */
		if (cindex < BUFFER_SIZE-1) {
			buf[cindex+1] = '\n';
			return cindex+2;	
		}

		while(buf[cindex] != '\n' && cindex){
			cindex--;
		}
		return cindex+1;
	}

}

/**
 * These macros are just avoiding the duplicate code in the function
 * read_and_merge_files. some of the variables are common to the code,
 * so not passing explicitly in the code.
 */
#define APPEND_REMAINING_FILE(_filep_, _size_, _src_buffer_, _src_len_,   \
		_dest_,                                     \
		_flags_, _err_, _prev_)				              \
do {		  										                                            \
	while((_filep_)->f_pos < (_size_)) {							                    \
		bytes = kernel_read((_filep_), (_filep_)->f_pos,                    \
				(_src_buffer_), BUFFER_SIZE);	                  \
		bytes = adjusted_bytes((_src_buffer_), bytes);                      \
		if (bytes < 0) {								                                    \
			MDBG; 									                                          \
			ret = bytes;								                                      \
			goto cleanup;								                                      \
		}	else {								                                            \
			(_src_len_) = bytes;                                              \
		}                                                                   \
		bytes =  merge_records((_src_buffer_), (_src_len_), NULL, -1,       \
				(_dest_),                                    \
				(_flags_), &(_err_), (_prev_));              \
		ret = kernel_write(outfilp, (_dest_), bytes, outfilp->f_pos);			  \
		if (ret < 0) {									                                    \
			MDBG;									                                            \
			goto cleanup;								                                      \
		}										                                                \
		if (ret != bytes) {								                                  \
			ret = -1;								                                          \
			goto cleanup;								                                      \
		}										                                                \
		outfilp->f_pos += ret;								                              \
		(_filep_)->f_pos += (_src_len_);				                            \
	}											                                                \
} while(0)

#define CHECK_PTR_ERR(_p_)			\
	do {	  					              \
		if (IS_ERR((_p_))) {			  \
			MDBG;				              \
			ret = PTR_ERR((_p_));		  \
			goto cleanup;			        \
		}					                  \
	} while(0)

#define CHECK_FILEP(_filep_)		\
	do {	  					              \
		if (!(_filep_)) {			      \
			MDBG;				              \
			ret = -1;			            \
			goto cleanup;			        \
		}					                  \
		CHECK_PTR_ERR((_filep_));		\
	} while(0)

/**
 * read_and_merge_files - pulp of the implementation. reads two sorted or partially
 *                        input files by max BUFFER_SIZE chunk, merge them and put 
 *                        into placeholder output buffer. flush the output buffer
 *                        content to the output file.
 *                        does also all possible checks on input arguments and validity
 *                        of the user intended merge operation.
 * @arg: arguments passed from user
 *
 * returns positive value if successful else negative value.
 */                       
	int
read_and_merge_files(void *arg)
{
	struct file	      *infilp1 = NULL, *infilp2 = NULL, *outfilp = NULL;
	void 		          *inbuf1  = NULL, *inbuf2  = NULL, *outbuf  = NULL;
	struct filename   *infile1 = NULL, *infile2 = NULL, *outfile = NULL;
	int 		          bytes = -1;
	margs_t 	        marg;
	int		            insize1, insize2;
	int		            ret = 0;
	int		            flags;
	int 		          src_len1, src_len2;
	mode_t 		        mode = 0;
	int               merge_err = 0;
	char              prev[MAXWORD_LEN];

	rec_total = 0;

	if (copy_from_user(&marg, arg, sizeof(margs_t))) {
		MDBG;
		ret = -EFAULT;
		goto cleanup;
	}

	if (!access_ok(VERIFY_READ, marg.infile1, sizeof((marg.infile1))) ||
			!access_ok(VERIFY_READ, marg.infile2, sizeof((marg.infile2))) ||
			!access_ok(VERIFY_READ, marg.outfile, sizeof((marg.outfile))) || 
			!access_ok(VERIFY_WRITE, marg.records, sizeof((marg.records)))) {
		MDBG;
		ret = -EFAULT;
		goto cleanup;
	}

	infile1 = getname((const char __user *)marg.infile1);
	CHECK_PTR_ERR(infile1);
	infile2 = getname((const char __user *)marg.infile2);
	CHECK_PTR_ERR(infile2);
	outfile = getname((const char __user *)marg.outfile);
	CHECK_PTR_ERR(outfile);

	flags	= marg.flags;

	printk("Input Filename:%s\n", infile1->name);
	printk("Input Filename:%s\n", infile2->name);
	printk("Output Filename:%s\n", outfile->name);

	if (!strcmp(infile1->name,infile2->name) ||
			!strcmp(infile1->name, outfile->name) ||
			!strcmp(infile2->name,outfile->name))  {
		MDBG;
		ret = -EINVAL;
		goto cleanup;
	}

	/*
	 * open I/P files in read only mode.
	 */ 
	infilp1 = filp_open(infile1->name, O_RDONLY, 0);
	CHECK_FILEP(infilp1);

	infilp2 = filp_open(infile2->name, O_RDONLY, 0);
	CHECK_FILEP(infilp2);
	/*
	 * merge should be allowed only on regular files.
	 */
#define inode_mode(_filep_) ((_filep_)->f_inode->i_mode)
#define _mode(T,_mode_)  ((S_IRWX ## T) & (_mode_))
	if (!S_ISREG(inode_mode(infilp1)) || !S_ISREG(inode_mode(infilp2))) {
		ret = -EPERM;
		goto cleanup;
	} else {
		/*
		 * mode of output file will be no more permissive than the
		 * input file permissions.
		 * e.g. if infile1 : 0555, infile2 : 0400
		 *      then outfile permissions : 0400
		 */        
		mode |= _mode(U,inode_mode(infilp1)) > _mode(U,inode_mode(infilp2))? 
			_mode(U,inode_mode(infilp2)):
			_mode(U,inode_mode(infilp1));

		mode |= _mode(G,inode_mode(infilp1)) > _mode(G,inode_mode(infilp2))? 
			_mode(G,inode_mode(infilp2)):
			_mode(G,inode_mode(infilp1));

		mode |= _mode(O,inode_mode(infilp1)) > _mode(O,inode_mode(infilp2))? 
			_mode(O,inode_mode(infilp2)):
			_mode(O,inode_mode(infilp1));
		printk ("Output file mode: %o\n", mode);
	}

	/*
	 * create output file in exclusive mode. don't overwrite if file is already present.
	 */
	outfilp = filp_open(outfile->name, O_CREAT | O_RDWR | O_TRUNC | O_EXCL, mode);
	CHECK_FILEP(outfilp);

	if(!S_ISREG(inode_mode(outfilp))) {
		ret = -EPERM;
		goto cleanup;
	}

	/*
	 * I/P and O/P files should be in same File System
	 */
#define inode_superblk(_filep_) ((_filep_)->f_inode->i_sb)
	if (inode_superblk(infilp1) != inode_superblk(infilp2) ||
			inode_superblk(infilp1) != inode_superblk(outfilp) ||
			inode_superblk(infilp2) != inode_superblk(outfilp)) {
		ret = -EACCES;
		goto cleanup;
	}

	/*
	 * I/P and O/P files should be different.
	 */ 
#define inode_num(_filep_) ((_filep_)->f_inode->i_ino)
	if(inode_num(infilp1) == inode_num(outfilp) ||
			inode_num(infilp2) == inode_num(outfilp)) {
		ret = -EINVAL;
		goto cleanup;
	}

#define file_size(_filep_) ((_filep_)->f_inode->i_size)
	insize1 = file_size(infilp1);
	insize2 = file_size(infilp2);
	if (insize1 < 0 || insize2 < 0) {
		ret = -EBADF;
		goto cleanup;
	}

#define BUF_ALLOCD_CHECK(_buf_)         \
	if ((_buf_) == NULL) {            \
		ret = -ENOMEM;                \
		goto cleanup;                 \
	}

	if (insize1 > 0) { 
		inbuf1  = kmalloc(sizeof(char)*BUFFER_SIZE, GFP_KERNEL);
		BUF_ALLOCD_CHECK(inbuf1);
	}

	if (insize2 > 0) {
		inbuf2  = kmalloc(sizeof(char)*BUFFER_SIZE, GFP_KERNEL);
		BUF_ALLOCD_CHECK(inbuf2);
	}

	if (insize1 || insize2) { 
		outbuf  = kmalloc(2*sizeof(char)*BUFFER_SIZE, GFP_KERNEL);
		BUF_ALLOCD_CHECK(outbuf);
	}


	infilp1->f_pos = 0;		/* start offset */
	infilp2->f_pos = 0;		/* start offset */
	outfilp->f_pos = 0;		/* start offset */

	while ((infilp1->f_pos < insize1) && (infilp2->f_pos < insize2)) {
		/*read chunk from input file1 */
		bytes = kernel_read(infilp1, infilp1->f_pos, inbuf1, BUFFER_SIZE);
		bytes = adjusted_bytes(inbuf1, bytes);
		if (bytes < 0) {
			ret = bytes;
			goto cleanup;
		} else {
			src_len1 = bytes;
		}

		/* read chunk from input file2 */
		bytes = kernel_read(infilp2, infilp2->f_pos, inbuf2, BUFFER_SIZE);
		bytes = adjusted_bytes(inbuf2, bytes);
		if (bytes < 0) {
			ret = bytes;
			goto cleanup;
		} else {
			src_len2 = bytes;
		}

		if (src_len1 + src_len2 > 2 * BUFFER_SIZE) {
			ret = -1;
			goto cleanup;
		}
		/* merge the records and put into outbuf */
		bytes = merge_records(inbuf1, src_len1, inbuf2, src_len2,
				outbuf, flags, &merge_err, prev);
		ret = kernel_write(outfilp, outbuf, bytes, outfilp->f_pos);	
		if (ret < 0) {
			goto cleanup;
		}

		if (merge_err == -1) {
			ret = -1;
			goto cleanup;
		}

		outfilp->f_pos += ret;
		infilp1->f_pos += src_len1;
		infilp2->f_pos += src_len2;	
	}

	/*
	 * append remaining recs in correct fashion.
	 */ 
	if ((infilp2->f_pos >= insize2) && (infilp1->f_pos < insize1)) {
		APPEND_REMAINING_FILE(infilp1, insize1, inbuf1, src_len1,
				outbuf, flags, merge_err, prev);
	}

	if ((infilp1->f_pos >= insize1) && (infilp2->f_pos < insize2)) {
		APPEND_REMAINING_FILE(infilp2, insize2, inbuf2, src_len2,
				outbuf, flags, merge_err, prev);
	}

	bytes = outfilp->f_pos;
	ret = bytes;

cleanup:
	SAFE_PUTNAME(infile1);
	SAFE_PUTNAME(infile2);
	SAFE_PUTNAME(outfile);
	SAFE_FILPCLOSE(infilp1);
	SAFE_FILPCLOSE(infilp2);
	SAFE_FREE(inbuf1);
	SAFE_FREE(inbuf2);
	SAFE_FREE(outbuf);

	if (ret >= 0) {
		if(flags & FLAG_RET_CNT &&
				copy_to_user((void __user *)marg.records,
					&rec_total, sizeof(rec_total)) != 0) {
			ret = -EFAULT;
		}
		SAFE_FILPCLOSE(outfilp);
		printk("Dumped sorted contents: %d bytes\n", bytes);
	} else {
		if (merge_err == 0) {
			SAFE_REMOVE(outfilp);
		}
		SAFE_FILPCLOSE(outfilp);
		printk("Couldn't complete successfully %d\n",ret);
	}
	return ret;
}

/**
 * xmergesort - does merge of two sorted/partially sorted input files
 * @arg: user argument
 *
 * returns success(>=0)/ failure(<0).
 */
asmlinkage long xmergesort(void *arg)
{
	int  		ret = 0;
	/* dummy syscall: returns 0 for non null, -EINVAL for NULL */
	if (arg == NULL) {
		return -EINVAL;
	}
	printk ("arg ptr value:%p\n", arg);
	ret  = read_and_merge_files(arg);
	printk("xmergesort received ret  %d\n", ret);
	return	ret; 
}

/**
 * init_sys_xmergesort - initialize the sys_xmergesort module
 */
static int __init init_sys_xmergesort(void)
{
	printk("installed new sys_xmergesort module\n");
	if (sysptr == NULL)
		sysptr = xmergesort;
	return 0;
}

/**
 * exit_sys_xmergesort - remove sys_xmergesort module
 */ 
static void  __exit exit_sys_xmergesort(void)
{
	if (sysptr != NULL)
		sysptr = NULL;
	printk("removed sys_xmergesort module\n");
}

module_init(init_sys_xmergesort);
module_exit(exit_sys_xmergesort);
MODULE_LICENSE("GPL");
