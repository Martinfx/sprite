/*
 * vmSysCall.c -
 *
 * 	This file contains routines that handle virtual memory system
 *	calls.
 *
 * Copyright (C) 1985 Regents of the University of California
 * All rights reserved.
 */


#ifndef lint
static char rcsid[] = "$Header$ SPRITE (Berkeley)";
#endif not lint

#include <sprite.h>
#include <vm.h>
#include <vmInt.h>
#include <lock.h>
#include <user/vm.h>
#include <sync.h>
#include <sys.h>
#include <stdlib.h>
#include <string.h>
#include <fs.h>
#include <fsio.h>
#include <sys/mman.h>
#include <stdio.h>
#include <bstring.h>

extern Vm_SharedSegTable sharedSegTable;
char	 *sprintf();

int vmShmDebug = 0;	/* Shared memory debugging flag. */

extern int   debugFsStubs; /* Unix compatibility debug flags. */
extern int   debugProcStubs;
extern int   debugSigStubs;
extern int   debugSysStubs;
extern int   debugTimerStubs;
extern int   debugVmStubs;

/*
 * Forward declaration.
 */
static ReturnStatus VmMunmapInt _ARGS_((Address startAddr, int length,
	int noError));

/*
 * VmVirtAddrParseUnlock calls VmVirtAddrParse and then unlocks
 * the heap page table if necessary, since VmVirtAddrParse may
 * lock it.
 */
#define VmVirtAddrParseUnlock(procPtr, startAddr, virtAddrPtr) \
    {VmVirtAddrParse(procPtr,startAddr, virtAddrPtr); \
    if ((virtAddrPtr)->flags & VM_HEAP_PT_IN_USE) \
	    { VmDecPTUserCount(procPtr->vmPtr->segPtrArray[VM_HEAP]); } }

/*
 * Test if an address is not page aligned.
 */
#define BADALIGN(addr) (((int)(addr))&(vm_PageSize-1))


/*
 *----------------------------------------------------------------------
 *
 * Vm_PageSize --
 *
 *	Return the hardware page size.
 *
 * Results:
 *	Status from copying the page size out to user space.
 *
 * Side effects:
 *	Copy page size out to user address space.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Vm_PageSize(pageSizePtr)
    int	*pageSizePtr;
{
    int			pageSize = vm_PageSize;

    return(Vm_CopyOut(4, (Address) &pageSize, (Address) pageSizePtr));
}


/***************************************************************************
 *
 *	The following two routines, Vm_CreateVA and Vm_DestroyVA, are used
 *	to allow users to add or delete ranges of valid virtual addresses
 *	from their virtual address space.  Since neither of these routiens
 *	are monitored (although they call monitored routines), there are 
 *	potential synchronization problems for users sharing memory who
 *	expand their heap segment.  The problems are caused if two or more
 *	users attempt to simultaneously change the size of the heap segment 
 *	to different sizes. The virtual memory system will not have any 
 *	problems handling the conflicting requests, however the actual range 
 *	of valid virtual addresses is unpredictable.  It is up to the user
 *	to synchronize when expanding the HEAP segment.
 */


/*
 *----------------------------------------------------------------------
 *
 * Vm_CreateVA --
 *
 *	Validate the given range of addresses.  If necessary the segment
 *	is expanded to make room.
 *
 * Results:
 *	VM_WRONG_SEG_TYPE if tried to validate addresses for a stack or
 *	code segment and VM_SEG_TOO_LARGE if the segment cannot be
 *	expanded to fill the size.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Vm_CreateVA(address, size)
    Address address;	/* Address to validate from. */
    int	    size;	/* Number of bytes to validate. */
{
    register Vm_Segment *segPtr;
    int			firstPage, lastPage;
    Proc_ControlBlock	*procPtr;

    procPtr = Proc_GetCurrentProc();
    firstPage = (unsigned) (address) >> vmPageShift;
    lastPage = (unsigned) ((int) address + size - 1) >> vmPageShift;

    segPtr = (Vm_Segment *) procPtr->vmPtr->segPtrArray[VM_HEAP];

    /*
     * Make sure that the beginning address falls into the heap segment and
     * not the code segment.
     */
    if (firstPage < segPtr->offset) {
	return(VM_WRONG_SEG_TYPE);
    }

    /*
     * Make sure that the ending page is not greater than the highest
     * possible page.  Since there must be one stack page and one page
     * between the stack and the heap, the highest possible heap page is
     * mach_LastUserStackPage - 2 or segPtr->maxAddr, whichever is lower.
     */
    if (lastPage > mach_LastUserStackPage - 2 ||
        address + size - 1 > segPtr->maxAddr) {
	return(VM_SEG_TOO_LARGE);
    }

    /*
     * Make pages between firstPage and lastPage part of the heap segment's
     * virtual address space.
     */
    return(VmAddToSeg(segPtr, firstPage, lastPage));
}


/*
 *----------------------------------------------------------------------
 *
 * Vm_DestroyVA --
 *
 *	Invalidate the given range of addresses.  If the starting address
 *	is too low then an error message is returned.
 *
 * Results:
 *	VM_WRONG_SEG_TYPE if tried to invalidate addresses for a code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Vm_DestroyVA(address, size)
    Address address;	/* Address to validate from. */
    int	    size;	/* Number of bytes to validate. */
{
    register Vm_Segment *segPtr;
    int			firstPage, lastPage;
    Proc_ControlBlock	*procPtr;

    procPtr = Proc_GetCurrentProc();
    firstPage = (unsigned) (address) >> vmPageShift;
    lastPage = (unsigned) ((int) address + size - 1) >> vmPageShift;

    segPtr = (Vm_Segment *) procPtr->vmPtr->segPtrArray[VM_HEAP];

    /*
     * Make sure that the beginning address falls into the 
     * heap segment.
     */
    if (firstPage < segPtr->offset) {
	return(VM_WRONG_SEG_TYPE);
    }

    /*
     * Make pages between firstPage and lastPage not members of the segment's
     * virtual address space.
     */
    return(Vm_DeleteFromSeg(segPtr, firstPage, lastPage));
}

