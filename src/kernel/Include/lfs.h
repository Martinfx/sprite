/*
 * lfs.h --
 *
 *	Declarations of data structures and routines of the LFS file system
 *	exported to the rest of the Sprite file system. 
 *
 * Copyright 1989 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * $Header: /sprite/src/kernel/Cvsroot/kernel/lfs/lfs.h,v 1.5 92/03/06 11:56:46 mgbaker Exp $ SPRITE (Berkeley)
 */

#ifndef _LFS
#define _LFS

#include <sprite.h>
#include <fsdm.h>

/*
 * Flags controlling file system.
 * LFS_CONTROL_CLEANALL - When cleaning, clean all dirty segments.
 */
#define	LFS_CONTROL_CLEANALL	0x1

/*
 * These flags are just for the ASPLOS measurements, to tag write operations
 * if they are a result of an fsync.  They shouldn't be flags in controlFlags,
 * but it won't interfere with anything there, and this way I don't have
 * to change the size of the Lfs structure.
 *
 * Can be removed after ASPLOS measurements.
 * 			Mary	2/14/92
 */
#define	LFS_FILE_FSYNCED	0x2
#define	LFS_FSYNC_IN_PROGRESS	0x4

/* Descriptor management routines. */

extern ReturnStatus Lfs_GetNewFileNumber _ARGS_((Fsdm_Domain *domainPtr, 
		int dirFileNum, int *fileNumberPtr));
extern ReturnStatus Lfs_FreeFileNumber _ARGS_((Fsdm_Domain *domainPtr, 
		int fileNumber));
extern ReturnStatus Lfs_FileDescStore _ARGS_((register Fsdm_Domain *domainPtr,
		Fsio_FileIOHandle *handlePtr, int fileNumber, 
		Fsdm_FileDescriptor *fileDescPtr, Boolean forceOut));
extern ReturnStatus Lfs_FileDescFetch _ARGS_((Fsdm_Domain *domainPtr, 
		int fileNumber, Fsdm_FileDescriptor *fileDescPtr));
extern ReturnStatus Lfs_FileDescInit _ARGS_((Fsdm_Domain *domainPtr, 
		int fileNumber, int type, int permissions, int uid, int gid,
		Fsdm_FileDescriptor *fileDescPtr));
/*
 * Startup and shutdown routines. 
 */
extern ReturnStatus Lfs_AttachDisk _ARGS_((Fs_Device *devicePtr, 
			char *localName, int flags, int *domainNumPtr));
extern ReturnStatus Lfs_DetachDisk _ARGS_((Fsdm_Domain *domainPtr));
extern ReturnStatus Lfs_DomainWriteBack _ARGS_((Fsdm_Domain *domainPtr, 
			Boolean shutdown));
extern ReturnStatus Lfs_RereadSummaryInfo _ARGS_((Fsdm_Domain *domainPtr));
extern ReturnStatus Lfs_DomainInfo _ARGS_((Fsdm_Domain *domainPtr, 
			Fs_DomainInfo *domainInfoPtr));
/*
 * File I/O and allocate routines. 
 */
extern ReturnStatus Lfs_FileBlockRead _ARGS_((Fsdm_Domain *domainPtr, 
			register Fsio_FileIOHandle *handlePtr, 
			Fscache_Block *blockPtr));
extern ReturnStatus Lfs_FileBlockWrite _ARGS_((Fsdm_Domain *domainPtr, 
			Fsio_FileIOHandle *handlePtr, Fscache_Block *blockPtr));
extern ReturnStatus Lfs_BlockAllocate _ARGS_((Fsdm_Domain *domainPtr, 
			register Fsio_FileIOHandle *handlePtr, int offset, 
			int numBytes, int flags, int *blockAddrPtr, 
			Boolean *newBlockPtr));
extern ReturnStatus Lfs_FileTrunc _ARGS_((Fsdm_Domain *domainPtr, 
			Fsio_FileIOHandle *handlePtr, 
			int size, Boolean delete));

/*
 * Directory routines.
 */
extern ClientData Lfs_DirOpStart _ARGS_((Fsdm_Domain *domainPtr, int opFlags, 
			char *name, int nameLen, int fileNumber, 
			Fsdm_FileDescriptor *fileDescPtr, int dirFileNumber, 
			int dirOffset, Fsdm_FileDescriptor *dirFileDescPtr));
extern void Lfs_DirOpEnd _ARGS_((Fsdm_Domain *domainPtr, ClientData clientData,
			ReturnStatus status, int opFlags, char *name, 
			int nameLen, int fileNumber, 
			Fsdm_FileDescriptor *fileDescPtr, int dirFileNumber, 
			int dirOffset, Fsdm_FileDescriptor *dirFileDescPtr));


extern void Lfs_Init _ARGS_((void));
extern ReturnStatus Lfs_Command _ARGS_((int command, int bufSize, 
				       Address buffer));

#endif /* _LFS */

