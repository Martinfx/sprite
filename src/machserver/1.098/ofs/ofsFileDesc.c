/* 
 * ofsFileDesc.c --
 *
 *	Routines to allocate, initialize, and free file descriptors for the
 *  	OFS domain file systems.
 *
 * Copyright 1987 Regents of the University of California
 * All rights reserved.
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/kernel/ofs/RCS/ofsFileDesc.c,v 9.5 91/01/26 15:34:50 mgbaker Exp $ SPRITE (Berkeley)";
#endif not lint

#include <sprite.h>
#include <fs.h>
#include <fsutil.h>
#include <fslcl.h>
#include <fsNameOps.h>
#include <fsio.h>
#include <fsStat.h>
#include <fsdm.h>
#include <ofs.h>
#include <stdio.h>

#define LOCKPTR (&ofsPtr->fileDescLock)

/*
 * Array to provide the ability to set and extract bits out of a bitmap byte.
 */
static unsigned char bitmasks[8] = {0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1};

int	fsdmDescSearchStarts = 0;
int	fsdmDescBytesSearched = 0;

/*
 *----------------------------------------------------------------------
 *
 * OfsFileDescAllocInit --
 *
 *	Initialize the data structure needed for allocation of file
 *	descriptors.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory allocated for the bit map and the bit map is read in.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
OfsFileDescAllocInit(ofsPtr)
    register Ofs_Domain *ofsPtr;
{
    register ReturnStatus	status;

    Sync_LockInitDynamic(&(ofsPtr->fileDescLock), "Fs:fileDescLock");
    /*
     * Allocate the bit map.
     */

    ofsPtr->fileDescBitmap = (unsigned char *) 
	malloc(ofsPtr->headerPtr->fdBitmapBlocks * FS_BLOCK_SIZE);

    /* 
     * Read in the bit map.
     */

    status = OfsDeviceBlockIO(ofsPtr, FS_READ, 
		    ofsPtr->headerPtr->fdBitmapOffset * 4, 
		    ofsPtr->headerPtr->fdBitmapBlocks * 4,
		    (Address) ofsPtr->fileDescBitmap);
    if (status != SUCCESS) {
	printf( "Could not read in OFS file descriptor bit map.\n");
	return(status);
    } else {
	fs_Stats.gen.physBytesRead += FS_BLOCK_SIZE;
    }
    return(SUCCESS);
}

/*
 *----------------------------------------------------------------------
 *
 * OfsWriteBackFileDescBitmap() --
 *
 *	Write the file descriptor bit map out to disk for this domain.
 *
 * Results:
 *	Error if the write failed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ENTRY ReturnStatus
OfsWriteBackFileDescBitmap(ofsPtr)
    register Ofs_Domain *ofsPtr;
{
    register ReturnStatus	status;

    LOCK_MONITOR;

    status = OfsDeviceBlockIO(ofsPtr, FS_WRITE,
		    ofsPtr->headerPtr->fdBitmapOffset * 4, 
		    ofsPtr->headerPtr->fdBitmapBlocks * 4,
		    (Address) ofsPtr->fileDescBitmap);
    if (status != SUCCESS) {
	printf( "Could not write out OFS file desc bit map.\n");
    } else {
	fs_Stats.gen.physBytesWritten += FS_BLOCK_SIZE;
    }

    UNLOCK_MONITOR;
    return(status);
}

/*
 *----------------------------------------------------------------------
 *
 * Ofs_GetNewFileNumber() --
 *
 *	Get a new file number by allocating a free file descriptor
 *	from the file descriptor bitmap.
 *
 * Results:
 *	An error if could not find a free file descriptor.
 *
 * Side effects:
 *	fileNumberPtr is set to the number of the file descriptor allocated.
 *
 *----------------------------------------------------------------------
 */

