/* vmServer.c -
 *
 *     	This file contains routines that read and write pages to/from the page
 *	server and file server.
 *
 * Copyright (C) 1985 Regents of the University of California
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Header: /cdrom/src/kernel/Cvsroot/kernel/vm/vmServer.c,v 9.8 92/04/10 16:46:08 kupfer Exp $ SPRITE (Berkeley)";
#endif not lint

#include <sprite.h>
#include <vm.h>
#include <vmInt.h>
#include <vmSwapDir.h>
#include <lock.h>
#include <status.h>
#include <sched.h>
#include <sync.h>
#include <dbg.h>
#include <list.h>
#include <string.h>
#include <stdlib.h>
#include <proc.h>
#include <stdio.h>

Boolean	vmUseFSReadAhead = TRUE;
extern	Boolean	vm_NoStickySegments;
char		*sprintf();

/*
 * Condition to wait on when want to do a swap file operation but someone
 * is already doing one.  This is used for synchronization of opening
 * of swap files.  It is possible that the open of the swap file could
 * happen more than once if the open is not synchronized.
 */
Sync_Condition	swapFileCondition;

void	Fscache_BlocksUnneeded();


/*
 *----------------------------------------------------------------------
 *
 * VmSwapFileLock --
 *
 *	Set a lock on the swap file for the given segment.  If the lock
 *	is already set then wait.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Flags field in segment table entry is modified to make the segment
 *	locked.
 *----------------------------------------------------------------------
 */

ENTRY void
VmSwapFileLock(segPtr)
    register	Vm_Segment	*segPtr;
{
    LOCK_MONITOR;

    while (segPtr->flags & VM_SWAP_FILE_LOCKED) {
	(void) Sync_Wait(&swapFileCondition, FALSE);
    }
    segPtr->flags |= VM_SWAP_FILE_LOCKED;

    UNLOCK_MONITOR;
}


/*
 *----------------------------------------------------------------------
 *
 * VmSwapFileUnlock --
 *
 *	Clear the lock on the swap file for the given segment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Flags field in segment table entry is modified to make the segment
 *	unlocked and anyone waiting for the segment is awakened.
 *----------------------------------------------------------------------
 */

ENTRY void
VmSwapFileUnlock(segPtr)
    register	Vm_Segment	*segPtr;
{
    LOCK_MONITOR;

    segPtr->flags &= ~VM_SWAP_FILE_LOCKED;
    Sync_Broadcast(&swapFileCondition);

    UNLOCK_MONITOR;
}

/*
 *----------------------------------------------------------------------
 *
 * VmPageServerRead --
 *
 *	Read the given page from the swap file into the given page frame.
 *	This routine will panic if the swap file does not exist.
 *
 *	NOTE: It is assumed that the page frame that is to be written into
 *	      cannot be given to another segment.
 *
 * Results:
 *	SUCCESS if the page server could be read from or an error if either
 *	a swap file could not be opened or the page server could not be
 *	read from the swap file.
 *
 * Side effects:
 *	The hardware page is written into.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
VmPageServerRead(virtAddrPtr, pageFrame)
    Vm_VirtAddr			*virtAddrPtr;
    unsigned	int		pageFrame;
{
    register	int		mappedAddr;
    register	Vm_Segment	*segPtr;
    int				status;
    int				pageToRead;

    segPtr = virtAddrPtr->segPtr;
    if (!(segPtr->flags & VM_SWAP_FILE_OPENED)) {
	printf("VmPageServerRead: swap file %s doesn't exist.\n",
	       (segPtr->swapFileName != (char *)NIL
		? segPtr->swapFileName : "???"));
	return(VM_SWAP_ERROR);
    }

    if (segPtr->type == VM_STACK) {
	pageToRead = mach_LastUserStackPage - virtAddrPtr->page;
    } else if (segPtr->type == VM_SHARED) {
	pageToRead= virtAddrPtr->page - segOffset(virtAddrPtr) +
		(virtAddrPtr->sharedPtr->fileAddr>>vmPageShift);
    } else {
	pageToRead = virtAddrPtr->page - segPtr->offset;
    }

    /*
     * Map the page into the kernel's address space and fill it from the
     * file server.
     */
    mappedAddr = (int) VmMapPage(pageFrame);
    status = Fs_PageRead(segPtr->swapFilePtr, (Address) mappedAddr,
			 pageToRead << vmPageShift, vm_PageSize, FS_SWAP_PAGE);
    VmUnmapPage((Address) mappedAddr);

    return(status);
}



