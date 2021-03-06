/* Buffer management for tar.
   Copyright (C) 1988 Free Software Foundation

This file is part of GNU Tar.

GNU Tar is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 1, or (at your option)
any later version.

GNU Tar is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Tar; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/*
 * Buffer management for tar.
 *
 * Written by John Gilmore, ihnp4!hoptoad!gnu, on 25 August 1985.
 *
 * @(#) buffer.c 1.28 11/6/87 - gnu
 */

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>		/* For non-Berkeley systems */
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <assert.h>

#ifndef MSDOS
#include <sys/ioctl.h>
#ifndef USG
#include <sys/mtio.h>
#endif
#endif

#ifdef	MSDOS
# include <fcntl.h>
#include <process.h>
#else
# ifdef XENIX
#  include <sys/inode.h>
# endif
# include <sys/file.h>
#endif

extern int errno;
extern union record *head;

#include "tar.h"
#include "port.h"
#include "rmt.h"

/* Either stdout or stderr:  The thing we write messages (standard msgs, not
   errors) to.  Stdout unless we're writing a pipe, in which case stderr */
FILE *msg_file = stdout;

#define	STDIN	0		/* Standard input  file descriptor */
#define	STDOUT	1		/* Standard output file descriptor */

#define	PREAD	0		/* Read  file descriptor from pipe() */
#define	PWRITE	1		/* Write file descriptor from pipe() */

#ifdef __GNU__
extern void	*malloc();
extern void	*valloc();
#else
extern char	*malloc();
extern char	*valloc();
#endif

extern char	*index(), *strcat();
extern char	*strcpy();

/*
 * V7 doesn't have a #define for this.
 */
#ifndef O_RDONLY
#define	O_RDONLY	0
#endif
#ifndef O_RDWR
#define O_RDWR		2
#endif
#ifndef O_CREAT
#define O_CREAT		0
#endif
#ifndef O_BINARY
#define O_BINARY	0
#endif

#define	MAGIC_STAT	105	/* Magic status returned by child, if
				   it can't exec.  We hope compress/sh
				   never return this status! */

static void _writeerror();
#define writeerror(err)     _writeerror(err, __LINE__)

void readerror();

void ck_pipe();
void ck_close();

extern void finish_header();

/*
 * The record pointed to by save_rec should not be overlaid
 * when reading in a new tape block.  Copy it to record_save_area first, and
 * change the pointer in *save_rec to point to record_save_area.
 * Saved_recno records the record number at the time of the save.
 * This is used by annofile() to print the record number of a file's
 * header record.
 */
static union record **save_rec;
 union record record_save_area;
static long	    saved_recno;

/*
 * PID of child program, if f_compress or remote archive access.
 */
static int	childpid = 0;

/*
 * Record number of the start of this block of records
 */
long	baserec;

/*
 * Error recovery stuff
 */
static int	r_error_count;

/*
 * Have we hit EOF yet?
 */
static int	eof;

/* JF we're reading, but we just read the last record and its time to update */
extern time_to_start_writing;
int file_to_switch_to= -1;	/* If remote update, close archive, and use
				   this descriptor to write to */

static int volno = 1;		/* JF which volume of a multi-volume tape
				   we're on */

char *save_name = 0;		/* Name of the file we are currently writing */
long save_totsize;		/* total size of file we are writing.  Only
				   valid if save_name is non_zero */
long save_sizeleft;		/* Where we are in the file we are writing.
				   Only valid if save_name is non-zero */

int write_archive_to_stdout;

/* Used by fl_read and fl_write to store the real info about saved names */
static char real_s_name[NAMSIZ];
static long real_s_totsize;
static long real_s_sizeleft;

/*
 * Return the location of the next available input or output record.
 * Return NULL for EOF.  Once we have returned NULL, we just keep returning
 * it, to avoid accidentally going on to the next file on the "tape".
 * 
 * Side effects:
 * 	Might move the saved record (if any), invalidating any saved
 * 	pointers into it.
 */
union record *
findrec()
{
	if (ar_record == ar_last) {
		if (eof)
			return (union record *)NULL;	/* EOF */
		flush_archive();
		if (ar_record == ar_last) {
			eof++;
			return (union record *)NULL;	/* EOF */
		}
	}
	return ar_record;
}


/*
 * Indicate that we have used all records up thru the argument.
 * (should the arg have an off-by-1? XXX FIXME)
 */