ENTRY ReturnStatus
Ofs_GetNewFileNumber(domainPtr, dirFileNum, fileNumberPtr)
    Fsdm_Domain 	*domainPtr;	/* Domain to allocate the file 
					 * descriptor out of. */
    int			dirFileNum;	/* File number of the directory that
					   the file is in.  -1 means that
					   this file descriptor is being
					   allocated for a directory. */
    int			*fileNumberPtr; /* Place to return the number of
					   the file descriptor allocated. */
{
    register Ofs_Domain		*ofsPtr = OFS_PTR_FROM_DOMAIN(domainPtr);
    register int 	   	i;
    register int		j;
    int				startByte;
    register unsigned char 	*bitmapPtr;
    register unsigned char 	*bitmaskPtr;
    Boolean		   	found = FALSE;
    int			   	descBytes;

    LOCK_MONITOR;
    fsdmDescSearchStarts++;
    descBytes = ofsPtr->headerPtr->numFileDesc >> 3;
    
    if (dirFileNum == -1) {
	/*
	 * Search linearly from a random starting byte.
	 */
	startByte = ((Fsutil_TimeInSeconds() * 1103515245 + 12345) & 0x7fffffff) % 
			descBytes;
    } else {
	/*
	 * Start search where directory is.
	 */
	startByte = dirFileNum / 8;
    }

    /*
     * Linear search forward the bit map a byte at a time.
     */
    bitmapPtr = &(ofsPtr->fileDescBitmap[startByte]);
    i = startByte;
    do {
	fsdmDescBytesSearched++;
	if (*bitmapPtr != 0xff) {
	    found = TRUE;
	    break;
	}
	i++;
	if (i == descBytes) {
	    i = 0;
	    bitmapPtr = ofsPtr->fileDescBitmap;
	} else {
	    bitmapPtr++;
	}
    } while (i != startByte);

    if (!found) {
	printf( "Out of file descriptors.\n");
	UNLOCK_MONITOR;
	return(FAILURE);
    }

    ofsPtr->summaryInfoPtr->numFreeFileDesc--;
    /*
     * Now find which file descriptor is free within the byte.
     */
    for (j = 0, bitmaskPtr = bitmasks; 
	 j < 8 && (*bitmapPtr & *bitmaskPtr) != 0; 
	 j++, bitmaskPtr++) {
    }
    *fileNumberPtr = i * 8 + j;
    *bitmapPtr |= *bitmaskPtr;

    UNLOCK_MONITOR;

    return(SUCCESS);
}


/*
 *----------------------------------------------------------------------
 *
 * Ofs_FreeFileNumber() --
 *
 *	Free a file number by clearing the corresponding bit the in
 *	file descriptor bit map.
 *
 * Results:
 *	SUCCESS.
 *
 * Side effects:
 *	Bit map modified.
 *
 *----------------------------------------------------------------------
 */

ENTRY ReturnStatus
Ofs_FreeFileNumber(domainPtr, fileNumber)
    register Fsdm_Domain *domainPtr;	/* Domain that the file 
					 * descriptor is in. */
    int			fileNumber; 	/* Number of file descriptor to 
					   free.*/
{
    Ofs_Domain	*ofsPtr = OFS_PTR_FROM_DOMAIN(domainPtr);
    LOCK_MONITOR;
    ofsPtr->summaryInfoPtr->numFreeFileDesc++;
    ofsPtr->fileDescBitmap[fileNumber / 8] &= ~bitmasks[fileNumber & 0x7];
    UNLOCK_MONITOR;
    return SUCCESS;
}