static int	copySize = 4096;

#ifndef CLEAN
static char	buffer[8192];
#endif CLEAN

void	SetVal();

/*
 * The # construct to turn a variable into a string is not handled correctly
 * on the current ds3100 (non-ansi) compiler/preprocessor set up.
 * This will change when it's handled correctly.
 */
#if defined(__STDC__)
#define SETVAR(var, val) SetVal(#var, val, (int *)&(var))
#else
#define	SETVAR(var, val) SetVal("var", val, (int *)&(var))
#endif /* sun4 */


/*
 *----------------------------------------------------------------------
 *
 * Vm_Cmd --
 *
 *      This routine allows a user level program to give commands to
 *      the virtual memory system.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Some parameter of the virtual memory system will be modified.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Vm_Cmd(command, arg)
    int		command;
    int         arg;
{
    int			numBytes;
    ReturnStatus	status = SUCCESS;
    int			numModifiedPages;
 
    switch (command) {
	case VM_COUNT_DIRTY_PAGES:
	    numModifiedPages = VmCountDirtyPages();
	    if (Vm_CopyOut(sizeof(int), (Address) &numModifiedPages,
			   (Address) arg) != SUCCESS) {
		status = SYS_ARG_NOACCESS;
	    }
	    break;
	case VM_FLUSH_SEGMENT: {
	    extern int	vmNumSegments;
	    int		intArr[3];
	    Vm_Segment	*segPtr;
	    int		lowPage;
	    int		highPage;
	    Vm_VirtAddr	virtAddr;

	    status = Vm_CopyIn(3 * sizeof(int), (Address)arg, 
				(Address)intArr);
	    if (status != SUCCESS) {
		break;
	    }
	    if (intArr[0] <= 0 || intArr[0] >= vmNumSegments) {
		status = SYS_INVALID_ARG;
		break;
	    }
	    segPtr = VmGetSegPtr(intArr[0]);
	    if (segPtr->type == VM_STACK) {
		lowPage = mach_LastUserStackPage - segPtr->numPages + 1;
		highPage = mach_LastUserStackPage;
	    } else {
		lowPage = segPtr->offset;
		highPage = segPtr->offset + segPtr->numPages - 1;
	    }
	    if (intArr[1] >= lowPage && intArr[1] <= highPage) {
		lowPage = intArr[1];
	    }
	    if (intArr[2] >= lowPage && intArr[2] <= highPage) {
		highPage = intArr[2];
	    }
	    virtAddr.sharedPtr = (Vm_SegProcList *)NIL;
	    virtAddr.segPtr = segPtr;
	    virtAddr.page = lowPage;
	    VmFlushSegment(&virtAddr, highPage);
	    break;
	}
	case VM_SET_FREE_WHEN_CLEAN:
	    SETVAR(vmFreeWhenClean, arg);
	    break;
	case VM_SET_MAX_DIRTY_PAGES:
	    SETVAR(vmMaxDirtyPages, arg);
	    break;
	case VM_SET_PAGEOUT_PROCS:
	    SETVAR(vmMaxPageOutProcs, arg);
	    break;
        case VM_SET_CLOCK_PAGES:
            SETVAR(vmPagesToCheck, arg);
            break;
        case VM_SET_CLOCK_INTERVAL: {
	    SETVAR(vmClockSleep, (int) (arg * timer_IntOneSecond));
            break;
	}
	case VM_SET_COPY_SIZE:
	    SETVAR(copySize, arg);
	    break;
#ifndef CLEAN
	case VM_DO_COPY_IN:
	    (void)Vm_CopyIn(copySize, (Address) arg, buffer);
	    break;
	case VM_DO_COPY_OUT:
	    (void)Vm_CopyOut(copySize, buffer, (Address) arg);
	    break;
	case VM_DO_MAKE_ACCESS_IN:
	    Vm_MakeAccessible(0, copySize, (Address) arg, &numBytes,
			      (Address *) &arg);
	    bcopy((Address) arg, buffer, copySize);
	    Vm_MakeUnaccessible((Address) arg, numBytes);
	    break;
	case VM_DO_MAKE_ACCESS_OUT:
	    Vm_MakeAccessible(0, copySize, (Address) arg, &numBytes,
			      (Address *) &arg);
	    bcopy(buffer, (Address) arg, copySize);
	    Vm_MakeUnaccessible((Address) arg, numBytes);
	    break;
#endif CLEAN
	case VM_GET_STATS: {
#if defined(ds3100) || defined(ds5000)
	    extern unsigned int end;
	    /*
	     * The decstations have a big hole between the initialized data
	     * and the heap, so we can't just subtract the kernel beginning
	     * from the end.
	     */
	    vmStat.kernMemPages = (unsigned int) 
		(vmMemEnd - VMMACH_VIRT_CACHED_START + (unsigned int) &end
		 - mach_KernStart) / vm_PageSize;
#else
	    vmStat.kernMemPages = 
		    (unsigned int)(vmMemEnd - mach_KernStart) / vm_PageSize;