void
userec(rec)
	union record *rec;
{
	while(rec >= ar_record)
		ar_record++;
	/*
	 * Do NOT flush the archive here.  If we do, the same
	 * argument to userec() could mean the next record (if the
	 * input block is exactly one record long), which is not what
	 * is intended.
	 */
	if (ar_record > ar_last)
		abort();
}


/*
 * Return a pointer to the end of the current records buffer.
 * All the space between findrec() and endofrecs() is available
 * for filling with data, or taking data from.
 */
union record *
endofrecs()
{
	return ar_last;
}


/*
 * Duplicate a file descriptor into a certain slot.
 * Equivalent to BSD "dup2" with error reporting.
 */
void
dupto(from, to, msg)
	int from, to;
	char *msg;
{
	int err;

	if (from != to) {
		err=close(to);
		if(err<0 && errno!=EBADF) {
			msg_perror("Cannot close descriptor %d",to);
			exit(EX_SYSTEM);
		}
		err = dup(from);
		if (err != to) {
			msg_perror("cannot dup %s",msg);
			exit(EX_SYSTEM);
		}
		ck_close(from);
	}
}

#ifdef MSDOS
void
child_open()
{
	fprintf(stderr,"MSDOS %s can't use compressed or remote archives\n",tar);
	exit(EX_ARGSBAD);
}
#else
void
child_open()
{
	int pipe[2];
	int err = 0;

	int kidpipe[2];
	int kidchildpid;

#define READ	0
#define WRITE	1

	ck_pipe(pipe);

	childpid=fork();
	if(childpid<0) {
		msg_perror("cannot fork");
		exit(EX_SYSTEM);
	}
	if(childpid>0) {
		/* We're the parent.  Clean up and be happy */
		/* This, at least, is easy */

		if(ar_reading) {
			f_reblock++;
			archive=pipe[READ];
			ck_close(pipe[WRITE]);
		} else {
			archive = pipe[WRITE];
			ck_close(pipe[READ]);
		}
		return;
	}

	/* We're the kid */
	if(ar_reading) {
		dupto(pipe[WRITE],STDOUT,"(child) pipe to stdout");
		ck_close(pipe[READ]);
	} else {
		dupto(pipe[READ],STDIN,"(child) pipe to stdin");
		ck_close(pipe[WRITE]);
	}

	/* We need a child tar only if
	   1: we're reading/writing stdin/out (to force reblocking)
	   2: the file is to be accessed by rmt (compress doesn't know how)
	   3: the file is not a plain file */
#ifdef NO_REMOTE
	if(!(ar_file[0]=='-' && ar_file[1]=='\0') && isfile(ar_file))
#else
	if(!(ar_file[0]=='-' && ar_file[1]=='\0') && !_remdev(ar_file) && isfile(ar_file))
#endif
	{
		/* We don't need a child tar.  Open the archive */
		if(ar_reading) {
			archive=open(ar_file, O_RDONLY|O_BINARY, 0666);
			if(archive<0) {
				msg_perror("can't open archive %s",ar_file);
				exit(EX_BADARCH);
			}
			dupto(archive,STDIN,"archive to stdin");
			/* close(archive); */
		} else {
			archive=creat(ar_file,0666);
			if(archive<0) {
				msg_perror("can't open archive %s",ar_file);
				exit(EX_BADARCH);
			}
			dupto(archive,STDOUT,"archive to stdout");
			/* close(archive); */
		}
	} else {
		/* We need a child tar */
		ck_pipe(kidpipe);

		kidchildpid=fork();
		if(kidchildpid<0) {
			msg_perror("child can't fork");
			exit(EX_SYSTEM);
		}

		if(kidchildpid>0) {
			/* About to exec compress:  set up the files */
			if(ar_reading) {
				dupto(kidpipe[READ],STDIN,"((child)) pipe to stdin");
				ck_close(kidpipe[WRITE]);
				/* dup2(pipe[WRITE],STDOUT); */
			} else {
				/* dup2(pipe[READ],STDIN); */
				dupto(kidpipe[WRITE],STDOUT,"((child)) pipe to stdout");
				ck_close(kidpipe[READ]);
			}
			/* ck_close(pipe[READ]); */
			/* ck_close(pipe[WRITE]); */
			/* ck_close(kidpipe[READ]);
			ck_close(kidpipe[WRITE]); */
		} else {
		/* Grandchild.  Do the right thing, namely sit here and
		   read/write the archive, and feed stuff back to compress */
			tar="tar (child)";
			if(ar_reading) {
				dupto(kidpipe[WRITE],STDOUT,"[child] pipe to stdout");
				ck_close(kidpipe[READ]);
			} else {
				dupto(kidpipe[READ],STDIN,"[child] pipe to stdin");
				ck_close(kidpipe[WRITE]);
			}

			if (ar_file[0] == '-' && ar_file[1] == '\0') {
				if (ar_reading)
					archive = STDIN;
				else
					archive = STDOUT;
			} else /* This can't happen if (ar_reading==2)
				archive = rmtopen(ar_file, O_RDWR|O_CREAT|O_BINARY, 0666);
			else */if(ar_reading)
				archive = rmtopen(ar_file, O_RDONLY|O_BINARY, 0666);
			else
				archive = rmtcreat(ar_file, 0666);

			if (archive < 0) {
				msg_perror("can't open archive %s",ar_file);
				exit(EX_BADARCH);
			}

			if(ar_reading) {
				for(;;) {
					char *ptr;
					int max,count;
		
					r_error_count = 0;
				error_loop:
					err=rmtread(archive, ar_block->charptr,(int)(blocksize));
					if(err<0) {
						readerror();
						goto error_loop;
					}
					if(err==0)
						break;
					ptr = ar_block->charptr;
					max = err;
					while(max) {
						count = (max<RECORDSIZE) ? max : RECORDSIZE;
						err=write(STDOUT,ptr,count);
						if(err!=count) {
							if(err<0) {
								msg_perror("can't write to compress");
								exit(EX_SYSTEM);
							} else
								msg("write to compress short %d bytes",count-err);
							count = (err<0) ? 0 : err;
						}
						ptr+=count;
						max-=count;
					}
				}
			} else {
				for(;;) {
					int n;
					char *ptr;
		
					n=blocksize;
					ptr = ar_block->charptr;
					while(n) {
						err=read(STDIN,ptr,(n<RECORDSIZE) ? n : RECORDSIZE);
						if(err<=0)
							break;
						n-=err;
						ptr+=err;
					}
						/* EOF */
					if(err==0) {
						if(f_compress<2)
							blocksize-=n;
						else
							bzero(ar_block->charptr+n,blocksize-n);
						err=rmtwrite(archive,ar_block->charptr,blocksize);
						if(err!=(blocksize))
							writeerror(err);
						if(f_compress<2)
							blocksize+=n;
						break;
					}
					if(n) {
						msg_perror("can't read from compress");
						exit(EX_SYSTEM);
					}
					err=rmtwrite(archive, ar_block->charptr, (int)blocksize);
					if(err!=blocksize)
						writeerror(err);
				}
			}
		
			/* close_archive(); */
			if (f_debug) {
			    fprintf(stderr, "Tar.gnu: exiting normally\n");
			}
			exit(0);
		}
	}
		/* So we should exec compress (-d) */
	if(ar_reading)
		execlp("compress", "compress", "-d", (char *)0);
	else
		execlp("compress", "compress", (char *)0);
	msg_perror("can't exec compress");
	_exit(EX_SYSTEM);
}