/*
 *----------------------------------------------------------------------
 *
 * Ofs_FileDescInit() --
 *
 *	Initialize a new file descriptor.
 *
 * Results:
 *	An error if could not read the file descriptor from disk.
 *
 * Side effects:
 *	The file decriptor is initialized.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Ofs_FileDescInit(domainPtr, fileNumber, type, permissions, uid, gid, fileDescPtr)
    register Fsdm_Domain *domainPtr;	/* Domain of the file */
    int			fileNumber; 	/* Number of file descriptor */
    int			type;		/* Type of the file */
    int			permissions;	/* Permission bits for the file */
    int			uid;		/* Owner ID for the file */
    int			gid;		/* Group ID for the file */
    register Fsdm_FileDescriptor	*fileDescPtr;	/* File descriptor structure to
					   initialize. */
{
    ReturnStatus status;
    register int index;
 
    /*
     * Fetch the file descriptor and do rudimentation consistency checks.
     * This also gets its block into the cache which will happen sooner
     * or later anyway.
     */
    status = Ofs_FileDescFetch(domainPtr, fileNumber, fileDescPtr);
    if (status != SUCCESS) {
	return(status);
    }
    if (fileDescPtr->flags & FSDM_FD_ALLOC) {
	printf( "Ofs_FileDescInit fetched non-free file desc\n");
	return(FS_FILE_EXISTS);
    }
    fileDescPtr->magic = FSDM_FD_MAGIC;
    fileDescPtr->flags = FSDM_FD_ALLOC|FSDM_FD_DIRTY;
    fileDescPtr->fileType = type;
    fileDescPtr->permissions = permissions;
    fileDescPtr->uid = uid;
    fileDescPtr->gid = gid;
    fileDescPtr->lastByte = -1;
    fileDescPtr->firstByte = -1;
    fileDescPtr->userType = FS_USER_TYPE_UNDEFINED;
    fileDescPtr->numLinks = 1;
    fileDescPtr->numKbytes = 0;
    /*
     * Give this new file a new version number.  The increment is by 2 to
     * ensure that a client invalidates any cache blocks associated with
     * the previous incarnation of the file.  Remember that when a client
     * opens for writing a version number 1 greater means that its old
     * cache blocks are still ok, and also remember that clients with
     * clean blocks are not told when a file is deleted.
     */
    fileDescPtr->version += 2;

    /*
     * Clear out device info.  It is set up properly by the make-device routine.
     */
    fileDescPtr->devServerID = -1;
    fileDescPtr->devType = -1;
    fileDescPtr->devUnit = -1;

    /*
     * Set the time stamps.  These times should come from the client.
     */
    fileDescPtr->createTime = Fsutil_TimeInSeconds();
    fileDescPtr->accessTime = fileDescPtr->createTime;
    fileDescPtr->descModifyTime = fileDescPtr->createTime;
    fileDescPtr->dataModifyTime = fileDescPtr->createTime;

    for (index = 0; index < FSDM_NUM_DIRECT_BLOCKS ; index++) {
	fileDescPtr->direct[index] = FSDM_NIL_INDEX;
    }
    for (index = 0; index < FSDM_NUM_INDIRECT_BLOCKS ; index++) {
	fileDescPtr->indirect[index] = FSDM_NIL_INDEX;
    }
    return(SUCCESS);
}