/*
 *----------------------------------------------------------------------
 *
 * VmPageServerWrite --
 *
 *	Write the given page frame to the swap file.  If the swap file is
 *	not open yet then it will be open.
 *
 *	NOTE: It is assumed that the page frame that is to be read from
 *	      cannot be given to another segment.
 *
 * Results:
 *	SUCCESS if the page server could be written to or an error if either
 *	a swap file could not be opened or the page server could not be
 *	written to.
 *
 * Side effects:
 *	If no swap file exists, then one is created.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
VmPageServerWrite(virtAddrPtr, pageFrame, toDisk)
    Vm_VirtAddr		*virtAddrPtr;
    unsigned int	pageFrame;
    Boolean		toDisk;
{
    register	int		mappedAddr;
    register	Vm_Segment	*segPtr;
    ReturnStatus		status;
    int				pageToWrite;

    vmStat.pagesWritten++;

    segPtr = virtAddrPtr->segPtr;

    /*
     * Lock the swap file while opening it so that we don't have more than
     * one swap file open at a time.
     */
    VmSwapFileLock(segPtr);
    if (!(segPtr->flags & VM_SWAP_FILE_OPENED)) {
	status = VmOpenSwapFile(segPtr);
	if (status != SUCCESS) {
	    VmSwapFileUnlock(segPtr);
	    return(status);
	}
    }
    VmSwapFileUnlock(segPtr);

    if (segPtr->type == VM_STACK) {
	pageToWrite = mach_LastUserStackPage - virtAddrPtr->page;
    } else {
	pageToWrite = virtAddrPtr->page - segOffset(virtAddrPtr);
    }

    /*
     * Map the page into the kernel's address space and write it out.
     */
    VmMach_FlushPage(virtAddrPtr, FALSE);
    mappedAddr = (int) VmMapPage(pageFrame);
    status = Fs_PageWrite(segPtr->swapFilePtr, (Address) mappedAddr,
			  pageToWrite << vmPageShift, vm_PageSize, toDisk);
    VmUnmapPage((Address) mappedAddr);

    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * VmCopySwapSpace --
 *
 *	Copy the swap space for all pages that have been written out to swap
 *	space for the source segment into the destination segments swap space.
 *
 * Results:
 *	Error if swap file could not be opened, read or written.  Otherwise
 *	SUCCESS is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