/* return non-zero if p is the name of a directory */
isfile(p)
char *p;
{
	struct stat stbuf;

	if(stat(p,&stbuf)<0)
		return 1;
	if((stbuf.st_mode&S_IFMT)==S_IFREG)
		return 1;
	return 0;
}

#endif

/*
 * Open an archive file.  The argument specifies whether we are
 * reading or writing.
 */
/* JF if the arg is 2, open for reading and writing. */
open_archive(reading)
	int reading;
{
	msg_file = stdout;

	if (blocksize == 0) {
		msg("invalid value for blocksize");
		exit(EX_ARGSBAD);
	}

	if(ar_file==0) {
		msg("No archive name given, what should I do?");
		exit(EX_BADARCH);
	}

	/*NOSTRICT*/
	if(f_multivol) {
		ar_block = (union record *) valloc((unsigned)(blocksize+(2*RECORDSIZE)));
		if(ar_block)
			ar_block += 2;
	} else
		ar_block = (union record *) valloc((unsigned)blocksize);
	if (!ar_block) {
		msg("could not allocate memory for blocking factor %d",
			blocking);
		exit(EX_ARGSBAD);
	}

	ar_record = ar_block;
	ar_last   = ar_block + blocking;
	ar_reading = reading;

	if (f_compress) {
		if(reading==2 || f_verify) {
			msg("cannot update or verify compressed archives");
			exit(EX_ARGSBAD);
		}
		child_open();
		if(!reading && ar_file[0]=='-' && ar_file[1]=='\0')
			msg_file = stderr;
		/* child_open(rem_host, rem_file); */
	} else if (ar_file[0] == '-' && ar_file[1] == '\0') {
		f_reblock++;	/* Could be a pipe, be safe */
		if(f_verify) {
			msg("can't verify stdin/stdout archive");
			exit(EX_ARGSBAD);
		}
		if(reading==2) {
			archive=STDIN;
			msg_file=stderr;
			write_archive_to_stdout++;
		} else if (reading)
			archive = STDIN;
		else {
			archive = STDOUT;
			msg_file = stderr;
		}
	} else if (reading==2 || f_verify) {
		archive = rmtopen(ar_file, O_RDWR|O_CREAT|O_BINARY, 0666);
	} else if(reading) {
		archive = rmtopen(ar_file, O_RDONLY|O_BINARY, 0666);
	} else {
		archive = rmtcreat(ar_file, 0666);
	}

	if (archive < 0) {
		msg_perror("can't open %s",ar_file);
		exit(EX_BADARCH);
	}
#ifdef	MSDOS
	setmode(archive, O_BINARY);
#endif

	if (reading) {
		ar_last = ar_block;		/* Set up for 1st block = # 0 */
		(void) findrec();		/* Read it in, check for EOF */

		if(f_volhdr) {
			union record *header;
			char *ptr;

			if(f_multivol) {
				ptr=malloc(strlen(f_volhdr)+20);
				sprintf(ptr,"%s Volume %d",f_volhdr,1);
			} else
				ptr=f_volhdr;
			header=findrec();
			if(!header) {
			    fprintf(stderr, "Null input\n");
			    exit(EX_BADARCH);
			}
			if(strcmp(ptr,header->header.name)) {
				msg("Volume mismatch!  %s!=%s\n",ptr,header->header.name);
				exit(EX_BADARCH);
			}
			if(ptr!=f_volhdr)
				free(ptr);
		}
	} else if(f_volhdr) {
		bzero((void *)ar_block,RECORDSIZE);
		if(f_multivol)
			sprintf(ar_block->header.name,"%s Volume 1",f_volhdr);
		else
			strcpy(ar_block->header.name,f_volhdr);
		ar_block->header.linkflag = LF_VOLHDR;
		finish_header(ar_block);
		/* ar_record++; */
	}
}