#endif
	    if (Vm_CopyOut(sizeof(Vm_Stat), (Address) &vmStat, 
			   (Address) arg) != SUCCESS) {
		status = SYS_ARG_NOACCESS;
	    }
	    break;
	}
	case VM_SET_COW:
	    /*
	     * It's okay to turn on COW when it's off, but not the other
	     * way around.
	     */
	    if (arg || !vm_CanCOW) {
		SETVAR(vm_CanCOW, arg);
	    } else {
		status = GEN_INVALID_ARG;
	    }
	    break;
	case VM_SET_FS_PENALTY:
	    if (arg <= 0) {
		/*
		 * Caller is setting an absolute penalty.
		 */
		SETVAR(vmCurPenalty, -arg);
	    } else {
		SETVAR(vmFSPenalty, arg);
		SETVAR(vmCurPenalty, (vmStat.fsMap - vmStat.fsUnmap) / 
					vmPagesPerGroup * vmFSPenalty);
	    }
	    break;
	case VM_SET_NUM_PAGE_GROUPS: {
	    int	numPages;
	    int curGroup;
	    numPages = vmPagesPerGroup * vmNumPageGroups;
	    if (arg <= 0) {
		status = GEN_INVALID_ARG;
		break;
	    }
	    SETVAR(vmNumPageGroups, arg);
	    SETVAR(vmPagesPerGroup, numPages / vmNumPageGroups);
	    curGroup = (vmStat.fsMap - vmStat.fsUnmap) / vmPagesPerGroup;
	    SETVAR(vmCurPenalty, curGroup * vmFSPenalty);
	    SETVAR(vmBoundary, (curGroup + 1) * vmPagesPerGroup);
	    break;
	}
	case VM_SET_ALWAYS_REFUSE:
	    SETVAR(vmAlwaysRefuse, arg);
	    break;
	case VM_SET_ALWAYS_SAY_YES:
	    SETVAR(vmAlwaysSayYes, arg);
	    break;
	case VM_RESET_FS_STATS:
	    vmStat.maxFSPages = vmStat.fsMap - vmStat.fsUnmap;
	    vmStat.minFSPages = vmStat.fsMap - vmStat.fsUnmap;
	    break;
	case VM_SET_COR_READ_ONLY:
	    SETVAR(vmCORReadOnly, arg);
	    break;
	case VM_SET_PREFETCH:
	    SETVAR(vmPrefetch, arg);
	    break;
	case VM_SET_USE_FS_READ_AHEAD:
	    SETVAR(vmUseFSReadAhead, arg);
	    break;
	case VM_SET_WRITEABLE_PAGEOUT:
	    SETVAR(vmWriteablePageout, arg);
	    break;
	case VM_SET_WRITEABLE_REF_PAGEOUT:
	    SETVAR(vmWriteableRefPageout, arg);
	    break;
	case 1999:
	    SETVAR(vmShmDebug, arg);
	    break;
	case 1234: /* Temporary unix compatibility hook. */
	    SETVAR(debugFsStubs, arg);
	    SETVAR(debugProcStubs, arg);
	    SETVAR(debugSigStubs, arg);
	    SETVAR(debugSysStubs, arg);
	    SETVAR(debugTimerStubs, arg);
	    SETVAR(debugVmStubs, arg);
        default:
	    status = VmMach_Cmd(command, arg);
            break;
    }
 
    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * SetVal --
 *
 *      Set a given VM variable.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The given variable is set.
 *
 *----------------------------------------------------------------------
 */
void
SetVal(descript, newVal, valPtr)
    char	*descript;
    int		newVal;
    int		*valPtr;
{
    printf("%s val was %d, is %d\n", descript, *valPtr, newVal);
    *valPtr = newVal;
}

/*
 *----------------------------------------------------------------------
 *
 * Vm_Mmap --
 *
 *	Map a page.
 *
 * Results:
 *	Status from the map.
 *
 * Side effects:
 *	Maps the page.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ENTRY ReturnStatus
Vm_Mmap(startAddr, length, prot, share, streamID, fileAddr, mappedAddr)
    Address	startAddr;	/* Requested starting virt-addr. */
    int		length;		/* Length of mapped segment. */
    int		prot;		/* Protection for mapped segment. */
    int		share;		/* Private/shared flag. */
    int		streamID;	/* Open file to be mapped. */
    int		fileAddr;	/* Offset into mapped file. */
    Address	*mappedAddr;	/* Mapped address (user space). */
{
    Address mappedAddrInt;
    ReturnStatus status;

    status = Vm_MmapInt(startAddr, length, prot, share, streamID, fileAddr,
	    &mappedAddrInt);
    if (status==SUCCESS) {
	status = Vm_CopyOut(sizeof(int), (Address)&mappedAddrInt,
		(Address)mappedAddr);
    }
    return status;
    
}