VmCopySwapSpace(srcSegPtr, destSegPtr)
    register	Vm_Segment	*srcSegPtr;
    register	Vm_Segment	*destSegPtr;
{
    register	int	page;
    register	Vm_PTE	*ptePtr;
    ReturnStatus	status = SUCCESS;
    register	int	i;

    VmSwapFileLock(srcSegPtr);
    if (!(srcSegPtr->flags & VM_SWAP_FILE_OPENED)) {
	VmSwapFileUnlock(srcSegPtr);
	return(SUCCESS);
    }
    VmSwapFileUnlock(srcSegPtr);

    VmSwapFileLock(destSegPtr);
    if (!(destSegPtr->flags & VM_SWAP_FILE_OPENED)) {
	status = VmOpenSwapFile(destSegPtr);
	if (status != SUCCESS) {
	    VmSwapFileUnlock(destSegPtr);
	    return(status);
	}
    }
    VmSwapFileUnlock(destSegPtr);

    if (destSegPtr->type == VM_STACK) {
	page = destSegPtr->numPages - 1;
	ptePtr = VmGetPTEPtr(destSegPtr, mach_LastUserStackPage - 
			                 destSegPtr->numPages + 1);
    } else {
	page = 0;
    	ptePtr = destSegPtr->ptPtr;
    }

    for (i = 0; i < destSegPtr->numPages; i++, VmIncPTEPtr(ptePtr, 1)) {

	if (*ptePtr & VM_IN_PROGRESS_BIT) {
	    *ptePtr &= ~VM_IN_PROGRESS_BIT;
	    /*
	     * The page is on the swap file and not in memory.  Need to copy
	     * the page in the file.
	     */
	    vmStat.swapPagesCopied++;
	    status = Fs_PageCopy(srcSegPtr->swapFilePtr, 
				destSegPtr->swapFilePtr, 
				page << vmPageShift, vm_PageSize);
	    if (status != SUCCESS) {
		break;
	    }
	}
	if (destSegPtr->type == VM_STACK) {
	    page--;
	} else {
	    page++;
	}
    }

    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * VmFileServerRead --
 *
 *	Read the given page from the file server into the given page frame.
 *
 *	NOTE: It is assumed that the page frame that is to be written into
 *	      cannot be given to another segment.
 *
 * Results:
 *	Error if file server could not be read from, SUCCESS otherwise.
 *
 * Side effects:
 *	The hardware page is written into.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
VmFileServerRead(virtAddrPtr, pageFrame)
    Vm_VirtAddr		*virtAddrPtr;
    unsigned int	pageFrame;
{
    register	int		mappedAddr;
    register	Vm_Segment	*segPtr;
    int				length;
    int				status;
    int				offset;

    segPtr = virtAddrPtr->segPtr;

    /*
     * Map the page frame into the kernel's address space.
     */
    mappedAddr = (int) VmMapPage(pageFrame);
    /*
     * The address to read is just the page offset into the segment
     * ((page - offset) << vmPageShift) plus the offset of this segment into
     * the file (fileAddr).
     * (We have to use the right offset for shared memory.
     */
    length = vm_PageSize;
    if (segPtr->type == VM_SHARED) {
	if (virtAddrPtr->sharedPtr == (Vm_SegProcList *)NIL) {
	    printf("*** NIL sharedPtr in VmFileServerRead\n");
	    return FAILURE;
	}
	offset = ((virtAddrPtr->page - segOffset(virtAddrPtr)) << vmPageShift)
		+ virtAddrPtr->sharedPtr->fileAddr;
    } else {
	offset = ((virtAddrPtr->page - segOffset(virtAddrPtr)) << vmPageShift)
		+ segPtr->fileAddr;
    }
    if (vmPrefetch || !vmUseFSReadAhead) {
	/*
	 * If we are using prefetch then do the reads ourselves.
	 */
	if (segPtr->type == VM_CODE && !vm_NoStickySegments) {
	    status = Fs_PageRead(segPtr->filePtr, (Address)mappedAddr, offset,
				 length, FS_CODE_PAGE);
	} else {
	    status = Fs_PageRead(segPtr->filePtr, (Address)mappedAddr, offset,
				 length, FS_HEAP_PAGE);
	}
    } else {
	/*
	 * No prefetch so use the file system full blown mechanism so
	 * that we can take advantage of its read ahead.
	 */
	status = Fs_Read(segPtr->filePtr, (Address) mappedAddr, offset,
			 &length);
	if (status == SUCCESS && !vm_NoStickySegments && 
	    segPtr->type == VM_CODE) {
	    /*
	     * Tell the file system that we just read some file system blocks
	     * into virtual memory.
	     */
	    Fscache_BlocksUnneeded(segPtr->filePtr, offset, vm_PageSize, TRUE);
	}
    }
    VmUnmapPage((Address) mappedAddr);
    if (status != SUCCESS) {
	printf("%s VmFileServerRead: Error %x from Fs_Read or Fs_PageRead\n",
		"Warning:", status);
	return(status);
    }

    return(SUCCESS);
}



/*
 *----------------------------------------------------------------------
 *
 * VmCopySwapPage --
 *
 *	Copy the swap page from the source segment's swap file to the
 *	destination.
 *
 * Results:
 *	Error if swap file could not be opened, read or written.  Otherwise
 *	SUCCESS is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
VmCopySwapPage(srcSegPtr, virtPage, destSegPtr)
    register	Vm_Segment	*srcSegPtr;	/* Source for swap file. */
    int				virtPage;	/* Virtual page to copy. */
    register	Vm_Segment	*destSegPtr;	/* Destination swap file. */
{
    int			pageToCopy;
    ReturnStatus	status;

    /*
     * Lock the swap file while opening it so that we don't have more than
     * one swap file open at a time.
     */
    VmSwapFileLock(destSegPtr);
    if (!(destSegPtr->flags & VM_SWAP_FILE_OPENED)) {
	status = VmOpenSwapFile(destSegPtr);
	if (status != SUCCESS) {
	    VmSwapFileUnlock(destSegPtr);
	    return(status);
	}
    }
    VmSwapFileUnlock(destSegPtr);

    vmStat.swapPagesCopied++;
    if (destSegPtr->type == VM_STACK) {
	pageToCopy = mach_LastUserStackPage - virtPage;
    } else {
	pageToCopy = virtPage - destSegPtr->offset;
    }

    status = Fs_PageCopy(srcSegPtr->swapFilePtr, 
			 destSegPtr->swapFilePtr, 
			 pageToCopy << vmPageShift, vm_PageSize);

    return(status);
}