/*
 * Remember a union record * as pointing to something that we
 * need to keep when reading onward in the file.  Only one such
 * thing can be remembered at once, and it only works when reading
 * an archive.
 *
 * We calculate "offset" then add it because some compilers end up
 * adding (baserec+ar_record), doing a 9-bit shift of baserec, then
 * subtracting ar_block from that, shifting it back, losing the top 9 bits.
 */
saverec(pointer)
	union record **pointer;
{
	long offset;
#if 0
	static num_saved;

	if (pointer == NULL) {
	    assert(num_saved == 1);
	    num_saved = 0;
	} else {
	    assert(num_saved == 0);
	    num_saved = 1;
	}
#endif
	assert(pointer == NULL || pointer == &head);
	assert(save_rec == NULL || save_rec == &head);
	if (save_rec != NULL && pointer != NULL) {
		msg("Warning: saving a record over another one.");
	}
	save_rec = pointer;
	offset = ar_record - ar_block;
	saved_recno = baserec + offset;
}

/*
 * Perform a write to flush the buffer.
 */

/*send_buffer_to_file();
  if(new_volume) {
  	deal_with_new_volume_stuff();
	send_buffer_to_file();
  }
 */

fl_write()
{
	int err;
	int copy_back;
#ifdef TEST
	static long test_written = 0;
#endif

#ifdef TEST
	if(test_written>=30720) {
		errno = ENOSPC;
		err = 0;
	} else
#endif
	err = rmtwrite(archive, ar_block->charptr,(int) blocksize);
	if(err!=blocksize && !f_multivol)
		writeerror(err);

#ifdef TEST
	if(err>0)
		test_written+=err;
#endif
	if (err == blocksize) {
		if(f_multivol) {
			if(!save_name) {
				real_s_name[0]='\0';
				real_s_totsize=0;
				real_s_sizeleft = 0;
				return;
			}
#ifdef MSDOS
			if(save_name[1]==':')
				save_name+=2;
#endif
			while(*save_name=='/')
				save_name++;

			strcpy(real_s_name,save_name);
			real_s_totsize = save_totsize;
			real_s_sizeleft = save_sizeleft;
		}
		return;
	}

	/* We're multivol  Panic if we didn't get the right kind of response */
	/* ENXIO is for the UNIX PC */
	if(err>0 || (err<0 && errno!=ENOSPC && errno!=EIO && errno!=ENXIO))
		writeerror(err);

	if(new_volume(0)<0)
		return;
#ifdef TEST
	test_written=0;
#endif
	if(f_volhdr && real_s_name[0]) {
		copy_back=2;
		ar_block-=2;
	} else if(f_volhdr || real_s_name[0]) {
		copy_back = 1;
		ar_block--;
	} else
		copy_back = 0;
	if(f_volhdr) {
		bzero((void *)ar_block,RECORDSIZE);
		sprintf(ar_block->header.name,"%s Volume %d",f_volhdr,volno);
		ar_block->header.linkflag = LF_VOLHDR;
		finish_header(ar_block);
	}
	if(real_s_name[0]) {
		extern void to_oct();
		int tmp;

		if(f_volhdr)
			ar_block++;
		bzero((void *)ar_block,RECORDSIZE);
		strcpy(ar_block->header.name,real_s_name);
		ar_block->header.linkflag = LF_MULTIVOL;
		to_oct((long)real_s_sizeleft,1+12,
		       ar_block->header.size);
		to_oct((long)real_s_totsize-real_s_sizeleft,
		       1+12,ar_block->header.offset);
		tmp=f_verbose;
		f_verbose=0;
		finish_header(ar_block);
		f_verbose=tmp;
		if(f_volhdr)
			ar_block--;
	}

	err = rmtwrite(archive, ar_block->charptr,(int) blocksize);
	if(err!=blocksize)
		writeerror(err);

#ifdef TEST
	test_written = blocksize;
#endif
	if(copy_back) {
		ar_block+=copy_back;
		bcopy((void *)(ar_block+blocking-copy_back),
		      (void *)ar_record,
		      copy_back*RECORDSIZE);
		ar_record+=copy_back;

		if(real_s_sizeleft>=copy_back*RECORDSIZE)
			real_s_sizeleft-=copy_back*RECORDSIZE;
		else if((real_s_sizeleft+RECORDSIZE-1)/RECORDSIZE<=copy_back)
			real_s_name[0] = '\0';
		else {
#ifdef MSDOS
			if(save_name[1]==':')
				save_name+=2;
#endif
			while(*save_name=='/')
				save_name++;

			strcpy(real_s_name,save_name);
			real_s_sizeleft = save_sizeleft;
			real_s_totsize=save_totsize;
		}
		copy_back = 0;
	}
}