/*
 *----------------------------------------------------------------------
 *
 * Ofs_FileDescFetch() --
 *
 *	Fetch the given file descriptor from disk and store it into
 *	*fileDescPtr.
 *
 * Results:
 *	An error if could not read the file descriptor from disk.
 *
 * Side effects:
 *	*fileDescPtr is modified.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Ofs_FileDescFetch(domainPtr, fileNumber, fileDescPtr)
     Fsdm_Domain 	*domainPtr;	/* Domain to fetch the file 
					 * descriptor from. */
    register int	fileNumber; 	/* Number of file descriptor to 
					   fetch.*/
    Fsdm_FileDescriptor	*fileDescPtr;	/* File descriptor structure to
					   initialize. */
{
    register Ofs_Domain	      *ofsPtr = OFS_PTR_FROM_DOMAIN(domainPtr);
    register ReturnStatus     status;
    register Ofs_DomainHeader *headerPtr;
    register int 	    blockNum;
    int 		    offset;
    Fscache_Block	    *blockPtr;
    Boolean		    found;
#ifdef SOSP91
    Boolean		isForeign = FALSE;	/* Due to migration? */
#endif SOSP91

    headerPtr = ofsPtr->headerPtr;
    blockNum = headerPtr->fileDescOffset + fileNumber / FSDM_FILE_DESC_PER_BLOCK;
    offset = (fileNumber & (FSDM_FILE_DESC_PER_BLOCK - 1)) *
		FSDM_MAX_FILE_DESC_SIZE;

#ifdef SOSP91
    if (proc_RunningProcesses[0] != (Proc_ControlBlock *) NIL) {
	if ((proc_RunningProcesses[0]->state == PROC_MIGRATED) ||
		(proc_RunningProcesses[0]->genFlags &
		(PROC_FOREIGN | PROC_MIGRATING))) {
	    isForeign = TRUE;
	}
    }
#endif SOSP91
    fs_Stats.blockCache.fileDescReads++;
#ifdef SOSP91
    if (isForeign) {
	fs_SospMigStats.blockCache.fileDescReads++;
    }
#endif SOSP91
    Fscache_FetchBlock(&ofsPtr->physHandle.cacheInfo, blockNum, 
		      FSCACHE_DESC_BLOCK, &blockPtr, &found);
    if (!found) {
	status = OfsDeviceBlockIO(ofsPtr, FS_READ, 
			   blockNum * FS_FRAGMENTS_PER_BLOCK,
			   FS_FRAGMENTS_PER_BLOCK, blockPtr->blockAddr);
	if (status != SUCCESS) {
	    printf( "Could not read in file descriptor\n");
	    Fscache_UnlockBlock(blockPtr, 0, -1, 0, FSCACHE_DELETE_BLOCK);
	    return(status);
	} else {
	    fs_Stats.gen.physBytesRead += FS_BLOCK_SIZE;
	}
    } else {
	fs_Stats.blockCache.fileDescReadHits++;
#ifdef SOSP91
	if (isForeign) {
	    fs_SospMigStats.blockCache.fileDescReadHits++;
	}
#endif SOSP91
    }
    bcopy(blockPtr->blockAddr + offset, (Address) fileDescPtr,
	sizeof(Fsdm_FileDescriptor));
    Fscache_UnlockBlock(blockPtr, 0, blockNum * FS_FRAGMENTS_PER_BLOCK, 
   			 FS_BLOCK_SIZE, 0);

    if (fileDescPtr->magic != FSDM_FD_MAGIC) {
	printf( "Fsdm_FileDescFetch found junky file desc\n");
	return(FAILURE);
    } else {
	return(SUCCESS);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ofs_FileDescStore() --
 *
 *	Store the given file descriptor back into the file system block
 *	where it came from.  This involves putting the block back into
 *	the cache.
 *
 * Results:
 *	An error if could not read the file descriptor from disk.
 *
 * Side effects:
 *	Cache block is modified.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Ofs_FileDescStore(domainPtr, handlePtr, fileNumber, fileDescPtr, forceOut)
    register Fsdm_Domain *domainPtr;	/* Domain to store the file 
					 * descriptor into. */
    Fsio_FileIOHandle	*handlePtr;
    int			fileNumber; 	/* Number of file descriptor to 
					   store.*/
    Fsdm_FileDescriptor	*fileDescPtr;	 /* File descriptor to store. */
    Boolean		forceOut;  /* Force the change to disk. */
{
    Ofs_Domain	*ofsPtr = OFS_PTR_FROM_DOMAIN(domainPtr);
    register ReturnStatus   status = SUCCESS;
    register Ofs_DomainHeader *headerPtr;
    register int 	    blockNum;
    int 		    offset;
    Fscache_Block	    *blockPtr;
    Boolean		    found;
#ifdef SOSP91
    Boolean		isForeign = FALSE;	/* Due to migration? */
#endif SOSP91

    headerPtr = ofsPtr->headerPtr;
    blockNum = headerPtr->fileDescOffset + fileNumber / FSDM_FILE_DESC_PER_BLOCK;
    offset = (fileNumber & (FSDM_FILE_DESC_PER_BLOCK - 1)) *
		FSDM_MAX_FILE_DESC_SIZE;

#ifdef SOSP91
    if (proc_RunningProcesses[0] != (Proc_ControlBlock *) NIL) {
	if ((proc_RunningProcesses[0]->state == PROC_MIGRATED) ||
		(proc_RunningProcesses[0]->genFlags &
		(PROC_FOREIGN | PROC_MIGRATING))) {
	    isForeign = TRUE;
	}
    }
#endif SOSP91
    fs_Stats.blockCache.fileDescWrites++;
#ifdef SOSP91
    if (isForeign) {
	fs_SospMigStats.blockCache.fileDescWrites++;
    }
#endif SOSP91
    Fscache_FetchBlock(&ofsPtr->physHandle.cacheInfo, blockNum, 
		      (int)(FSCACHE_IO_IN_PROGRESS | FSCACHE_DESC_BLOCK),
		      &blockPtr, &found);
    if (!found) {
	status = OfsDeviceBlockIO(ofsPtr, FS_READ, 
			   blockNum * FS_FRAGMENTS_PER_BLOCK,
			   FS_FRAGMENTS_PER_BLOCK, blockPtr->blockAddr);
	if (status != SUCCESS) {
	    printf( "Could not read in file descriptor\n");
	    Fscache_UnlockBlock(blockPtr, 0, blockNum * FS_FRAGMENTS_PER_BLOCK,
				FS_BLOCK_SIZE, FSCACHE_DELETE_BLOCK);
	    return(status);
	} else {
	    fs_Stats.gen.physBytesWritten += FS_BLOCK_SIZE;
	}
    } else {
	fs_Stats.blockCache.fileDescWriteHits++;
#ifdef SOSP91
	if (isForeign) {
	    fs_SospMigStats.blockCache.fileDescWriteHits++;
	}
#endif SOSP91
    }
    fileDescPtr->flags &= ~FSDM_FD_DIRTY;
    bcopy((Address) fileDescPtr, blockPtr->blockAddr + offset, sizeof(Fsdm_FileDescriptor));
    /*
     * Put the block back into the cache setting the modify time to 1 which
     * will guarantee that the next time the cache is written back this block
     * is written back as well.
     */
    Fscache_UnlockBlock(blockPtr, 1, blockNum * FS_FRAGMENTS_PER_BLOCK,
			FS_BLOCK_SIZE, 0);
    if ((status == SUCCESS) && forceOut) { 
	int	blocksSkipped;
#ifdef SOSP91
	ofsPtr->physHandle.cacheInfo.flags |= FSCACHE_DESC;
#endif SOSP91
	status = Fscache_FileWriteBack(&ofsPtr->physHandle.cacheInfo,
		    blockNum, blockNum, FSCACHE_FILE_WB_WAIT, &blocksSkipped);
	if (status != SUCCESS) {
	    printf("Ofs_FileDescStore: Couldn't write back desc <%d,%d>\n",
		    domainPtr->domainNumber, fileNumber);
	}
    }

    return(status);
}

/*
 *----------------------------------------------------------------------
 *
 * Ofs_FileDescSync() --
 *
 *	Sync any cached file descriptor to disk.
 *
 * Results:
 *	An error if could not read the file descriptor from disk.
 *
 * Side effects:
 *	Cache block is modified.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Ofs_FileDescSync(domainPtr, fileNumber)
    register Fsdm_Domain 	*domainPtr;	/* Domain to store the file 
					 * descriptor into. */
    int			fileNumber; 	/* Number of file descriptor to 
					   store.*/
{
    Ofs_Domain	*ofsPtr = OFS_PTR_FROM_DOMAIN(domainPtr);
    ReturnStatus status;
    int	blocksSkipped;
    int	blockNum;

    blockNum = ofsPtr->headerPtr->fileDescOffset + 
				fileNumber / FSDM_FILE_DESC_PER_BLOCK;
#ifdef SOSP91
    ofsPtr->physHandle.cacheInfo.flags |= FSCACHE_DESC;
#endif SOSP91
    status = Fscache_FileWriteBack(&ofsPtr->physHandle.cacheInfo,
		blockNum, blockNum, FSCACHE_FILE_WB_WAIT, &blocksSkipped);
    if (status != SUCCESS) {
	printf("Ofs_FileDescSync: Couldn't sync back desc <%d,%d>\n",
		domainPtr->domainNumber, fileNumber);
    }
    return status;

}