/*
 *----------------------------------------------------------------------
 *
 * Vm_MmapInt --
 *
 *	Internal routine for Vm_Mmap.
 *
 * Results:
 *	Status from the map.
 *
 * Side effects:
 *	Maps the page.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ENTRY ReturnStatus
Vm_MmapInt(startAddr, length, prot, share, streamID, fileAddr, mappedAddr)
    Address	startAddr;	/* Requested starting virt-addr. */
    int		length;		/* Length of mapped segment. */
    int		prot;		/* Protection for mapped segment. */
    int		share;		/* Private/shared flag. */
    int		streamID;	/* Open file to be mapped. */
    int		fileAddr;	/* Offset into mapped file. */
    Address	*mappedAddr;	/* Mapped address. */
{
    Proc_ControlBlock	*procPtr;
    Fs_Stream 		*streamPtr;
    int			pnum;
    ReturnStatus	status = SUCCESS;
    Fs_Attributes	attr;
    Vm_SharedSegTable	*segTabPtr;
    Vm_Segment		*segPtr;
    Vm_SegProcList	*sharedSeg;
    Fs_Stream 		*filePtr;

    length = (length+vm_PageSize-1)&~(vm_PageSize-1);
    dprintf("mmap( %x, %d, %d, %d, %d, %d)\n", startAddr, length,
	    prot, share, streamID, fileAddr);

    if ((share&MAP_TYPE)!=MAP_SHARED && (share&MAP_TYPE)!=MAP_PRIVATE) {
	printf("Vm_Mmap: Invalid share flag %x\n", share);
        return VM_WRONG_SEG_TYPE;
    }

    procPtr = Proc_GetCurrentProc();
    status = Fs_GetStreamPtr(procPtr,streamID,&streamPtr);
    dprintf("Vm_Mmap: procPtr: %x  streamID: %d streamPtr: %x\n",
	    procPtr,streamID,(int)streamPtr);
    if (status != SUCCESS) {
	printf("Vm_Mmap: Fs_GetStreamPtr failure\n");
	return status;
    }
    if (debugVmStubs) {
	printf("mmap: refCount = %d for stream %x ",
		((Fs_HandleHeader *)streamPtr)->refCount, streamPtr);
    }
    Fsio_StreamCopy(streamPtr,&filePtr);
    if (debugVmStubs) {
	printf(", after copy refCount = %d\n", ((Fs_HandleHeader *)streamPtr)->refCount);
    }

    status = Fs_GetAttrStream(filePtr,&attr);
    if (status != SUCCESS) {
	printf("Vm_Mmap: Fs_GetAttrStream failure\n");
	(void)Fs_Close(filePtr);
	return status;
    }
    dprintf("file: fileNumber %d size %d\n",attr.fileNumber, attr.size);

    if (!(attr.type == FS_DEVICE) &&
            (BADALIGN(startAddr) || BADALIGN(fileAddr))) {
        printf("Vm_Mmap: Invalid start or offset\n");
        return VM_WRONG_SEG_TYPE;
    }
    /*
     * Do a device-specific mmap.
     */
    if (attr.type == FS_DEVICE) {
        extern  ReturnStatus    Fsio_DeviceMmap();

        (void) Fs_Close(filePtr);               /* Don't need this filePtr. */
        /*
         * Should I really indirect through the file system here?  But that
         * requires adding an mmap switch to all the file system crud.  I'll
         * test it this way first by going straight to Fs_Device stuff.
         */
        status = Fsio_DeviceMmap(streamPtr, startAddr, length, fileAddr,
                &startAddr);
        if (status == SUCCESS) {
	    *mappedAddr = startAddr;
        }
	return status;
    }


    /* 
     * Check permissions.
     * The rules:
     * The file must have read permissions.
     * We must request read and/or write permissions.
     * If we take a private copy, we can do whatever we want.
     * Otherwise we must have the requested permissions.
     */
    if (attr.type != FS_FILE) {
	printf("Vm_Mmap: not a file\n");
	(void)Fs_Close(filePtr);
	return VM_WRONG_SEG_TYPE;
    }
    if (!(filePtr->flags & FS_READ) || !(prot & (PROT_READ | PROT_WRITE)) ||
	    ((share&MAP_TYPE)!=MAP_PRIVATE &&
	    (((prot & PROT_WRITE) && !(filePtr->flags & FS_WRITE)) ||
	    ((prot & PROT_EXEC) && !(filePtr->flags & FS_EXECUTE))))) {
	printf("Vm_Mmap: protection failure\n");
	printf("flags = %x, prot = %x\n", filePtr->flags, prot);
	(void)Fs_Close(filePtr);
	return VM_WRONG_SEG_TYPE;
    }

    LOCK_SHM_MONITOR;
    if (procPtr->vmPtr->sharedSegs == (List_Links *)NIL) {
	dprintf("Vm_Mmap: New proc list\n");
	procPtr->vmPtr->sharedSegs = (List_Links *)
		malloc(sizeof(Vm_SegProcList));
	VmMach_SharedProcStart(procPtr);
	dprintf("Vm_Mmap: sharedStart: %x\n",procPtr->vmPtr->sharedStart);
	List_Init((List_Links *)procPtr->vmPtr->sharedSegs);
	Proc_NeverMigrate(procPtr);
    }
    if (!(share&MAP_FIXED) || startAddr==0) {
	status = VmMach_SharedStartAddr(procPtr, length, &startAddr, 0);
    } else {
	status = VmMach_SharedStartAddr(procPtr, length, &startAddr, 1);
    }
    if (status != SUCCESS) {
	printf("Vm_Mmap: VmMach_SharedStart failure\n");
	UNLOCK_SHM_MONITOR;
	(void)Fs_Close(filePtr);
	return status;
    }

    /*
     * See if a shared segment is already using the specified file.
     * If we're making a private copy, we probably need a new segment.
     * (We should probably do something more intelligent about mapping
     * multiple copies of a file into memory.)
     */
    segPtr = (Vm_Segment *) NIL;
    segTabPtr = (Vm_SharedSegTable *) NIL;
    if ((share&MAP_TYPE)!=MAP_PRIVATE) {
	LIST_FORALL((List_Links *)&sharedSegTable, (List_Links *)segTabPtr) {
	    if (segTabPtr->serverID == attr.serverID && segTabPtr->domain ==
		    attr.domain && segTabPtr->fileNumber == attr.fileNumber) {
		segPtr = segTabPtr->segPtr;
		break;
	    }
	}
    }

#if 0
    pnum = 10288;	/* Random number, since shared memory shouldn't
				be using this value */
#else
    pnum = (int)startAddr>>vmPageShift;
#endif

    if (segPtr == (Vm_Segment *)NIL) {
	dprintf("Vm_Mmap: New segment\n");
	/*
	 * Create a new segment and add to the shared segment list.
	 */
	
	segTabPtr = (Vm_SharedSegTable*) malloc(sizeof(Vm_SharedSegTable));
	dprintf("Vm_Mmap: creating new segment\n");
	segTabPtr->segPtr = Vm_SegmentNew(VM_SHARED,(Fs_Stream *)NIL,
		0,length>>vmPageShift,pnum,procPtr);
	if ((share&MAP_TYPE)==MAP_PRIVATE) {
	    segTabPtr->segPtr->filePtr = filePtr;
	} else {
	    segTabPtr->segPtr->filePtr = (Fs_Stream *)NIL;
	    segTabPtr->segPtr->swapFilePtr = filePtr;
	    segTabPtr->segPtr->flags |= VM_SWAP_FILE_OPENED;
	}
	segTabPtr->serverID = attr.serverID;
	segTabPtr->domain = attr.domain;
	segTabPtr->fileNumber = attr.fileNumber;
	segTabPtr->refCount = 0;
	dprintf("Vm_Mmap: Validating pages %x to %x\n",
		pnum,pnum+(length>>vmPageShift)-1);
	Vm_ValidatePages(segTabPtr->segPtr,pnum,pnum+(length>>vmPageShift)-1,
		FALSE,TRUE);
	List_Insert((List_Links *)segTabPtr,
		LIST_ATFRONT((List_Links *)&sharedSegTable));
	dprintf("Calling Fs_FileBeingMapped(1)\n");
	Fs_FileBeingMapped(filePtr,1);
	dprintf("Done Fs_FileBeingMapped(1)\n");
    } else {
	int i;
	/*
	startAddr = (Address)(segPtr->offset<<vmPageShift);
	*/
	if (length > (segPtr->numPages<<vmPageShift)) {
	    dprintf("Vm_Mmap: Enlarging segment: 0 to %d\n",
		    ((int)length-1)>>vmPageShift);
	    status = VmAddToSeg(segPtr,segPtr->offset,segPtr->offset+
		    (length>>vmPageShift)-1);
	    if (status != SUCCESS) {
		printf("VmAddToSeg failure\n");
		UNLOCK_SHM_MONITOR;
		(void)Fs_Close(filePtr);
		return status;
	    }
	}
	for (i=0;i<32;i++) {
	    if (segPtr->ptPtr[i] & VM_VIRT_RES_BIT) {
		dprintf("1");
	    } else {
		dprintf("0");
	    }
	}
	dprintf("\n");
	if (procPtr->vmPtr->sharedSegs == (List_Links *)NIL ||
	    !VmCheckSharedSegment(procPtr,segPtr)) {
	    /*
	     * Process is not using segment.
	     */
	    dprintf("Vm_Mmap: Adding reference to proc to segment\n");
	    Vm_SegmentIncRef(segPtr,procPtr);
	} else {
	    /*
	     * Process is already using segment.
	     */
	    dprintf("Vm_Mmap: Process is using segment.\n");
	}
	/*
	 * We don't want to keep the file pointer around, since the
	 * shared memory segment already exists.
	 */
	if (debugVmStubs) {
	    printf("Closing filePtr, refCount = %d\n", 
		((Fs_HandleHeader *)streamPtr)->refCount);
	}
	(void)Fs_Close(filePtr);
    }

    /*
     * Unmap any current mapping at the address range.
     */
    status=VmMunmapInt(startAddr,length,1);
    if (status == SUCCESS) {
	/*
	 * Force the mach module to allocate the segment again.
	 * (The allocation of shared segments in memory needs to be fixed up.
	 * The problem is that we may have multiple segments mapped into
	 * the same memory, and thus it is hard to keep track of what's
	 * in use).  This should force the segment to be marked as in use.
	 * There still may be a problem if part of a segment is unmapped.
	 */
	status = VmMach_SharedStartAddr(procPtr, length, &startAddr, 1);
    }
    if (status != SUCCESS) {
	printf("Vm_Mmap: Vm_Munmap failure\n");
	UNLOCK_SHM_MONITOR;
	(void)Fs_Close(filePtr);
	return status;
    }

    /*
     * Add the segment to the process's list of shared segments.
     */
    dprintf("Vm_Mmap: Adding segment to list\n");
    sharedSeg = (Vm_SegProcList *)malloc(sizeof(Vm_SegProcList));
    sharedSeg->fd = streamID;
    sharedSeg->stream = filePtr;
    sharedSeg->segTabPtr = segTabPtr;
    segTabPtr->refCount++;
    sharedSeg->addr = startAddr;
    sharedSeg->mappedStart = startAddr;
    dprintf("fileAddr: %x\n",fileAddr);
    sharedSeg->fileAddr = fileAddr;
    sharedSeg->offset = (int)startAddr>>vmPageShift;
    sharedSeg->mappedEnd = startAddr+length-1;
    sharedSeg->prot = (prot&PROT_WRITE) ? 0 : VM_READONLY_SEG;
    dprintf("Set prot to %d\n",sharedSeg->prot);
    if ((int)sharedSeg->segTabPtr == -1) {
	dprintf("Vm_Mmap: Danger: sharedSeg->segTabPtr is -1!\n");
    }
    List_Insert((List_Links *)sharedSeg,
	    LIST_ATFRONT((List_Links *)procPtr->vmPtr->sharedSegs));
    

    VmPrintSharedSegs(procPtr);
    UNLOCK_SHM_MONITOR;
    dprintf("Vm_Mmap: Completed page mapping\n");
    *mappedAddr = startAddr;
    if (debugVmStubs) {
	printf("end of mmap: refCount = %d\n",
		((Fs_HandleHeader *)streamPtr)->refCount);
    }
    return SUCCESS;
}