/* Handle write errors on the archive.  Write errors are always fatal */
/* Hitting the end of a volume does not cause a write error unless the write
*  was the first block of the volume */

static void
_writeerror(err, line)
    int err;
    int line;
{
	if (err < 0) {
		msg_perror("can't write to %s",ar_file);
		fprintf(stderr, "line = %d\n", line);
		exit(EX_BADARCH);
	} else {
		msg("only wrote %u of %u bytes to %s",err,blocksize,ar_file);
		fprintf(stderr, "line = %d\n", line);
		exit(EX_BADARCH);
	}
}

/*
 * Handle read errors on the archive.
 *
 * If the read should be retried, readerror() returns to the caller.
 */
void
readerror()
{
#	define	READ_ERROR_MAX	10

	read_error_flag++;		/* Tell callers */

	msg_perror("read error on %s",ar_file);

	if (baserec == 0) {
		/* First block of tape.  Probably stupidity error */
		exit(EX_BADARCH);
	}

	/*
	 * Read error in mid archive.  We retry up to READ_ERROR_MAX times
	 * and then give up on reading the archive.  We set read_error_flag
	 * for our callers, so they can cope if they want.
	 */
	if (r_error_count++ > READ_ERROR_MAX) {
		msg("Too many errors, quitting.");
		exit(EX_BADARCH);
	}
	return;
}


/*
 * Perform a read to flush the buffer.
 */
