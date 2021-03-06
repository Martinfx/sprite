/* 
 * malloc.c --
 *
 *	Source code for the "malloc" library procedure.  See memInt.h
 *	for overall information about how the allocator works.
 *
 * Copyright 1985, 1988 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/lib/c/stdlib/RCS/malloc.c,v 1.6 88/08/20 21:09:18 ouster Exp $ SPRITE (Berkeley)";
#endif not lint

#include "stdlib.h"
#include "memInt.h"


/*
 * ----------------------------------------------------------------------------
 *
 * malloc --
 *
 *	This procedure allocates a chunk of memory.
 *
 * Results:
 *	The return value is a pointer to an area of at least bytesNeeded
 *	bytes of free storage.  If no memory is available, then this
 *	procedure will never return:  the MemChunkAlloc procedure 
 *	determines what will happen.
 *
 * Side effects:
 *	The returned block is marked as allocated and will not be
 *	allocated to anyone else until it is returned with a call
 *	to free.
 *
 * ----------------------------------------------------------------------------
 */

ENTRY Address
malloc(bytesNeeded)
    register unsigned bytesNeeded;	/* How many bytes to allocate. */
{
    int			thisBlockSize, admin;
    register Address	result;
    Address		newRegion;
    int			regionSize;
    int			origSize;
    register int	index;

    LOCK_MONITOR;

#ifdef MEM_TRACE
    mem_NumAllocs++;
#endif

    if (!memInitialized) {
	MemInit();
    }

    origSize = bytesNeeded;

    /* 
     * Handle binned objects quickly, if possible.
     */

    bytesNeeded = BYTES_TO_BLOCKSIZE(bytesNeeded);
    index = BLOCKSIZE_TO_INDEX(bytesNeeded);
    if (bytesNeeded <= BIN_SIZE) {
	result = memFreeLists[index];
	if (result == NOBIN) {
	    goto largeBlockAllocator;
	}
	if (result == (Address) NULL) {

	    /*
	     * There aren't currently any free objects of this size.
	     * Call the client's function to obtain another region
	     * from the system.  While we're at it, get a whole bunch
	     * of objects of this size once and put all but one back
	     * on the free list.
	     */

	    regionSize = BLOCKS_AT_ONCE * bytesNeeded;
	    newRegion = MemChunkAlloc(regionSize);

	    while (regionSize >= bytesNeeded) {
		SET_ADMIN(newRegion, memFreeLists[index]);
		memFreeLists[index] = newRegion;
		mem_NumBlocks[index] += 1;
		newRegion += bytesNeeded;
		regionSize -= bytesNeeded;
	    }
	    result = memFreeLists[index];
	}

	memFreeLists[index] = (Address) GET_ADMIN(result);
	SET_ADMIN(result, MARK_DUMMY(bytesNeeded));

#ifdef MEM_TRACE
	mem_NumBinnedAllocs[index] += 1;
	SET_PC(result);
	SET_ORIG_SIZE(result, origSize);
	MemDoTrace(TRUE, result, (Address)NULL, bytesNeeded);
#endif MEM_TRACE

	UNLOCK_MONITOR;
	return(result+sizeof(AdminInfo));
    }

    /*
     * This is a large object.  Step circularly through the blocks
     * in the list, looking for one that's large enough to hold
     * what's needed.
     */

largeBlockAllocator:
    if (origSize == 0) {
	return (Address) NULL;
    }
#ifdef MEM_TRACE
    mem_NumLargeAllocs += 1;
#endif
    result = memCurrentPtr;
    while (TRUE) {
	Address nextPtr;

#ifdef MEM_TRACE
	mem_NumLargeLoops += 1;
#endif
	admin = GET_ADMIN(result);
	thisBlockSize = SIZE(admin);
	nextPtr = result+thisBlockSize;
	if (!IS_IN_USE(admin)) {
	
	    /*
	     * Several blocks in a row could have been freed since the last
	     * time we were here.  If so, merge them together.
	     */

	    while (!IS_IN_USE(GET_ADMIN(nextPtr))) {
		thisBlockSize += SIZE(GET_ADMIN(nextPtr));
		admin = MARK_FREE(thisBlockSize);
		SET_ADMIN(result, admin);
		nextPtr = result+thisBlockSize;
	    }
	    if (thisBlockSize >= bytesNeeded) {
		break;
	    }
	    if (thisBlockSize > memLargestFree) {
		memLargestFree = thisBlockSize;
	    }
	}

	/*
	 * This block won't do the job.  Go on to the next block.
	 */

	if (nextPtr != memLast) {
	    result = nextPtr;
	    continue;
	}

	/*
	 * End of the list.  If there's any chance that there's
	 * enough free storage in the list to satisfy the request,
	 * then go back to the beginning of the list and start
	 * again.
	 */

	if ((memLargestFree + memBytesFreed) > bytesNeeded) {
	    memLargestFree = 0;
	    memBytesFreed = 0;
	    result = memFirst;
	    continue;
	}

	/*
	 * Not enough free space in the list.  Allocate a new region
	 * of memory and add it to the list.
	 */

	if (bytesNeeded < MIN_REGION_SIZE) {
	    regionSize = MIN_REGION_SIZE;
	} else {
	    regionSize = bytesNeeded + sizeof(AdminInfo);
	}
	newRegion = MemChunkAlloc(regionSize);
	mem_NumLargeBytes += regionSize;

	/*
	 * If the new region immediately follows the end of the previous
	 * region, merge them together.  At this point result always
	 * points to a block of memory adjacent to and preceding the block
	 * of memory pointed to by memLast (memLast == nextPtr).  Thus it
	 * may be possible to merge the new region with both the region
	 * at result and the one at nextPtr.
	 */

	if (newRegion == (nextPtr+sizeof(AdminInfo))) {
	    newRegion = nextPtr;
	    regionSize += sizeof(AdminInfo);
	    if (!IS_IN_USE(admin)) {
		newRegion = result;
		regionSize += SIZE(admin);
	    }
	} else {
	    SET_ADMIN(nextPtr, MARK_DUMMY(newRegion - nextPtr));
	}

	/*
	 * Create a dummy block at the end of the new region, which links
	 * to "last".
	 */
	
	SET_ADMIN(newRegion, MARK_FREE(regionSize - sizeof(AdminInfo)));
	memLast = newRegion + regionSize - sizeof(AdminInfo);
	SET_ADMIN(memLast, MARK_DUMMY(0));

	/*
	 * Continue scanning the list (try result again in case it
	 * merged with the new region).
	 */
    }

    /*
     * At this point we've got a block that's large enough.  If it's
     * larger than needed for the object, put the rest back on the
     * free list (note: even if the remnant is smaller than the smallest
     * large object, which means it'll be used by itself, we put it back
     * on the list so it can merge with either this block or the next,
     * whichever gets freed first).
     */

    if (thisBlockSize < (bytesNeeded + sizeof(AdminInfo))) {
	SET_ADMIN(result, MARK_IN_USE(admin));
    } else {
	SET_ADMIN(result+bytesNeeded, MARK_FREE(thisBlockSize-bytesNeeded));
	SET_ADMIN(result, MARK_IN_USE(bytesNeeded));
    }

#ifdef MEM_TRACE
    SET_PC(result);
    SET_ORIG_SIZE(result, origSize);
    MemDoTrace(TRUE, result, (Address) NULL, bytesNeeded);
#endif MEM_TRACE
    memCurrentPtr = result;

    UNLOCK_MONITOR;
    return(result+sizeof(AdminInfo));
}