/*
 *----------------------------------------------------------------------
 *
 * Vm_Munmap --
 *
 *	Unmaps the process's pages in a specified address range.
 *	Gets the lock and then calls VmMunmapInt.
 *
 * Results:
 *	Status from the unmap operation.
 *
 * Side effects:
 *	Unmaps the pages.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Vm_Munmap(startAddr, length, noError)
    Address	startAddr;	/* Starting virt-addr. */
    int		length;		/* Length of mapped segment. */
    int		noError;	/* Ignore errors. */
{
    ReturnStatus status;

    dprintf("munmap(%x, %d, %d)\n", startAddr, length, noError);
    if (BADALIGN(startAddr) || BADALIGN(length)) {
	printf("Vm_Munmap: Invalid start or offset\n");
	return VM_WRONG_SEG_TYPE;
    }
    LOCK_SHM_MONITOR;
    status = VmMunmapInt(startAddr,length, noError);
    UNLOCK_SHM_MONITOR;
    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * VmMunmapInt --
 *
 *	Unmaps the process's pages in a specified address range.
 *
 * Results:
 *	Status from the unmap operation.
 *
 * Side effects:
 *	Unmaps the pages.
 *
 *----------------------------------------------------------------------
 */
static ReturnStatus
VmMunmapInt(startAddr, length, noError)
    Address	startAddr;	/* Starting virt-addr. */
    int		length;		/* Length of mapped segment. */
    int		noError;	/* 1 if don't care about errors
				   from absent segments. */
{

    ReturnStatus	status = SUCCESS;
    Proc_ControlBlock	*procPtr;
    Vm_SegProcList		*segProcPtr;
    Vm_SegProcList		*newSegProcPtr;
    Address		addr;
    Address		addr1;
    Address		endAddr;
    Vm_VirtAddr		virtAddr;

    virtAddr.flags = 0;
    virtAddr.offset = 0;

    dprintf("Vm_Munmap: Unmapping shared pages\n");

    procPtr = Proc_GetCurrentProc();
    endAddr = startAddr+length;
    for (addr=startAddr; addr<endAddr; ) {

	dprintf("Vm_Munmap: trying to eliminate %x to %x\n",
		(int)addr,(int)endAddr-1);
	/* Find the segment corresponding to this address. */
	if (procPtr->vmPtr->sharedSegs != (List_Links *)NIL) {
	    segProcPtr = VmFindSharedSegment(procPtr->vmPtr->sharedSegs,addr);
	} else if (noError) {
	    return SUCCESS;
	} else {
	    dprintf("Vm_Munmap: Process has no shared segments\n");
	    return VM_WRONG_SEG_TYPE;
	}

	/* There are three possibilities:
	   1. The unmap can remove a mapping.
	   2. The unmap can shrink a mapping.
	   3. The unmap can split a mapping.
	*/

	if (segProcPtr == (Vm_SegProcList *)NIL) {
	    if (!noError) {
		dprintf("Vm_Munmap: Page to unmap not in valid range\n");
		status = VM_WRONG_SEG_TYPE;
	    }
	    /*
	     * Move to next mapped segment, so we can keep unmapping.
	     */
	    addr1 = endAddr;
	    LIST_FORALL(procPtr->vmPtr->sharedSegs,
		    (List_Links *)segProcPtr) {
		if (segProcPtr->mappedStart > addr && 
		    segProcPtr->mappedStart < addr1) {
		    addr1 = segProcPtr->mappedStart;
		}
	    }
	} else {
	    addr1 = segProcPtr->mappedEnd+1;

	    /*
	     * Invalidate the pages to be mapped out.
	     */
	    virtAddr.page = max((int)segProcPtr->mappedStart,(int)addr)
		    >> vmPageShift;
	    virtAddr.segPtr = segProcPtr->segTabPtr->segPtr;
	    virtAddr.sharedPtr = segProcPtr;
	    dprintf("Vm_Munmap: invalidating: %x to %x\n",
		    (int)virtAddr.page<<vmPageShift,
		    min((int)segProcPtr->mappedEnd+1, (int)endAddr));
	    VmFlushSegment(&virtAddr,min((int)segProcPtr->mappedEnd+1,
		    (int)endAddr)>>vmPageShift);

	    if (segProcPtr->mappedStart < addr) {
		dprintf("Vm_Munmap: Shortening mapped region (1)\n");
		if (segProcPtr->mappedEnd >= endAddr) {
		    dprintf("Vm_Munmap: Splitting mapped region\n");
		    /*
		     * Split the mapped region.
		     */
		    newSegProcPtr = (Vm_SegProcList *)
			    malloc(sizeof(Vm_SegProcList));
		    if (debugVmStubs) {
			printf("Malloc'd %x\n", newSegProcPtr);
		    }
		    *newSegProcPtr = *segProcPtr;
		    newSegProcPtr->mappedStart = endAddr;
		    newSegProcPtr->mappedEnd = segProcPtr->mappedEnd;
		    segProcPtr->segTabPtr->refCount++;
		    List_Insert((List_Links *)newSegProcPtr, LIST_AFTER(
			    (List_Links *)segProcPtr));
		}
		/*
		 * Shrink the first mapping.
		 */
		segProcPtr->mappedEnd = addr-1;
	    } else if (segProcPtr->mappedEnd >= endAddr) {
		/*
		 * Shrink the mapped region.
		 */
		dprintf("Vm_Munmap: Shortening mapped region (2)\n");
		segProcPtr->mappedStart = endAddr;
	    } else {
		dprintf("Vm_Munmap: Removing mapped region\n");
		/*
		 * Remove the entire mapped region.
		 */
		Vm_DeleteSharedSegment(procPtr,segProcPtr);
	    }
	}
	if (addr == addr1) {
	    panic("Vm_Munmap loop\n");
	}
	addr = addr1;
    }

    VmPrintSharedSegs(procPtr);
    dprintf("Vm_Munmap: done\n");
    return status ;
}

/* 
 * Check if an address range is valid for the segment.
 */
#define CheckBounds(virtAddrPtr,startAddr,length) \
	((unsigned)(((int)(startAddr)>>vmPageShift) - segOffset(virtAddrPtr)) \
	< (virtAddrPtr)->segPtr->ptSize && \
	((unsigned)((((int)(startAddr)+(length)-1)>>vmPageShift) - \
	segOffset(virtAddrPtr)) < (virtAddrPtr)->segPtr->ptSize))
/*
 *----------------------------------------------------------------------
 *
 * Vm_Msync --
 *
 *	Sync pages to disk.
 *	startAddr and length must be divisible by the page size.
 *
 * Results:
 *	Status from the sync.
 *
 * Side effects:
 *	Page goes to disk.
 *
 *----------------------------------------------------------------------
 */
ENTRY ReturnStatus
Vm_Msync(startAddr, length)
    Address	startAddr;	/* Requested starting virt-addr. */
    int		length;		/* Length of region to page out. */
{
    Vm_VirtAddr		virtAddr;
    ReturnStatus	status;
    Proc_ControlBlock	*procPtr;

    dprintf("msync( %x, %d)\n", startAddr, length);
    if (BADALIGN(startAddr) || BADALIGN(length)) {
	printf("Vm_Msync: Invalid start or offset\n");
	return VM_WRONG_SEG_TYPE;
    }
    LOCK_SHM_MONITOR;
    procPtr = Proc_GetCurrentProc();
    VmVirtAddrParseUnlock(procPtr, startAddr, &virtAddr);
    if (virtAddr.segPtr == (Vm_Segment *)NIL || !CheckBounds(&virtAddr,
	    startAddr, length)) {
	UNLOCK_SHM_MONITOR;
	return SYS_INVALID_ARG;
    }
    status = VmPageFlush(&virtAddr, length, TRUE, TRUE);
    UNLOCK_SHM_MONITOR;
    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * Vm_Mlock --
 *
 *	Locks a page into memory.  Page is paged in if necessary.
 *
 * Results:
 *	Fails if unable to lock page.
 *
 * Side effects:
 *	Locks the page.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ENTRY ReturnStatus
Vm_Mlock(startAddr, length)
    Address	startAddr;	/* Requested starting virt-addr. */
    int		length;		/* Length of region to lock. */
{
    Vm_VirtAddr		virtAddr;
    Proc_ControlBlock	*procPtr;
    int			maptype = VM_READWRITE_ACCESS;

    dprintf("mlock( %x, %d)\n", startAddr, length);
    if (BADALIGN(startAddr) || BADALIGN(length)) {
	printf("Vm_Mlock: Invalid start or offset\n");
	return VM_WRONG_SEG_TYPE;
    }
    procPtr = Proc_GetCurrentProc();
    VmVirtAddrParseUnlock(procPtr, startAddr, &virtAddr);
    if (virtAddr.segPtr == (Vm_Segment *)NIL || !CheckBounds(&virtAddr,
	    startAddr, length)) {
	return SYS_INVALID_ARG;
    } else if (virtAddr.sharedPtr != (Vm_SegProcList *)NIL) {
	maptype = virtAddr.sharedPtr->prot == VM_READONLY_SEG ?
		VM_READONLY_ACCESS : VM_READWRITE_ACCESS;
    } else {
	maptype = virtAddr.segPtr->type == VM_CODE ? VM_READONLY_ACCESS :
		VM_READWRITE_ACCESS;
    }
    return Vm_PinUserMem(maptype,length,startAddr);
}

/*
 *----------------------------------------------------------------------
 *
 * Vm_Munlock --
 *
 *	Unlocks a page.
 *
 * Results:
 *	Status from the unlock.
 *
 * Side effects:
 *	Unlocks the page.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ENTRY ReturnStatus
Vm_Munlock(startAddr, length)
    Address	startAddr;	/* Requested starting virt-addr. */
    int		length;		/* Length of region to unlock. */
{
    Vm_VirtAddr	virtAddr;
    Proc_ControlBlock	*procPtr;

    dprintf("munlock( %x, %d)\n", startAddr, length);
    if (BADALIGN(startAddr) || BADALIGN(length)) {
	printf("Vm_Munlock: Invalid start or offset\n");
	return VM_WRONG_SEG_TYPE;
    }
    procPtr = Proc_GetCurrentProc();
    VmVirtAddrParseUnlock(procPtr, startAddr, &virtAddr);
    if (virtAddr.segPtr == (Vm_Segment *)NIL || !CheckBounds(&virtAddr,
	    startAddr, length)) {
	return SYS_INVALID_ARG;
    }
    Vm_UnpinUserMem(length,startAddr);
    return SUCCESS;
}

/*
 *----------------------------------------------------------------------
 *
 * Vm_Mincore --
 *
 *	Returns residency vector.
 *
 * Results:
 *	The values of vec are 0 if the page is not virtually resident,
 *	1 if the page is paged out, 2 if the page is clean and
 *	untouched, 3 if the page is referenced, and 4 if the page is dirty.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/

ENTRY ReturnStatus
Vm_Mincore(startAddr, length, retVec)
    Address	startAddr;	/* Requested starting virt-addr. */
    int		length;		/* Length of region. */
    char *	retVec;		/* Return vector. */
{
    char		*vec;
    Address		checkAddr;
    Vm_VirtAddr		virtAddr;
    Proc_ControlBlock	*procPtr;
    ReturnStatus	status;
    int			len;
    int			i;
    Vm_PTE		*ptePtr;
    Boolean		referenced;
    Boolean		modified;

    dprintf("mincore( %x, %d, %x)\n", startAddr, length, retVec);
    if (BADALIGN(startAddr) || BADALIGN(length)) {
	printf("Vm_Mincore: Invalid start or offset\n");
	return VM_WRONG_SEG_TYPE;
    }
    procPtr = Proc_GetCurrentProc();
    len = length>>vmPageShift;
    vec = (char *)malloc(len);
    checkAddr = startAddr;
    for (i=0;i<len;i++) {
	VmVirtAddrParseUnlock(procPtr, checkAddr, &virtAddr);
	if (virtAddr.segPtr == (Vm_Segment *)NIL) {
	    vec[i] = 0;
	    dprintf("Vm_Mincore: no segment at address %x\n", checkAddr);
	} else {
	    ptePtr = VmGetAddrPTEPtr(&virtAddr, virtAddr.page);
	    if (*ptePtr & VM_VIRT_RES_BIT) {
		if (*ptePtr & VM_PHYS_RES_BIT) {
		    VmMach_GetRefModBits(&virtAddr, Vm_GetPageFrame(*ptePtr),
			    &referenced, &modified);
		    if (modified || (*ptePtr & VM_MODIFIED_BIT)) {
			vec[i] = 4;
			dprintf("Vm_Mincore: modified at %x\n", checkAddr);
		    } else if (referenced || (*ptePtr & VM_REFERENCED_BIT)) {
			vec[i] = 3;
			dprintf("Vm_Mincore: referenced at %x\n", checkAddr);
		    } else {
			vec[i] = 2;
			dprintf("Vm_Mincore: present at %x\n", checkAddr);
		    }
		} else {
		    vec[i] = 1;
		    dprintf("Vm_Mincore: absent at %x\n", checkAddr);
		}
	    } else {
		vec[i] = 0;
		dprintf("Vm_Mincore: gone at %x\n", checkAddr);
	    }
	}
	checkAddr += vm_PageSize;
	dprintf("Vm_Mincore: i=%d v[i]=%d\n",i,vec[i]);
    }
    status = Vm_CopyOut( len, (Address) vec, (Address) retVec);
    free((char *)vec);
    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * Vm_Mprotect --
 *
 *	Change protection of pages.
 *
 * Results:
 *	SUCCESS or error condition.
 *
 * Side effects:
 *	Changes protection.
 *
 *----------------------------------------------------------------------
 */

ENTRY ReturnStatus
Vm_Mprotect(startAddr, length, prot)
    Address	startAddr;	/* Requested starting virt-addr. */
    int		length;		/* Length of region. */
    int		prot;		/* Protection for region. */
{
    Vm_VirtAddr virtAddr;
    Vm_PTE          *ptePtr;
    Proc_ControlBlock       *procPtr;
    int i;
    int firstPage, lastPage;

    if (BADALIGN(startAddr) || BADALIGN(length)) {
	printf("Vm_Mprotect: Invalid start or offset\n");
	return VM_WRONG_SEG_TYPE;
    }
#if 0
    if (prot & ~(PROT_READ|PROT_WRITE)) {
	printf("Vm_Mprotect: Only read/write permissions allowed\n");
	return SYS_INVALID_ARG;
    }
#endif
    if (!(prot & PROT_READ)) {
	printf("Vm_Mprotect: can't remove read perms in this implementation\n");
	return SYS_INVALID_ARG;
    }
    procPtr = Proc_GetCurrentProc();
    VmVirtAddrParseUnlock(procPtr, startAddr, &virtAddr);
    firstPage = (unsigned int) startAddr >> vmPageShift;
    lastPage = ((unsigned int)(startAddr+length-1)) >> vmPageShift;
    if (firstPage < segOffset(&virtAddr)) {
	printf("First page is out of range\n");
	return SYS_INVALID_ARG; /* ENOMEM */
    }
    for (i=lastPage-firstPage, ptePtr =
	    VmGetAddrPTEPtr(&virtAddr, virtAddr.page);i>=0;
	    i--, VmIncPTEPtr(ptePtr,1), virtAddr.page++) {
	if (virtAddr.page >= segOffset(&virtAddr) + virtAddr.segPtr->ptSize) {
	    printf("Page %d out of range\n",virtAddr.page);
	    printf("Addr = %x, segOffset=%d, ptSize=%d, segType %d\n", startAddr,
		    segOffset(&virtAddr), virtAddr.segPtr->ptSize,
		    virtAddr.segPtr->type);
	    break;
	}
	if (prot & PROT_WRITE) {
	    *ptePtr &= ~VM_READ_ONLY_PROT;
	} else {
	    *ptePtr |= VM_READ_ONLY_PROT;
	}
	VmMach_SetPageProt(&virtAddr, *ptePtr);
    }
    return SUCCESS;
}