fl_read()
{
	int err;		/* Result from system call */
	int left;		/* Bytes left */
	char *more;		/* Pointer to next byte to read */

	/*
	 * Clear the count of errors.  This only applies to a single
	 * call to fl_read.  We leave read_error_flag alone; it is
	 * only turned off by higher level software.
	 */
	r_error_count = 0;	/* Clear error count */

	/*
	 * If we are about to wipe out a record that
	 * somebody needs to keep, copy it out to a holding
	 * area and adjust somebody's pointer to it.
	 */
	if (save_rec &&
	    *save_rec >= ar_record &&
	    *save_rec < ar_last) {
		record_save_area = **save_rec;
		*save_rec = &record_save_area;
	}
	if(write_archive_to_stdout && baserec!=0) {
		err=rmtwrite(1, ar_block->charptr, blocksize);
		if(err!=blocksize)
			writeerror(err);
	}
	if(f_multivol) {
		if(save_name) {
			if(save_name!=real_s_name) {
#ifdef MSDOS
				if(save_name[1]==':')
					save_name+=2;
#endif
				while(*save_name=='/')
					save_name++;

				strcpy(real_s_name,save_name);
				save_name=real_s_name;
			}
			real_s_totsize = save_totsize;
			real_s_sizeleft = save_sizeleft;
				
		} else {
			real_s_name[0]='\0';
			real_s_totsize=0;
			real_s_sizeleft = 0;
		}
	}

error_loop:
	err = rmtread(archive, ar_block->charptr, (int)blocksize);
	if (err == blocksize)
		return;

	if((err == 0 || (err<0 && errno==ENOSPC)) && f_multivol) {
		union record *header;

	try_volume:
		if(new_volume((cmd_mode==CMD_APPEND || cmd_mode==CMD_CAT || cmd_mode==CMD_UPDATE) ? 2 : 1)<0)
			return;
	vol_error:
		err = rmtread(archive, ar_block->charptr,(int) blocksize);
		if(err < 0) {
			readerror();
			goto vol_error;
		}
		if(err!=blocksize)
			goto short_read;

		header=ar_block;

		if(header->header.linkflag==LF_VOLHDR) {
			if(f_volhdr) {
				char *ptr;

				ptr=(char *)malloc(strlen(f_volhdr)+20);
				sprintf(ptr,"%s Volume %d",f_volhdr,volno);
				if(strcmp(ptr,header->header.name)) {
					msg("Volume mismatch! %s!=%s\n",ptr,header->header.name);
					--volno;
					free(ptr);
					goto try_volume;
				}
				free(ptr);
			}
			if(f_verbose)
				fprintf(msg_file,"Reading %s\n",header->header.name);
			header++;
		} else if(f_volhdr) {
			msg("Warning:  No volume header!");
		}

		if(real_s_name[0]) {
			long from_oct();

			if(header->header.linkflag!=LF_MULTIVOL || strcmp(header->header.name,real_s_name)) {
				msg("%s is not continued on this volume!",real_s_name);
				--volno;
				goto try_volume;
			}
			if(real_s_totsize!=from_oct(1+12,header->header.size)+from_oct(1+12,header->header.offset)) {
				msg("%s is the wrong size (%ld!=%ld+%ld)",
				       header->header.name,save_totsize,
				       from_oct(1+12,header->header.size),
				       from_oct(1+12,header->header.offset));
				--volno;
				goto try_volume;
			}
			if(real_s_totsize-real_s_sizeleft!=from_oct(1+12,header->header.offset)) {
				msg("This volume is out of sequence");
				--volno;
				goto try_volume;
			}
			header++;
		}
		ar_record=header;
		return;
	} else if (err < 0) {
		readerror();
		goto error_loop;	/* Try again */
	}

 short_read:
	more = ar_block->charptr + err;
	left = blocksize - err;

again:
	if (0 == (((unsigned)left) % RECORDSIZE)) {
		/* FIXME, for size=0, multi vol support */
		/* On the first block, warn about the problem */
		if (!f_reblock && baserec == 0 && f_verbose && err > 0) {
		/*	msg("Blocksize = %d record%s",
				err / RECORDSIZE, (err > RECORDSIZE)? "s": "");*/
			msg("Blocksize = %d records", err / RECORDSIZE);
		}
		ar_last = ar_block + ((unsigned)(blocksize - left))/RECORDSIZE;
		return;
	}
	if (f_reblock) {
		/*
		 * User warned us about this.  Fix up.
		 */
		if (left > 0) {
error2loop:
			err = rmtread(archive, more, (int)left);
			if (err < 0) {
				readerror();
				goto error2loop;	/* Try again */
			}
			if (err == 0) {
				msg("archive %s EOF not on block boundary",ar_file);
				exit(EX_BADARCH);
			}
			left -= err;
			more += err;
			goto again;
		}
	} else {
		msg("only read %d bytes from archive %s",err,ar_file);
		exit(EX_BADARCH);
	}
}


/*
 * Flush the current buffer to/from the archive.
 */
flush_archive()
{
	baserec += ar_last - ar_block;	/* Keep track of block #s */
	ar_record = ar_block;		/* Restore pointer to start */
	ar_last = ar_block + blocking;	/* Restore pointer to end */

	if (ar_reading) {
		if(time_to_start_writing) {
			time_to_start_writing=0;
			ar_reading=0;

			if(file_to_switch_to>=0) {
				rmtclose(archive);
				archive=file_to_switch_to;
			} else
	 			(void)backspace_output();
			fl_write();
		} else
			fl_read();
	} else {
		fl_write();
	}
}

/* Backspace the archive descriptor by one blocks worth.
   If its a tape, MTIOCTOP will work.  If its something else,
   we try to seek on it.  If we can't seek, we lose! */
backspace_output()
{
	long cur;
	/* int er; */
	extern char *output_start;

#ifdef MTIOCTOP
	struct mtop t;

	t.mt_op = MTBSR;
	t.mt_count = 1;
	if((rmtioctl(archive,MTIOCTOP,&t))>=0)
		return 1;
	if(errno==EIO && (rmtioctl(archive,MTIOCTOP,&t))>=0)
		return 1;
#endif

		cur=rmtlseek(archive,0L,1);
	cur-=blocksize;
	/* Seek back to the beginning of this block and
	   start writing there. */

	if(rmtlseek(archive,cur,0)!=cur) {
		/* Lseek failed.  Try a different method */
		msg("Couldn't backspace archive file.  It may be unreadable without -i.");
		/* Replace the first part of the block with nulls */
		if(ar_block->charptr!=output_start)
			bzero(ar_block->charptr,output_start-ar_block->charptr);
		return 2;
	}
	return 3;
}


/*
 * Close the archive file.
 */
close_archive()
{
	int child;
	int status;

	if (time_to_start_writing || !ar_reading)
		flush_archive();
	if(cmd_mode==CMD_DELETE) {
		long pos;

		pos = rmtlseek(archive,0L,1);
#ifndef MSDOS
		(void) ftruncate(archive,(off_t)pos);
#else
		(void)rmtwrite(archive,"",0);
#endif
	}
	if(f_verify)
		verify_volume();
	(void) rmtclose(archive);

#ifndef	MSDOS
	if (childpid) {
		/*
		 * Loop waiting for the right child to die, or for
		 * no more kids.
		 */
		while (((child = wait(&status)) != childpid) && child != -1)
			;

		if (child != -1) {
			switch (TERM_SIGNAL(status)) {
			case 0:
				/* Child voluntarily terminated  -- but why? */
				if (TERM_VALUE(status) == MAGIC_STAT) {
				    if (f_debug) {
					fprintf(stderr,
					    "Tar.gnu: child exit abnormally\n");
				    }
				    exit(EX_SYSTEM);/* Child had trouble */
				}
				if (TERM_VALUE(status) == (SIGPIPE + 128)) {
					/*
					 * /bin/sh returns this if its child
					 * dies with SIGPIPE.  'Sok.
					 */
					break;
				} else if (TERM_VALUE(status))
					msg("child returned status %d",
						TERM_VALUE(status));
			case SIGPIPE:
				break;		/* This is OK. */

			default:
				msg("child died with signal %d%s",
				 TERM_SIGNAL(status),
				 TERM_COREDUMP(status)? " (core dumped)": "");
			}
		}
	}
#endif	/* MSDOS */
}


#ifdef DONTDEF
/*
 * Message management.
 *
 * anno writes a message prefix on stream (eg stdout, stderr).
 *
 * The specified prefix is normally output followed by a colon and a space.
 * However, if other command line options are set, more output can come
 * out, such as the record # within the archive.
 *
 * If the specified prefix is NULL, no output is produced unless the
 * command line option(s) are set.
 *
 * If the third argument is 1, the "saved" record # is used; if 0, the
 * "current" record # is used.
 */
void
anno(stream, prefix, savedp)
	FILE	*stream;
	char	*prefix;
	int	savedp;
{
#	define	MAXANNO	50
	char	buffer[MAXANNO];	/* Holds annorecment */
#	define	ANNOWIDTH 13
	int	space;
	long	offset;
	int	save_e;

	save_e=errno;
	/* Make sure previous output gets out in sequence */
	if (stream == stderr)
		fflush(stdout);
	if (f_sayblock) {
		if (prefix) {
			fputs(prefix, stream);
			putc(' ', stream);
		}
		offset = ar_record - ar_block;
		(void) sprintf(buffer, "rec %d: ",
			savedp?	saved_recno:
				baserec + offset);
		fputs(buffer, stream);
		space = ANNOWIDTH - strlen(buffer);
		if (space > 0) {
			fprintf(stream, "%*s", space, "");
		}
	} else if (prefix) {
		fputs(prefix, stream);
		fputs(": ", stream);
	}
	errno=save_e;
}
#endif

/* We've hit the end of the old volume.  Close it and open the next one */
/* Values for type:  0: writing  1: reading  2: updating */
new_volume(type)
int	type;
{
	/* int	c; */
	char	inbuf[80];
	char	*p;
	static FILE *read_file = 0;
	extern int now_verifying;
	extern char TTY_NAME[];
	char *getenv();

	if(!read_file && !f_run_script_at_end)
		read_file = (archive==0) ? fopen(TTY_NAME, "r") : stdin;

	if(now_verifying)
		return -1;
	if(f_verify)
		verify_volume();
	if(rmtclose(archive)<0) {
		msg_perror("can't close %s",ar_file);
		exit(EX_BADARCH);
	}
	volno++;
	if (f_run_script_at_end)
		system(info_script);
	else for(;;) {
		fprintf(msg_file,"Prepare volume #%d and hit return: ",volno);
		if(fgets(inbuf,sizeof(inbuf),read_file)==0 || inbuf[0]=='\n')
			break;
		switch(inbuf[0]) {
		case '?':
		{
			fprintf(msg_file,"\
 n [name]   Give a new filename for the next (and subsequent) volume(s)\n\
 q          Abort tar\n\
 !          Spawn a subshell\n\
 ?          Print this list\n");
		}
			break;

		case 'q':	/* Quit */
			fprintf(msg_file,"No new volume; exiting.\n");
			if(cmd_mode!=CMD_EXTRACT && cmd_mode!=CMD_LIST && cmd_mode!=CMD_DIFF)
				msg("Warning:  Archive is INCOMPLETE!");
			exit(EX_BADARCH);

		case 'n':	/* Get new file name */
		{
			char *q,*r;
			static char *old_name;

			for(q= &inbuf[1];*q==' ' || *q=='\t';q++)
				;
			for(r=q;*r;r++)
				if(*r=='\n')
					*r='\0';
			if(old_name)
				free(old_name);
			old_name=p=(char *)malloc((unsigned)(strlen(q)+2));
			if(p==0) {
				msg("Can't allocate memory for name");
				exit(EX_SYSTEM);
			}
			(void) strcpy(p,q);
			ar_file=p;
		}
			break;

		case '!':
#ifdef MSDOS
			spawnl(P_WAIT,getenv("COMSPEC"),"-",0);
#else
				/* JF this needs work! */
			switch(fork()) {
			case -1:
				msg_perror("can't fork!");
				break;
			case 0:
				p=getenv("SHELL");
				if(p==0) p="/bin/sh";
				execlp(p,"-sh","-i",0);
				msg_perror("can't exec a shell %s",p);
				_exit(55);
			default:
				wait((union wait *)0);
				break;
			}
#endif
			break;
		}
	}

	if(type==2 || f_verify)
		archive=rmtopen(ar_file,O_RDWR|O_CREAT,0666);
	else if(type==1)
		archive=rmtopen(ar_file,O_RDONLY,0666);
	else if(type==0)
		archive=rmtcreat(ar_file,0666);
	else
		archive= -1;

	if(archive<0) {
		msg_perror("can't open %s",ar_file);
		exit(EX_BADARCH);
	}
#ifdef MSDOS
	setmode(archive,O_BINARY);
#endif
	return 0;
}

/* this is a useless function that takes a buffer returned by wantbytes
   and does nothing with it.  If the function called by wantbytes returns
   an error indicator (non-zero), this function is called for the rest of
   the file.
 */
/* ARGSUSED */
int
no_op(size,data)
int size;
char *data;
{
	return 0;
}

/* Some other routine wants SIZE bytes in the archive.  For each chunk of
   the archive, call FUNC with the size of the chunk, and the address of
   the chunk it can work with.
 */
int
wantbytes(size,func)
long size;
int (*func)();
{
	char *data;
	long	data_size;

	while(size) {
		data = findrec()->charptr;
		if (data == NULL) {	/* Check it... */
			msg("Unexpected EOF on archive file");
			return -1;
		}
		data_size = endofrecs()->charptr - data;
		if(data_size>size)
			data_size=size;
		if((*func)(data_size,data))
			func=no_op;
		userec((union record *)(data + data_size - 1));
		size-=data_size;
	}
	return 0;
}


#if 0
int
rmtread(fd, buf, cnt)
    int fd;
    char *buf;
    int cnt;
{

    printf("rmtread(%d, %x, %d) at %x\n",
	fd, buf, cnt, lseek(fd, 0, 1));

    return read(fd, buf, cnt);
}
#endif
