/* 
 * vmOps.c --
 *
 *	Random Sprite virtual memory operations.
 *	XXX Maybe some of these routines should go in VmSubr.c.  The 
 *	routines for MIG calls should probably stay here.
 *
 * Copyright 1991 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that this copyright
 * notice appears in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /user5/kupfer/spriteserver/src/sprited/vm/RCS/vmOps.c,v 1.26 92/07/16 18:05:07 kupfer Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include <sprite.h>
#include <bstring.h>
#include <ckalloc.h>
#include <mach.h>
#include <mach_error.h>
#include <mach/mach_host.h>
#include <status.h>
#include <string.h>

#include <fs.h>
#include <fsutil.h>
#include <machCalls.h>
#include <proc.h>
#include <procMach.h>
#include <timer.h>
#include <utils.h>
#include <vm.h>
#include <vmInt.h>
#include <vmStat.h>
#include <user/vmTypes.h>

/* 
 * We currently require that a stack fault be within a certain 
 * distance of the existing stack.  We might want to relax this 
 * restriction later.  This is the number of bytes in the current 
 * limit. 
 */
#define VM_STACK_MAX_DISTANCE	(10 * 1024 * 1024) /* 10 MB */

Vm_Stat vmStat;			/* VM instrumentation */

#ifndef CLEAN
static vm_address_t copyBuffer; /* dummy char array for copyin/copyout
				 * performance testing  */
#endif

/* Forward references: */

static void AddMappedRange _ARGS_((Address startAddr, vm_size_t length,
		VmMappedSegment *mappedSegPtr));
static ReturnStatus CopyInInbandTest _ARGS_((int copyLength, 
		int inBufLength, Address inBuf));
static ReturnStatus CopyInOutTest _ARGS_((int command, int length,
		Address userBuffer));
static ReturnStatus CopyOutInbandTest _ARGS_((int copyLength, 
		int *outBufLengthPtr, Address *outBufPtr, 
		Boolean *outBufDeallocPtr));
static ReturnStatus CopyRegion _ARGS_((Proc_LockedPCB *fromProcPtr,
		Proc_LockedPCB *toProcPtr, vm_size_t regionSize,
		Address regionAddr)); 
static void GetMemorySize _ARGS_((void));
static vm_offset_t GetStackOffset _ARGS_((VmMappedSegment *stackInfoPtr,
		Address address));
static VmMappedSegment *FindMappedSegment _ARGS_((List_Links *segmentList,
		Vm_Segment *segPtr));
static ReturnStatus MakeAccessibleTest _ARGS_((int command, int length,
		Address userBuffer));
static void MakeCopyTestBuffer _ARGS_((void));
static ReturnStatus MapHeapPage _ARGS_((Proc_ControlBlock *procPtr,
		Address address, VmMappedSegment *heapInfoPtr));
static ReturnStatus MapIntoProcess _ARGS_((Proc_LockedPCB *procPtr,
		Vm_Segment *segPtr, vm_offset_t offset, vm_size_t numBytes,
		Address mapAddr, vm_prot_t protection));
static void NoteMappedSegment _ARGS_((Proc_LockedPCB *procPtr,
		Vm_Segment *segPtr, Address startAddress, vm_size_t length));
static Boolean OkayToExtendStack _ARGS_((Proc_LockedPCB *procPtr,
		Address address));
static void RememberSpecialSegment _ARGS_((Proc_LockedPCB *procPtr,
		VmMappedSegment *mapSegPtr));
static void UnmapPages _ARGS_((Proc_LockedPCB *procPtr, 
		VmMappedSegment *mappedSegPtr));
static ReturnStatus VmAddrRegion _ARGS_((Proc_LockedPCB *procPtr,
		Address startAddr, int *numBytesPtr,
		Vm_Segment **segPtrPtr, vm_offset_t *offsetPtr));


/*
 *----------------------------------------------------------------------
 *
 * Vm_Init --
 *
 *	Initialization for the VM module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Varied.
 *
 *----------------------------------------------------------------------
 */

void
Vm_Init()
{
    VmSegmentInit();
    VmPagerInit();
    GetMemorySize();
}


/*
 *----------------------------------------------------------------------
 *
 * GetMemorySize --
 *
 *	Record in vmStat how much physical memory is in the system.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetMemorySize()
{
    kern_return_t kernStatus;
    mach_msg_type_number_t infoCount;
    struct host_basic_info hostInfo;

    infoCount = HOST_BASIC_INFO_COUNT;
    kernStatus = host_info(mach_host_self(), HOST_BASIC_INFO,
			   (host_info_t)&hostInfo, &infoCount);
    if (kernStatus != KERN_SUCCESS) {
	panic("GetMemorySize failed: %s\n",
	      mach_error_string(kernStatus));
    }
    if (infoCount != HOST_BASIC_INFO_COUNT) {
	panic("GetMemorySize: expected %d words, got %d.\n",
	      HOST_BASIC_INFO_COUNT, infoCount);
    }

    vmStat.numPhysPages = hostInfo.memory_size / vm_page_size;
}


/*
 *----------------------------------------------------------------------
 *
 * Vm_MapFile --
 *
 *	Map the named file into the given process's address space.
 *	The user can grow the file by mapping it read-write and 
 *	specifying an offset+length that go beyond the current length 
 *	of the file.
 *
 * Results:
 *	Returns a Mach status code.  Fills in a Sprite status code and 
 *	the address that the file was mapped into if successful.
 *	Returns a Sprite status code of FS_NO_ACCESS if the file is to 
 *	be mapped read-only and the given offset plus length run off
 *	the end of the file.
 *
 * Side effects:
 *	Maps the requested portion of the file into the user's address 
 *	space.
 *
 *----------------------------------------------------------------------
 */

kern_return_t
Vm_MapFile(procPtr, fileName, readOnly, offset, length, statusPtr,
	   startAddrPtr)
    Proc_ControlBlock *procPtr;	/* the user process to map the file into */
    char *fileName;		/* name of file to map */
    boolean_t readOnly;		/* map the file read-only or read-write? */
    off_t offset;		/* where in the file to start mapping. 
				 * NB: not necessarily page-aligned */
    vm_size_t length;		/* how much of the file to map */
    ReturnStatus *statusPtr;	/* OUT: Sprite return status */
    Address *startAddrPtr;	/* OUT: where file was mapped to */
{
    Vm_Segment *segmentPtr;	/* segment for the file */
    kern_return_t kernStatus = KERN_SUCCESS;
    Fs_Stream *filePtr;		/* for creating the file, if need be */

    length = round_page(length);

    /* 
     * Verify that the user has adequate access to the file.  If the 
     * file doesn't exist and the user is requesting read-write 
     * access, try to create it.
     * XXX Should be smarter about the permissions on the newly 
     * created file.
     * XXX Do we have to create the new file here, or can we let
     * Vm_GetSharedSegment do that?  (Do we care?  Maybe this call should
     * just go away.)
     */
    *statusPtr = Fs_CheckAccess(fileName,
				(readOnly ? FS_READ : FS_READ | FS_WRITE),
				TRUE);
    if ((*statusPtr == GEN_ENOENT
	 || *statusPtr == FS_FILE_NOT_FOUND)
	&& !readOnly) {
	*statusPtr = Fs_Open(fileName, FS_READ | FS_WRITE | FS_CREATE,
			     FS_FILE, 0644, &filePtr);
	if (*statusPtr == SUCCESS) {
	    (void)Fs_Close(filePtr);
	}
    }
    if (*statusPtr != SUCCESS) {
	return KERN_SUCCESS;
    }

    /* 
     * Get a segment for the file and map it into the process address 
     * space. 
     */
    *statusPtr = Vm_GetSharedSegment(fileName, &segmentPtr);
    if (*statusPtr != SUCCESS) {
	return KERN_SUCCESS;
    }
    
    *startAddrPtr = 0;
    Proc_Lock(procPtr);
    kernStatus = Vm_MapSegment(Proc_AssertLocked(procPtr), segmentPtr,
			       readOnly, TRUE, (vm_offset_t)offset, length, 
			       startAddrPtr, statusPtr);
    Proc_Unlock(Proc_AssertLocked(procPtr));
    if (kernStatus != KERN_SUCCESS || *statusPtr != SUCCESS) {
	Vm_SegmentRelease(segmentPtr);
    }

    return kernStatus;
}


/*
 *----------------------------------------------------------------------
 *
 * Vm_MapSegment --
 *
 *	Map a segment into a user process's address space.
 *
 * Results:
 *	Returns a Mach status code.  Fills in a Sprite status code and 
 *	the address that the file was mapped into if successful.
 *
 * Side effects:
 *	If successful, the requested portion of the file is mapped
 *	into the process's address space, and the reference for the
 *	segment is consumed.
 *
 *----------------------------------------------------------------------
 */

kern_return_t
Vm_MapSegment(lockedProcPtr, segPtr, readOnly, anywhere, offset, length,
	     startAddrPtr, statusPtr)
    Proc_LockedPCB *lockedProcPtr; /* the user process to map the file into */
    Vm_Segment *segPtr;		/* the segment to map */
    boolean_t readOnly;		/* map the file read-only or read-write? */
    boolean_t anywhere;		/* map the file anywhere, or at a 
				 * specific address? */
    vm_offset_t offset;		/* where in the segment to start mapping. 
				 * NB: not necessarily page-aligned */
    vm_size_t length;		/* how much of the segment to map */
    Address *startAddrPtr;	/* IN: where file should be mapped to
				 * OUT: where it was mapped to */
    ReturnStatus *statusPtr;	/* OUT: Sprite return status */
{
    vm_prot_t protectCode;	/* Mach protection code for the segment */
    kern_return_t kernStatus = KERN_SUCCESS;
    Proc_ControlBlock *procPtr = (Proc_ControlBlock *)lockedProcPtr;

    /* 
     * Set the Sprite status flag to success now, so that we don't have to 
     * remember to do it later.
     */
    *statusPtr = SUCCESS;

    protectCode = (readOnly ? VM_PROT_READ : VM_PROT_READ |
		   VM_PROT_WRITE);
    if (segPtr->type == VM_CODE) {
	protectCode |= VM_PROT_EXECUTE;
    }

    VmSegmentLock(segPtr);

    /* 
     * Check whether the segment is long enough.  If not and the mapping 
     * is read-only, return an error.  If the mapping is read-write, 
     * expand the segment. 
     */
    if (offset + length > segPtr->size) {
	if (readOnly) {
	    *statusPtr = FS_NO_ACCESS;
	    goto mapError;
	} else {
	    segPtr->size = offset + length;
	}
    }

    /* 
     * Try to map the segment into the process.
     */
    kernStatus = vm_map(procPtr->taskInfoPtr->task,
			(vm_address_t *)startAddrPtr, length, 0, anywhere,
			segPtr->requestPort, offset, FALSE, protectCode,
			protectCode, VM_INHERIT_NONE);
    if (kernStatus != KERN_SUCCESS) {
	printf("Vm_MapSegment: can't map file ``%s'' into user space: %s\n",
	       segPtr->swapFileName,
	       mach_error_string(kernStatus));
	goto mapError;
    }
    /* 
     * If this is the first usage of the segment, mark its control 
     * port so that we know the kernel has been told about it.
     */
    if (segPtr->controlPort == MACH_PORT_NULL) {
	segPtr->controlPort = MACH_PORT_DEAD;
    }


    /* 
     * Remember that the segment is used by the process.
     */
    VmSegmentUnlock(segPtr);
    NoteMappedSegment(Proc_AssertLocked(procPtr), segPtr,
		      (Address)*startAddrPtr, length);
    return KERN_SUCCESS;

 mapError:
    VmSegmentUnlock(segPtr);
    return kernStatus;
}


/*
 *----------------------------------------------------------------------
 *
 * Vm_CleanupTask --
 *
 *	Clean up VM information for a dead, locked process.  Should 
 *	only be called if the process's task is about to be nuked.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees the segments belonging to the process.
 *
 *----------------------------------------------------------------------
 */

void
Vm_CleanupTask(procPtr)
    Proc_LockedPCB *procPtr;	/* the dead process */
{
    Vm_ReleaseMappedSegments(procPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Vm_NewProcess --
 *
 *	Set up VM information for a new, locked process.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes the list of VM segments that are mapped into the 
 *	process. 
 *
 *----------------------------------------------------------------------
 */

void
Vm_NewProcess(vmInfoPtr)
    Vm_TaskInfo *vmInfoPtr;
{
    List_Init(&vmInfoPtr->mappedListHdr);
    vmInfoPtr->execHeapPages = 0;
    vmInfoPtr->execStackPages = 0;
    vmInfoPtr->codeInfoPtr = NULL;
    vmInfoPtr->heapInfoPtr = NULL;
    vmInfoPtr->stackInfoPtr = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Vm_ReleaseMappedSegments --
 *
 *	Release all the segments in a process's mapped segment list.  
 *	The process should already be locked.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The segments in the list are released, and memory for the list 
 *	is freed.  The memory potentially mapped by the segment is 
 *	deallocated from the process's address space (if the task isn't
 *	already gone).  The pointers to special segments for the process
 *	are cleared.
 *
 *----------------------------------------------------------------------
 */

void
Vm_ReleaseMappedSegments(procPtr)
    Proc_LockedPCB *procPtr;	/* the process that is losing the segments */
{
    List_Links *mappedSegList;
    List_Links *itemPtr;
    VmMappedSegment *mappedSegPtr;
    mach_port_t task;		/* the Mach task for the process */

    if (procPtr->pcb.taskInfoPtr == NULL) {
	printf("ReleaseMappedSegments: no task information.\n");
	return;
    }

    mappedSegList = &procPtr->pcb.taskInfoPtr->vmInfo.mappedListHdr;
    while (!List_IsEmpty(mappedSegList)) {
	itemPtr = List_First(mappedSegList);
	mappedSegPtr = (VmMappedSegment *)itemPtr;
	List_Remove(itemPtr);
	task = procPtr->pcb.taskInfoPtr->task;
	if (task != MACH_PORT_NULL) {
	    (void)vm_deallocate(task, (vm_address_t)mappedSegPtr->mappedAddr,
				mappedSegPtr->length);
	}
	Vm_SegmentRelease(mappedSegPtr->segPtr);
	ckfree(mappedSegPtr);
    }

    procPtr->pcb.taskInfoPtr->vmInfo.codeInfoPtr = NULL;
    procPtr->pcb.taskInfoPtr->vmInfo.heapInfoPtr = NULL;
    procPtr->pcb.taskInfoPtr->vmInfo.stackInfoPtr = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Vm_CleanupSharedFile --
 *
 *	Unmap any VM_SHARED segments backed by the given file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The segment is removed from the process's list of mapped segments.  
 *	Any memory backed by the file is unmapped from the process.
 *
 *----------------------------------------------------------------------
 */

void
Vm_CleanupSharedFile(procPtr, streamPtr)
    Proc_ControlBlock *procPtr;	/* the process to check */
    Fs_Stream *streamPtr;	/* the file to look for */
{
    List_Links *mappedSegList;
    List_Links *itemPtr;
    VmMappedSegment *mappedSegPtr;
    Vm_Segment *segPtr;
    ReturnStatus status;
    Fs_Attributes fileAttributes; /* the attributes for streamPtr */

    status = VmGetAttrStream(streamPtr, &fileAttributes);
    if (status != SUCCESS) {
	printf("Vm_CleanupSharedFile: couldn't get attributes for %s: %s\n",
	       Fsutil_GetFileName(streamPtr), Stat_GetMsg(status));
	return;
    }

    Proc_Lock(procPtr);
    if (procPtr->taskInfoPtr == NULL) {
	printf("Vm_CleanupSharedFile: no task information for process %x.\n",
	       procPtr->processID);
	goto done;
    }

    mappedSegList = &procPtr->taskInfoPtr->vmInfo.mappedListHdr;
    LIST_FORALL(mappedSegList, itemPtr) {
	mappedSegPtr = (VmMappedSegment *)itemPtr;
	segPtr = mappedSegPtr->segPtr;
	if (segPtr->swapFileNumber != fileAttributes.fileNumber
		|| segPtr->swapFileDomain != fileAttributes.domain
		|| segPtr->swapFileServer != fileAttributes.serverID) {
	    continue;
	}
	/* 
	 * Don't unmap, say, the text segment just because somebody does 
	 * "cp /sprite/cmds/cp foo".
	 */
	if (mappedSegPtr->segPtr->type != VM_SHARED) {
	    continue;
	}
	List_Remove(itemPtr);
	UnmapPages(Proc_AssertLocked(procPtr), mappedSegPtr);
	Vm_SegmentRelease(mappedSegPtr->segPtr);
	ckfree(mappedSegPtr);
    }

 done:
    Proc_Unlock(Proc_AssertLocked(procPtr));
}


/*
 *----------------------------------------------------------------------
 *
 * UnmapPages --
 *
 *	Unmap pages belonging to the given mapped segment.  This function 
 *	is needed because the address range in Vm_MappedSegment assumes 
 *	that the entire range is mapped by that segment, which is a bogus 
 *	assumption.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
UnmapPages(procPtr, mappedSegPtr)
    Proc_LockedPCB *procPtr;	/* the process to unmap from */
    VmMappedSegment *mappedSegPtr; /* the segment to unmap pages from */
{
    mach_port_t task;		/* the Mach task for the process */
    Address startAddr;		/* first potential address to unmap */
    Address endAddr;		/* last address + 1 (page) */
    vm_size_t pageSize;
    Vm_Segment *testSegPtr;	/* segment that backs a given page */
    vm_offset_t dummyOffset;	/* offset into testSegPtr; unused */
    ReturnStatus status;
    
    task = procPtr->pcb.taskInfoPtr->task;
    if (task == MACH_PORT_NULL) {
	return;
    }
    startAddr = mappedSegPtr->mappedAddr;
    endAddr = startAddr + mappedSegPtr->length;

    /* 
     * Check each page in the region to verify that it belongs to the given 
     * segment.  Probably slow, but avoids duplication of tedious code.
     */

    for (; startAddr < endAddr; startAddr += vm_page_size) {
	pageSize = vm_page_size;
	status = VmAddrRegion(procPtr, startAddr, (int *)&pageSize,
			      &testSegPtr, &dummyOffset);
	if (status != SUCCESS || testSegPtr != mappedSegPtr->segPtr) {
	    continue;
	}
	(void)vm_deallocate(task, (vm_address_t)startAddr, vm_page_size);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NoteMappedSegment --
 *
 *	Record a segment mapping for a process.  The process should be 
 *	locked. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 * 	If the segment isn't already mapped into the process, adds an 
 * 	element to the per-process list of mapped segments.  Either 
 * 	way, the reference to the segment is consumed.
 *
 *----------------------------------------------------------------------
 */

static void
NoteMappedSegment(procPtr, segPtr, startAddress, length)
    Proc_LockedPCB *procPtr;	/* the process affected */
    Vm_Segment *segPtr;		/* the segment being added */
    Address startAddress;	/* where the mapping starts */
    vm_size_t length;		/* how many bytes in the mapping */
{
    VmMappedSegment *mapSegPtr;
    List_Links *segmentList = &procPtr->pcb.taskInfoPtr->vmInfo.mappedListHdr;

    /* 
     * If the segment is already mapped into the process, then we 
     * free up the given reference to the segment.  If the segment 
     * isn't already mapped, the reference goes into the 
     * VmMappedSegment structure.
     */

    mapSegPtr = FindMappedSegment(segmentList, segPtr);
    if (mapSegPtr != NULL) {
	AddMappedRange(startAddress, length, mapSegPtr);
	Vm_SegmentRelease(mapSegPtr->segPtr);
    } else {
	mapSegPtr = (VmMappedSegment *)ckalloc(sizeof(VmMappedSegment));
	if (mapSegPtr == NULL) {
	    panic("NoteMappedSegment: out of memory.\n");
	}

	List_InitElement((List_Links *)mapSegPtr);
	mapSegPtr->segPtr = segPtr;
	mapSegPtr->mappedAddr = startAddress;
	mapSegPtr->length = length;

	List_Insert((List_Links *)mapSegPtr,
		    LIST_ATREAR(segmentList));

	RememberSpecialSegment(procPtr, mapSegPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * FindMappedSegment --
 *
 *	Find the mapped segment structure in the given list that 
 *	refers to the given segment.
 *
 * Results:
 *	Returns a pointer to the mapped segment structure if found, 
 *	NULL if not.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static VmMappedSegment *
FindMappedSegment(segmentList, segPtr)
    List_Links *segmentList;	/* list to look in */
    Vm_Segment *segPtr;		/* the segment to find */
{
    List_Links *itemPtr;
    VmMappedSegment *mappedSegPtr;

    LIST_FORALL(segmentList, itemPtr) {
	mappedSegPtr = (VmMappedSegment *)itemPtr;
	if (mappedSegPtr->segPtr == segPtr) {
	    return mappedSegPtr;
	}
    }

    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * AddMappedRange --
 *
 *	Update the information about how much memory is mapped by a 
 *	particular segment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If the given range extends the range recorded for the segment, 
 *	the record range is updated.
 *
 *----------------------------------------------------------------------
 */

static void
AddMappedRange(startAddr, length, mappedSegPtr)
    Address startAddr;		/* start of new range */
    vm_size_t length;		/* number of bytes in the new range */
    VmMappedSegment *mappedSegPtr; /* structure to update (in/out) */
{
    Address oldNextAddr;	/* next address after the segment 
				 * (last address plus one) */
    Address newNextAddr;	/* new next address after the segment */

    oldNextAddr = mappedSegPtr->mappedAddr + mappedSegPtr->length;
    if (startAddr + length > oldNextAddr) {
	newNextAddr = startAddr + length;
    } else {
	newNextAddr = oldNextAddr;
    }

    if (startAddr < mappedSegPtr->mappedAddr) {
	mappedSegPtr->mappedAddr = startAddr;
    }

    mappedSegPtr->length = newNextAddr - mappedSegPtr->mappedAddr;
}


/*
 *----------------------------------------------------------------------
 *
 * RememberSpecialSegment --
 *
 *	Remember in the pcb the handle for a new code, heap, or stack 
 *	segment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If the given segment is code, heap, or stack, sets the 
 *	corresponding pointer in the pcb.
 *
 *----------------------------------------------------------------------
 */

static void
RememberSpecialSegment(procPtr, mapSegPtr)
    Proc_LockedPCB *procPtr;	/* the process that's getting the segment */
    VmMappedSegment *mapSegPtr;	/* the segment being added to the process */
{
    switch (mapSegPtr->segPtr->type) {
    case VM_HEAP:
	if (procPtr->pcb.taskInfoPtr->vmInfo.heapInfoPtr != NULL) {
	    panic("RememberSpecialSegment: multiple heaps.\n");
	}
	procPtr->pcb.taskInfoPtr->vmInfo.heapInfoPtr = mapSegPtr;
	break;
    case VM_CODE:
	if (procPtr->pcb.taskInfoPtr->vmInfo.codeInfoPtr != NULL) {
	    panic("RememberSpecialSegment: multiple code segments.\n");
	}
	procPtr->pcb.taskInfoPtr->vmInfo.codeInfoPtr = mapSegPtr;
	break;
    case VM_STACK:
	if (procPtr->pcb.taskInfoPtr->vmInfo.stackInfoPtr != NULL) {
	    panic("RememberSpecialSegment: multiple stacks.\n");
	}
	procPtr->pcb.taskInfoPtr->vmInfo.stackInfoPtr = mapSegPtr;
	break;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Vm_ExtendStack --
 *
 * 	Try to extend the process's stack to include the given address.
 *
 * Results:
 * 	Returns SUCCESS if we were able to extend the stack.
 *
 * Side effects:
 *	If successful, the process's stack segment is mapped to cover 
 *	all pages between the current stack and the page containing 
 *	"address". 
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Vm_ExtendStack(lockedProcPtr, address)
    Proc_LockedPCB *lockedProcPtr; /* the process to look at */
    Address address;		/* the address to include */
{
    Proc_ControlBlock *procPtr = (Proc_ControlBlock *)lockedProcPtr;
    Address mapAddr;		/* where to start the new mapping */
    vm_offset_t segOffset;	/* offset for mapAddr */
    vm_size_t newMapLength;	/* bytes in new mapping */
    VmMappedSegment *stackInfoPtr; /* handle on the stack segment */
    kern_return_t kernStatus;
    ReturnStatus status = SUCCESS;
    
    stackInfoPtr = procPtr->taskInfoPtr->vmInfo.stackInfoPtr;
    if (stackInfoPtr == NULL) {
	panic("Vm_ExtendStack: no stack.\n");
    }
    if (!OkayToExtendStack(Proc_AssertLocked(procPtr), address)) {
	return FAILURE;
    }

    /* 
     * Vm_MapSegment consumes a reference to the segment if it 
     * succeeds, so get an additional reference to the segment.
     */
    Vm_SegmentAddRef(stackInfoPtr->segPtr);

    mapAddr = Vm_TruncPage(address);
    segOffset = GetStackOffset(stackInfoPtr, mapAddr);
    newMapLength = stackInfoPtr->mappedAddr - mapAddr;
    kernStatus = Vm_MapSegment(Proc_AssertLocked(procPtr),
			       stackInfoPtr->segPtr, FALSE, FALSE,
			       segOffset, newMapLength, &mapAddr, &status);
    if (kernStatus != KERN_SUCCESS) {
	printf("Vm_ExtendStack: couldn't map new region: %s\n",
	       mach_error_string(kernStatus));
	status = Utils_MapMachStatus(kernStatus);
    }
    if (status != SUCCESS) {
	Vm_SegmentRelease(stackInfoPtr->segPtr);
    }

    /* 
     * If the kernel successfully mapped the range, but not where we 
     * asked it to, it means either that (1) we screwed up or (2) the 
     * process has been talking to non-Sprite external pagers.  So 
     * complain, but don't panic.
     */
    if (status == SUCCESS && mapAddr != Vm_TruncPage(address)) {
	printf("Vm_ExtendStack: kernel mapped at 0x%x",
	       mapAddr);
	printf(", we wanted 0x%x\n", Vm_TruncPage(address));
	kernStatus = vm_deallocate(procPtr->taskInfoPtr->task,
				   (vm_address_t)mapAddr, newMapLength);
	if (kernStatus != KERN_SUCCESS) {
	    panic("Vm_ExtendStack: couldn't deallocate region: %s\n",
		   mach_error_string(kernStatus));
	}
	status = FAILURE;
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * OkayToExtendStack --
 *
 *	Check whether it's okay to extend the stack segment of the 
 *	given process to include the given address.
 *
 * Results:
 *	TRUE if it's okay, FALSE if not.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Boolean
OkayToExtendStack(lockedProcPtr, address)
    Proc_LockedPCB *lockedProcPtr; /* the process to check */
    Address address;		/* the address we'd like to put into 
				 * the stack */
{
    Proc_ControlBlock *procPtr = (Proc_ControlBlock *)lockedProcPtr;
    VmMappedSegment *stackInfoPtr; /* info about the process's stack */
    Address stackBottom;	/* current boundary address for the stack */
    Address pastStack;		/* last addr. plus one for stack */
    List_Links *itemPtr;
    VmMappedSegment *mappedSegPtr;
    Address segTop;		/* last address mapped in a segment */

    stackInfoPtr = procPtr->taskInfoPtr->vmInfo.stackInfoPtr;
    stackBottom = stackInfoPtr->mappedAddr;
    pastStack = stackInfoPtr->mappedAddr + stackInfoPtr->length;

    /* 
     * We shouldn't get an exception for addresses that are already 
     * mapped by the stack.
     */
    if (stackBottom <= address && address <= pastStack - sizeof(long)) {
	panic("OkayToExtendStack: the address is already in the stack.\n");
    }

    /*
     * We can't (or won't) extend the stack if 
     * - the faulting address runs past the high end of the stack.  (We 
     *   verified above that the address isn't in the stack.  If it's 
     *   not below the stack, it must be above the stack).
     * - the faulting address is too far below the stack.
     * - the faulting address can't possibly be in the stack because 
     *   it's lower than the lowest possible stack address.
     */
    if (stackBottom <= address) {
	return FALSE;
    }
    if (stackBottom - address > VM_STACK_MAX_DISTANCE) {
	return FALSE;
    }
    if (address < stackInfoPtr->segPtr->typeInfo.stackInfo.baseAddr) {
	return FALSE;
    }

    /* 
     * Walk through the list of mapped segments.  If there are any 
     * segments between "address" and the current stack, the stack 
     * can't be extended.  I suppose we could assume that no segments 
     * appear at addresses above the stack, but it's cheap to not make 
     * that assumption.
     */
    
    LIST_FORALL(&procPtr->taskInfoPtr->vmInfo.mappedListHdr, itemPtr) {
	mappedSegPtr = (VmMappedSegment *)itemPtr;
	segTop = mappedSegPtr->mappedAddr + mappedSegPtr->length - 1;
	if (address <= segTop && segTop < stackBottom) {
	    return FALSE;
	}
    }

    return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * GetStackOffset --
 *
 *	Convert an address in a process to the offset in the process's 
 *	stack segment.  The process should be locked.
 *
 * Results:
 *	Returns the offset for the page containing the address.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static vm_offset_t
GetStackOffset(stackInfoPtr, address)
    VmMappedSegment *stackInfoPtr; /* handle on the stack */
    Address address;		/* the address in memory; should be in 
				 * range of the stack */
{
    Address stackBase;		/* lowest possible address for the stack */

    /* 
     * The address should be page-aligned already, but in case it 
     * isn't... 
     */
    address = Vm_TruncPage(address);

    stackBase = stackInfoPtr->segPtr->typeInfo.stackInfo.baseAddr;
    if (address < stackBase) {
	panic("GetStackOffset: address not in stack.\n");
    }
    
    return address - stackBase;
}


/*
 *----------------------------------------------------------------------
 *
 * VmAddrParse --
 *
 *	"parse" an address into a segment and an offset in the segment.  
 *	The name derives from the routine VmVirtAddrParse in native Sprite, 
 *	which performs a similar function.
 *
 * Results:
 *	Returns a status code.  If successful, segment pointer and offset 
 *	are filled in.  If the given range falls off the end of the 
 *	segment, the byte count is truncated to give the actual number of 
 *	bytes available.  VM_NO_SEGMENTS means that the given address 
 *	isn't mapped (XXX maybe a new VM_NOT_MAPPED status code would be 
 *	better?).  VM_SEG_NOT_FOUND means that the address is mapped,
 *	but the memory object for it isn't a Sprite VM segment.
 *	Note: this is the same as VmAddrRegion.
 *
 * Side effects:
 *	If successful, the reference count on the segment pointer is 
 *	incremented (the caller is responsible for freeing it).
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
VmAddrParse(procPtr, startAddr, numBytesPtr, segPtrPtr, offsetPtr)
    Proc_ControlBlock *procPtr;	/* the process to do the lookup for */
    Address startAddr;		/* the address to check */
    int *numBytesPtr;		/* IN: the number of bytes to check.
				 * OUT: the number of bytes available */
    Vm_Segment **segPtrPtr;	/* OUT: the segment that startAddr falls in */
    vm_offset_t *offsetPtr;	/* OUT: the segment offset for startAddr */
{
    int currBytes;		/* number of bytes in current region */
    vm_offset_t currOffset;	/* offset for current region */
    vm_offset_t expectedOffset = 0; /* expected offset for current region */
    Vm_Segment *segPtr;		/* segment for the current region */
    int totalBytes;		/* total number of bytes found */
    ReturnStatus status;

    /* 
     * Map the given address to a segment, if possible.  Mach may have 
     * broken the requested memory range into multiple regions, all 
     * referring to successive portions of the same segment.  So if our 
     * first attempt doesn't map the entire requested range, try again.
     */

    *segPtrPtr = segPtr = NULL;
    totalBytes = 0;
    Proc_Lock(procPtr);

    while (totalBytes < *numBytesPtr) {
	/* 
	 * Compute the number of bytes we're still trying to map.
	 */
	currBytes = *numBytesPtr - totalBytes;
	status = VmAddrRegion(Proc_AssertLocked(procPtr), startAddr,
			      &currBytes, &segPtr, &currOffset);
	/* 
	 * If we didn't find anything, punt.
	 */
	if (status != SUCCESS && *segPtrPtr == NULL) {
	    Proc_Unlock(Proc_AssertLocked(procPtr));
	    return status;
	}
	/* 
	 * If this is the first time through the loop, record the starting 
	 * offset, the winning segment, and the size of the region.
	 * 
	 * Otherwise, see if we've reached unmapped memory or have bumped
	 * into a different segment (or a non-contiguous range of the
	 * current segment).  In these cases, return what we've got.
	 * 
	 * Otherwise, we've found a continuation of the current segment:
	 * update the number of bytes found and repeat.
	 */
	if (*segPtrPtr == NULL) {
	    *offsetPtr = currOffset;
	    expectedOffset = currOffset + currBytes;
	    *segPtrPtr = segPtr;
	    Vm_SegmentAddRef(segPtr);
	    totalBytes = currBytes;
	} else if (status != SUCCESS) {
	    break;
	} else if (segPtr != *segPtrPtr || currOffset != expectedOffset) {
	    break;
	} else {
	    expectedOffset += currBytes;
	    totalBytes += currBytes;
	}

	startAddr += currBytes;
    }

    Proc_Unlock(Proc_AssertLocked(procPtr));
    if (totalBytes < *numBytesPtr) {
	*numBytesPtr = totalBytes;
    }
    return SUCCESS;
}


/*
 *----------------------------------------------------------------------
 *
 * VmAddrRegion --
 *
 * 	Helper routine for VmAddrParse.  Find the segment and offset for 
 * 	the given user address, but don't worry about whether there are 
 * 	multiple regions.
 *
 * Results:
 *	Same as VmAddrParse.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
VmAddrRegion(procPtr, startAddr, numBytesPtr, segPtrPtr, offsetPtr)
    Proc_LockedPCB *procPtr;	/* the process to do the lookup for */
    Address startAddr;		/* the address to check */
    int *numBytesPtr;		/* IN: the number of bytes to check.
				 * OUT: the number of bytes available */
    Vm_Segment **segPtrPtr;	/* OUT: the segment that startAddr falls in */
    vm_offset_t *offsetPtr;	/* OUT: the segment offset for startAddr */
{
    VmMappedSegment *mappedSegPtr = NULL; /* mapped segment in the process */
    vm_size_t regionSize;	/* number of bytes in region */
    memory_object_name_t segmentName; /* name port for the region's segment */
    Address regionStartAddr;	/* where the region actually starts */
    vm_offset_t regionOffset;	/* offset in segment for start of region */
    vm_prot_t protection;	/* ignored */
    vm_prot_t maxProtection;	/* ignored */
    vm_inherit_t inheritance;	/* ignored */
    Boolean shared;		/* ignored */
    kern_return_t kernStatus;
    ReturnStatus status = SUCCESS;
    Address touchPage;		/* if we force the segment to be
				 * initialized, this is what vm_read gives */  
    vm_size_t touchPageSize;	/* number of bytes in touchPage */
    int junk;

    /* 
     * Ask Mach what segment the address is in.  Then check whether any of 
     * the process's mapped segments match the one Mach returned to us.  
     */
    
    regionStartAddr = startAddr;
    kernStatus = vm_region(procPtr->pcb.taskInfoPtr->task,
			   (vm_address_t *)&regionStartAddr, &regionSize,
			   &protection, &maxProtection, &inheritance, &shared, 
			   &segmentName, &regionOffset);
    if (kernStatus == KERN_NO_SPACE) {
	status = VM_NO_SEGMENTS;
	goto done;
    }
    /* 
     * Check whether the region found is the region we actually asked for.
     */
    if (regionStartAddr > startAddr) {
	status = VM_NO_SEGMENTS;
	goto done;
    }

    mappedSegPtr = VmSegByName((List_Links *)&procPtr->pcb.taskInfoPtr->
				         vmInfo.mappedListHdr,
			       segmentName);
    if (mappedSegPtr == NULL) {
	/* 
	 * We couldn't find a matching segment, but maybe that's because
	 * the segment hasn't been fully initialized.  Try reading from the
	 * requested address, to force the segment to be initialized.
	 */
	(void)vm_read(procPtr->pcb.taskInfoPtr->task,
		      (vm_address_t)regionStartAddr, vm_page_size,
		      (pointer_t *)&touchPage,
		      (mach_msg_type_number_t *)&touchPageSize);
	junk = *(int *)touchPage;
#ifdef lint
	junk = junk;
#endif
	vm_deallocate(mach_task_self(), (vm_address_t)touchPage,
		      touchPageSize);
	mappedSegPtr = VmSegByName((List_Links *)&procPtr->pcb.taskInfoPtr->
				         vmInfo.mappedListHdr,
				   segmentName);
	if (mappedSegPtr != NULL) {
	    vmStat.forcedInits++;
	} else {
	    status = VM_SEG_NOT_FOUND;
	    goto done;
	}
    }

    *segPtrPtr = mappedSegPtr->segPtr;

    if (startAddr + *numBytesPtr > regionStartAddr + regionSize) {
	*numBytesPtr = regionStartAddr + regionSize - startAddr;
    }

    *offsetPtr = (vm_offset_t)(startAddr - regionStartAddr) + regionOffset;

 done:
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Vm_Fork --
 *
 *	Copy the VM of the parent process to the child.
 *
 * Results:
 *	Returns a status code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Vm_Fork(procPtr, parentProcPtr)
    Proc_ControlBlock *procPtr;	/* the process to get the new VM */
    Proc_ControlBlock *parentProcPtr; /* the process to copy it from */
{
    ReturnStatus status = SUCCESS;
    kern_return_t kernStatus;
    Address addr;		/* address used to walk through the parent */
    Address regionAddr;		/* starting address of a region */
    mach_port_t segmentName;	/* name port for a region's segment */
    vm_prot_t protection;	/* protection on the region */
    vm_prot_t maxProtection;	/* maximum protection on the region */
    vm_size_t regionSize;	/* number of bytes in the region */
    vm_inherit_t inheritance;	/* inheritance code for the region */
    boolean_t shared;		/* is the region shared? */
    vm_offset_t offset;		/* region's starting offset in its segment */
    VmMappedSegment *parentHeapPtr; /* parent process's heap */
    Vm_Segment *childHeapPtr;	/* child process's heap */
    VmMappedSegment *parentStackPtr; /* parent process's stack segment */
    Vm_Segment *childStackPtr;	/* child process's stack segment */
    VmMappedSegment *mappedSegPtr; /* mapped segment in the parent */
    Vm_Segment *segPtr;		/* segment to map into the child */
    Boolean suspended = FALSE;	/* did we suspend the parent's task? */
    Time startTime, endTime, totalTime;	/* instrumentation */
    Time startSubstep, endSubstep;

    parentHeapPtr = NULL;	/* lint */
    parentStackPtr = NULL;

    if (sys_CallProfiling) {
	Timer_GetTimeOfDay(&startTime, (int *)NULL, (Boolean *)NULL);
    } else {
	startTime = time_ZeroSeconds;
    }

    Proc_Lock(parentProcPtr);
    Proc_Lock(procPtr);

    if (parentProcPtr->taskInfoPtr == NULL) {
	status = VM_CORRUPTED;
	goto bailOut;
    }
    parentHeapPtr = parentProcPtr->taskInfoPtr->vmInfo.heapInfoPtr;
    parentStackPtr = parentProcPtr->taskInfoPtr->vmInfo.stackInfoPtr;
    if (parentHeapPtr == NULL) {
	printf("Vm_Fork: process %x doesn't have a heap.\n",
	       parentProcPtr->processID);
	status = VM_CORRUPTED;
	goto bailOut;
    }
    if (parentStackPtr == NULL) {
	printf("Vm_Fork: process %x doesn't have a stack.\n",
	       parentProcPtr->processID);
	status = VM_CORRUPTED;
	goto bailOut;
    }
    childHeapPtr = NULL;
    childStackPtr = NULL;

    /* 
     * Suspend the parent's task, so that other threads in it don't make 
     * any changes while we're working.
     */
    kernStatus = task_suspend(parentProcPtr->taskInfoPtr->task);
    if (kernStatus != KERN_SUCCESS) {
	status = Utils_MapMachStatus(kernStatus);
	goto bailOut;
    }
    suspended = TRUE;

    /* 
     * Walk through the parent process's VM.  For every region, find the 
     * mapped Sprite segment and set up a corresponding region in the
     * child.
     */
    
    addr = 0;
    while (addr < procMach_MaxUserAddr) {
	regionAddr = addr;
	if (sys_CallProfiling) {
	    Timer_GetTimeOfDay(&startSubstep, (int *)NULL, (Boolean *)NULL);
	} else {
	    startSubstep = time_ZeroSeconds;
	}
	kernStatus = vm_region(parentProcPtr->taskInfoPtr->task,
			       (vm_address_t *)&regionAddr, &regionSize,
			       &protection, &maxProtection, &inheritance,
			       &shared, &segmentName, &offset);
	if (sys_CallProfiling && !Time_EQ(startSubstep, time_ZeroSeconds)) {
	    Timer_GetTimeOfDay(&endSubstep, (int *)NULL, (Boolean *)NULL);
	    Time_Subtract(endSubstep, startSubstep, &endSubstep);
	    Time_Add(endSubstep, vmStat.findRegionTime,
		     &vmStat.findRegionTime);
	}
	/* 
	 * If there aren't any more mapped regions, we're done.
	 */
	if (kernStatus == KERN_NO_SPACE) {
	    break;
	}
	if (sys_CallProfiling) {
	    Timer_GetTimeOfDay(&startSubstep, (int *)NULL, (Boolean *)NULL);
	} else {
	    startSubstep = time_ZeroSeconds;
	}
	mappedSegPtr = VmSegByName((List_Links *)&parentProcPtr->
				       taskInfoPtr->vmInfo.mappedListHdr,
				   segmentName);
	if (sys_CallProfiling && !Time_EQ(startSubstep, time_ZeroSeconds)) {
	    Timer_GetTimeOfDay(&endSubstep, (int *)NULL, (Boolean *)NULL);
	    Time_Subtract(endSubstep, startSubstep, &endSubstep);
	    Time_Add(endSubstep, vmStat.segLookupTime,
		     &vmStat.segLookupTime);
	}
	/* 
	 * If the region isn't mapped by a Sprite segment, complain and let 
	 * the user program deal with it.  Otherwise, map the segment into 
	 * the child process.  If it's a heap or stack segment, map a new 
	 * segment rather than the parent's segment, and copy the bits from
	 * the parent to the child.
	 */
	if (mappedSegPtr == NULL) {
	    printf("Vm_Fork: unrecognized segment in process %x, ",
		   parentProcPtr->processID);
	    printf("addresses [0x%x..0x%x)\n", regionAddr,
		   regionAddr + regionSize);
	} else {
	    segPtr = mappedSegPtr->segPtr;
	    if (sys_CallProfiling) {
		Timer_GetTimeOfDay(&startSubstep, (int *)NULL,
				   (Boolean *)NULL); 
	    } else {
		startSubstep = time_ZeroSeconds;
	    }
	    if (segPtr == parentHeapPtr->segPtr) {
		if (childHeapPtr == NULL) {
		    status = VmSegmentCopy(parentHeapPtr, &childHeapPtr);
		}
		segPtr = childHeapPtr;
	    } else if (segPtr == parentStackPtr->segPtr) {
		if (childStackPtr == NULL) {
		    status = VmSegmentCopy(parentStackPtr, &childStackPtr);
		}
		segPtr = childStackPtr;
	    }
	    if (sys_CallProfiling &&
		!Time_EQ(startSubstep, time_ZeroSeconds)) {
		Timer_GetTimeOfDay(&endSubstep, (int *)NULL, (Boolean *)NULL);
		Time_Subtract(endSubstep, startSubstep, &endSubstep);
		Time_Add(endSubstep, vmStat.segCopyTime,
			 &vmStat.segCopyTime);
	    }
	    if (status == SUCCESS) {
		status = MapIntoProcess(Proc_AssertLocked(procPtr), segPtr,
					offset, regionSize, regionAddr,
					protection);
		if (status != SUCCESS) {
		    printf("%s: couldn't map region [0x%x..0x%x) into pid %x:",
			   "Vm_Fork", regionAddr, regionAddr+regionSize, 
			   procPtr->processID);
		    printf(" %s\n", Stat_GetMsg(status));
		}
	    }
	    if (status == SUCCESS &&
		(segPtr->type == VM_HEAP || segPtr->type == VM_STACK)) {
		if (sys_CallProfiling) {
		    Timer_GetTimeOfDay(&startSubstep, (int *)NULL,
				       (Boolean *)NULL);
		} else {
		    startSubstep = time_ZeroSeconds;
		}
		status = CopyRegion(Proc_AssertLocked(parentProcPtr),
				    Proc_AssertLocked(procPtr), regionSize,
				    regionAddr);
		if (sys_CallProfiling &&
		    !Time_EQ(startSubstep, time_ZeroSeconds)) {
		    Timer_GetTimeOfDay(&endSubstep, (int *)NULL,
				       (Boolean *)NULL);
		    Time_Subtract(endSubstep, startSubstep, &endSubstep);
		    Time_Add(endSubstep, vmStat.regionCopyTime,
			     &vmStat.regionCopyTime);
		}
		if (status == SUCCESS) {
		    vmStat.pagesCopied += regionSize / vm_page_size;
		} else {
		    printf("Vm_Fork: couldn't copy region [0x%x..0x%x) from ",
			   regionAddr, regionAddr+regionSize);
		    printf("parent %x to child %x: %s\n",
			   parentProcPtr->processID, procPtr->processID,
			   Stat_GetMsg(status));
		}
	    }
	}

	if (status != SUCCESS) {
	    break;
	}
	addr = regionAddr + regionSize;
    }

    /* 
     * When we created the child's heap and stack segments, we were given 
     * references that we now need to free (MapIntoProcess made its own 
     * reference). 
     */
    if (childHeapPtr != NULL) {
	VmSegmentLock(childHeapPtr);
	childHeapPtr->flags &= ~VM_COPY_TARGET;
	VmSegmentUnlock(childHeapPtr);
	Vm_SegmentRelease(childHeapPtr);
	childHeapPtr = NULL;
    }
    if (childStackPtr != NULL) {
	VmSegmentLock(childStackPtr);
	childStackPtr->flags &= ~VM_COPY_TARGET;
	VmSegmentUnlock(childStackPtr);
	Vm_SegmentRelease(childStackPtr);
	childStackPtr = NULL;
    }
    VmSegmentLock(parentHeapPtr->segPtr);
    parentHeapPtr->segPtr->flags &= ~VM_COPY_SOURCE;
    VmSegmentUnlock(parentHeapPtr->segPtr);
    VmSegmentLock(parentStackPtr->segPtr);
    parentStackPtr->segPtr->flags &= ~VM_COPY_SOURCE;
    VmSegmentUnlock(parentStackPtr->segPtr);

    /* 
     * Verify that we were able to set up the minimum VM information.
     */
    if (status == SUCCESS &&
	    (procPtr->taskInfoPtr->vmInfo.codeInfoPtr == NULL ||
	     procPtr->taskInfoPtr->vmInfo.heapInfoPtr == NULL ||
	     procPtr->taskInfoPtr->vmInfo.stackInfoPtr == NULL)) {
	printf("Vm_Fork: missing segment(s) from pid %x.\n",
	       parentProcPtr->processID);
	status = VM_CORRUPTED;
	goto bailOut;
    }

    if (status == SUCCESS && sys_CallProfiling &&
	!Time_EQ(startTime, time_ZeroSeconds)) {
	Timer_GetTimeOfDay(&endTime, (int *)NULL, (Boolean *)NULL);
	Time_Subtract(endTime, startTime, &totalTime);
	Time_Add(vmStat.forkTime, totalTime, &vmStat.forkTime);
    }

 bailOut:
    if (suspended) {
	(void)task_resume(parentProcPtr->taskInfoPtr->task);
    }
    Proc_Unlock(Proc_AssertLocked(parentProcPtr));
    Proc_Unlock(Proc_AssertLocked(procPtr));
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * MapIntoProcess --
 *
 *	Map the given segment into the given process.
 *
 * Results:
 *	Returns a status code.  Note that the segment may be mapped into 
 *	the process even if the return status isn't SUCCESS.
 *
 * Side effects:
 *	If the segment is mapped, its reference count is increased.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
MapIntoProcess(procPtr, segPtr, offset, numBytes, mapAddr, protection)
    Proc_LockedPCB *procPtr;	/* the process to get the new mapping */
    Vm_Segment *segPtr;		/* the segment to map in */ 
    vm_offset_t offset;		/* the segment offset to start mapping at */
    vm_size_t numBytes;		/* the number of bytes to map */
    Address mapAddr;		/* the address to start mapping at */
    vm_prot_t protection;	/* the desired protection level */
{
    Address resultAddr;		/* the address actually mapped at */
    ReturnStatus status;
    kern_return_t kernStatus;

    Vm_SegmentAddRef(segPtr);
    resultAddr = mapAddr;
    kernStatus = Vm_MapSegment(procPtr, segPtr, !(protection & VM_PROT_WRITE),
			       FALSE, offset, numBytes, &resultAddr, &status);
    if (kernStatus != KERN_SUCCESS) {
	status = Utils_MapMachStatus(kernStatus);
    }
    if (status != SUCCESS) {
	Vm_SegmentRelease(segPtr);
	return status;
    }
    if (resultAddr != mapAddr) {
	printf("%s: didn't get requested address for pid %x.\n",
	       "MapIntoProcess", procPtr->pcb.processID);
	return VM_CORRUPTED;
    }

    return SUCCESS;
}


/*
 *----------------------------------------------------------------------
 *
 * CopyRegion --
 *
 *	Copy the given region from one user process to another.
 *
 * Results:
 *	Returns a Sprite status code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
CopyRegion(fromProcPtr, toProcPtr, regionSize, regionAddr)
    Proc_LockedPCB *fromProcPtr; /* the process to copy from */
    Proc_LockedPCB *toProcPtr;	/* the process to copy to */
    vm_size_t regionSize;	/* the number of bytes to copy */
    Address regionAddr;		/* where to start copying */
{
    kern_return_t kernStatus;
    vm_address_t copyBuffer;
    mach_msg_type_number_t copyCount;
    ReturnStatus status = SUCCESS;

    if (regionAddr != Vm_TruncPage(regionAddr)) {
	panic("CopyRegion: address 0x%x not page-aligned.\n",
	      regionAddr);
    }
    if (regionSize != trunc_page(regionSize)) {
	panic("CopyRegion: copy size 0x%x not page-aligned.\n",
	      regionSize);
    }

    kernStatus = vm_read(fromProcPtr->pcb.taskInfoPtr->task,
			 (vm_address_t)regionAddr, regionSize, &copyBuffer,
			 &copyCount);
    if (kernStatus != KERN_SUCCESS) {
	printf("CopyRegion: couldn't read region at 0x%x: %s\n",
	       regionAddr, mach_error_string(kernStatus));
	return Utils_MapMachStatus(kernStatus);
    }
    if (copyCount != regionSize) {
	panic("CopyRegion: expected 0x%x bytes, got 0x%x.\n",
	      regionSize, copyCount);
    }

    kernStatus = vm_write(toProcPtr->pcb.taskInfoPtr->task,
			  (vm_address_t)regionAddr, copyBuffer, copyCount);
    if (kernStatus != KERN_SUCCESS) {
	printf("CopyRegion: couldn't write region at 0x%x: %s\n",
	       regionAddr, mach_error_string(kernStatus));
	status = Utils_MapMachStatus(kernStatus);
    }
    (void)vm_deallocate(mach_task_self(), copyBuffer, copyCount);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * VmSegByName --
 *
 *	Look in a list of mapped segments for the one with the given Mach
 *	name port.  The process that owns this list should be locked.
 *
 * Results:
 *	Returns a mapped segment handle from the given list, or NULL if the 
 *	segment couldn't be found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

VmMappedSegment *
VmSegByName(segmentList, segmentName)
    List_Links *segmentList;	/* list to look in */
    mach_port_t segmentName;	/* name of desired segment */
{
    List_Links *itemPtr;
    VmMappedSegment *mappedSegPtr;

    LIST_FORALL(segmentList, itemPtr) {
	mappedSegPtr = (VmMappedSegment *)itemPtr;
	if (mappedSegPtr->segPtr->namePort == segmentName) {
	    return mappedSegPtr;
	}
    }

    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Vm_CreateVA --
 *
 *	Validate a range of user addresses.  If necessary, map in the heap 
 *	segment at those addresses.
 *
 * Results:
 *	Returns VM_WRONG_SEG_TYPE if some part of the address range belongs
 *	to some segment other than the heap (or a non-Sprite memory
 *	object).  Otherwise returns SUCCESS.
 *
 * Side effects:
 *	Might allocate some memory even if a failure code is returned.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Vm_CreateVA(startAddr, requestedBytes)
    Address startAddr;		/* Address to validate from. */
    vm_size_t requestedBytes;	/* Number of bytes to validate. */
{
    Address currAddr;		/* start of current region */
    vm_size_t numBytes;		/* number of bytes in current region */
    vm_size_t bytesLeft;	/* number of bytes left to check */
    Vm_Segment *segPtr;		/* segment for current region */
    Vm_Segment *heapPtr;	/* the process's heap segment */
    VmMappedSegment *heapInfoPtr;
    vm_offset_t offset;		/* offset into heap for current address */
    ReturnStatus status = SUCCESS;
    Proc_ControlBlock *procPtr;

    procPtr = Proc_GetCurrentProc();
    heapInfoPtr = procPtr->taskInfoPtr->vmInfo.heapInfoPtr;
    heapPtr = heapInfoPtr->segPtr;

    /* 
     * Verify that the requested range of addresses is at least potentially 
     * in the heap.
     */
    if (startAddr < heapInfoPtr->mappedAddr ||
	startAddr + requestedBytes > (heapInfoPtr->mappedAddr +
				      heapPtr->size)) {
	return VM_WRONG_SEG_TYPE;
    }

    currAddr = Vm_TruncPage(startAddr);
    bytesLeft = Vm_RoundPage(startAddr + requestedBytes) - currAddr;

    /* 
     * Portions of the requested region might already be mapped, with other 
     * portions unmapped.  So do this in a loop.  We'll go region-by-region
     * through the requested address range, with currAddr pointing to the
     * start of the region.
     */
    
    do {
	numBytes = bytesLeft;
	status = VmAddrParse(procPtr, currAddr, (int *)&numBytes, &segPtr,
			     &offset);
	switch (status) {
	case SUCCESS:
	    /* 
	     * The region is already mapped.  Verify that it's mapped by 
	     * the heap, and move on to the next region.
	     */
	    Vm_SegmentRelease(segPtr);
	    if (segPtr == heapPtr) {
		bytesLeft -= numBytes;
		currAddr += numBytes;
	    } else {
		status = VM_WRONG_SEG_TYPE;
		printf("Vm_CreateVA: wrong segment at address 0x%x: %s\n",
		       currAddr, Vm_SegmentName(segPtr)); /* DEBUG */
		printf("Segment type is %d\n", segPtr->type); /* DEBUG */
	    }
	    break;
	case VM_SEG_NOT_FOUND:
	    /* 
	     * The region is mapped, but not by a Sprite segment.
	     */
	    printf("Vm_CreateVA: address 0x%x is mapped by non-Sprite seg.\n",
		   currAddr);	/* DEBUG */
	    status = VM_WRONG_SEG_TYPE;
	    break;
	case VM_NO_SEGMENTS:
	    /* 
	     * The region is unmapped.  Map in one page of the heap and 
	     * move along.
	     */
	    status = MapHeapPage(procPtr, currAddr, heapInfoPtr);
	    if (status == SUCCESS) {
		currAddr += vm_page_size;
		bytesLeft -= vm_page_size;
	    }
	    if (status != SUCCESS) {
		printf("Vm_CreateVA: couldn't map heap page at 0x%x\n",
		       currAddr); /* DEBUG */
	    }
	    break;
	}
    } while (status == SUCCESS && currAddr < startAddr + requestedBytes);

    if (status != SUCCESS) {
	printf("Vm_CreateVA(0x%x, 0x%x) failed: %s\n",
	       startAddr, requestedBytes, Stat_GetMsg(status)); /* DEBUG */
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * MapHeapPage --
 *
 *	Map one page from the heap starting at the given user address.
 *
 * Results:
 *	Returns SUCCESS if it worked, some failure code if it didn't.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
MapHeapPage(procPtr, address, heapInfoPtr)
    Proc_ControlBlock *procPtr;	/* user process to get the new mapping */
    Address address;		/* where to map it */
    VmMappedSegment *heapInfoPtr; /* the user process's heap */
{
    Address resultAddr;		/* where the page actually got mapped to */
    vm_offset_t offset;		/* offset into the heap */
    kern_return_t kernStatus;
    ReturnStatus status = SUCCESS;

    /* 
     * Vm_MapSegment consumes a reference to the segment if it 
     * succeeds, so get an additional reference to the segment.
     */
    Vm_SegmentAddRef(heapInfoPtr->segPtr);

    resultAddr = address;
    offset = address - heapInfoPtr->mappedAddr;

    Proc_Lock(procPtr);
    kernStatus = Vm_MapSegment(Proc_AssertLocked(procPtr),
			       heapInfoPtr->segPtr, FALSE, FALSE,
			       offset, vm_page_size, &resultAddr, &status);
    Proc_Unlock(Proc_AssertLocked(procPtr));
    if (kernStatus != KERN_SUCCESS) {
	printf("MapHeap: couldn't map new page: %s\n",
	       mach_error_string(kernStatus)); /* DEBUG */
	status = Utils_MapMachStatus(kernStatus);
    }
    if (status != SUCCESS) {
	Vm_SegmentRelease(heapInfoPtr->segPtr);
    }
    if (status == SUCCESS && resultAddr != address) {
	panic("MapHeap: Vm_MapSegment lied when it said it had succeeded.\n");
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * VmGetAttrStream --
 *
 *	Like Fs_GetAttrStream, but retries in case of RPC errors.
 *
 * Results:
 *	See Fs_GetAttrStream.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
VmGetAttrStream(streamPtr, attrPtr)
    Fs_Stream *streamPtr;	/* the stream we want attributes for */
    Fs_Attributes *attrPtr;	/* OUT: the stream's attributes */
{
    Boolean retry;
    ReturnStatus status;

    do {
	retry = FALSE;
	status = Fs_GetAttrStream(streamPtr, attrPtr);
	if (status != SUCCESS && !sys_ShuttingDown
		&& Fsutil_RecoverableError(status)) {
	    status = Fsutil_WaitForHost(streamPtr, 0, status);
	    if (status == SUCCESS) {
		retry = TRUE;
	    } else {
		printf("VmGetAttrStream: recovery failed for %s: %s\n",
		       Fsutil_GetFileName(streamPtr),
		       Stat_GetMsg(status));   
	    }
	}
    } while (retry);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Vm_Cmd --
 *
 *	Random VM-related operations made available to user programs.
 *
 * Results:
 *	SYS_ARG_NOACCESS: the given argument refers to inaccessible user 
 *	    memory.
 *	GEN_INVALID_ARG: the given command was unrecognized.
 *
 * Side effects:
 *	Depends on the command.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Vm_Cmd(command, length, arg)
    int command;		/* what command */
    vm_size_t length;		/* buffer size, if needed */
    Address arg;		/* additional information for the command */
{
    ReturnStatus status = SUCCESS;
    int numPhysPages;

    switch (command) {
    case VM_GET_STATS: {
	if (length > sizeof(vmStat)) {
	    length = sizeof(vmStat);
	}
	status = Vm_CopyOut(length, (Address)&vmStat, arg);
	break;
    }
    case VM_CLEAR_COUNTERS:
	numPhysPages = vmStat.numPhysPages;
	bzero(&vmStat, sizeof(vmStat));
	vmStat.numPhysPages = numPhysPages;
	break;
#ifndef CLEAN
    case VM_DO_COPY_IN:
    case VM_DO_COPY_OUT:
	status = CopyInOutTest(command, length, arg);
	break;
    case VM_DO_MAKE_ACCESS_IN:
    case VM_DO_MAKE_ACCESS_OUT:
	status = MakeAccessibleTest(command, length, arg);
	break;
#endif /* CLEAN */
    default:
	status = GEN_INVALID_ARG;
	break;
    }

    return status;
}


#ifndef CLEAN
/*
 *----------------------------------------------------------------------
 *
 * CopyInOutTest --
 *
 *	Invoke Vm_CopyIn or Vm_CopyOut on a dummy array (for performance 
 *	tuning).
 *
 * Results:
 *	Returns GEN_INVALID_ARG if the given length is greater than the 
 *	maximum.  Otherwise returns the error code from the copy routine.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
CopyInOutTest(command, length, userBuffer)
    int command;		/* copy in or out */
    int length;			/* number of bytes to copy */
    Address userBuffer;		/* address of user's buffer */
{
    ReturnStatus status;

    MakeCopyTestBuffer();

    if (length > VM_DO_COPY_MAX_SIZE) {
	status = GEN_INVALID_ARG;
    } else {
	if (command == VM_DO_COPY_IN) {
	    status = Vm_CopyIn(length, userBuffer, (Address)copyBuffer);
	} else {
	    status = Vm_CopyOut(length, (Address)copyBuffer, userBuffer);
	}
    }

    return status;
}
#endif /* CLEAN */


#ifndef CLEAN
/*
 *----------------------------------------------------------------------
 *
 * MakeAccessibleTest --
 *
 *	Invoke Vm_MakeAccessible and bcopy on a dummy array (for
 *	performance tuning).
 *
 * Results:
 *	GEN_INVALID_ARG: the given length is greater than the 
 *	  maximum.
 *	GEN_FAILURE: Vm_MakeAccessible wouldn't map the user's buffer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
MakeAccessibleTest(command, length, userBuffer)
    int command;		/* copy in or out */
    int length;			/* number of bytes to copy */
    Address userBuffer;		/* address of user's buffer */
{
    ReturnStatus status = SUCCESS;
    int numBytes;		/* number of bytes actually mapped */

    MakeCopyTestBuffer();

    if (length > VM_DO_COPY_MAX_SIZE) {
	status = GEN_INVALID_ARG;
    } else {
	if (command == VM_DO_MAKE_ACCESS_IN) {
	    Vm_MakeAccessible(VM_READONLY_ACCESS, length, userBuffer,
			      &numBytes, &userBuffer);
	    if (userBuffer == (Address)NIL) {
		status = GEN_FAILURE;
		goto done;
	    }
	    bcopy(userBuffer, (Address)copyBuffer, length);
	    Vm_MakeUnaccessible(userBuffer, numBytes);
	} else {
	    Vm_MakeAccessible(VM_READWRITE_ACCESS, length, userBuffer,
			      &numBytes, &userBuffer);
	    if (userBuffer == (Address)NIL) {
		status = GEN_FAILURE;
		goto done;
	    }
	    bcopy((Address)copyBuffer, userBuffer, length);
	    Vm_MakeUnaccessible(userBuffer, numBytes);
	}
    }

 done:
    return status;
}
#endif /* CLEAN */


/*
 *----------------------------------------------------------------------
 *
 * MakeCopyTestBuffer --
 *
 *	Allocate the local buffer for copy tests, if it doesn't already 
 *	exist.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
MakeCopyTestBuffer()
{
    kern_return_t kernStatus;

    if (copyBuffer == 0) {
	printf("Vm_Cmd: allocating buffer for perf test.  Please rerun.\n");
	kernStatus = vm_allocate(mach_task_self(), &copyBuffer,
				 round_page(VM_DO_COPY_MAX_SIZE),
				 TRUE);
	if (kernStatus != KERN_SUCCESS) {
	    panic("%s: couldn't allocate dummy buffer for copy test: %s\n",
		  "MakeCopyTestBuffer", mach_error_string(kernStatus));
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * VmCmdInband --
 *
 *	Like Vm_Cmd, but supports calls that pass arguments directly, 
 *	rather than using copyin/copyout.
 *
 * Results:
 *	Returns the usual Sprite status code.  Fills in the return buffer
 *	pointer and byte count.
 *
 * Side effects:
 *	Depends on the command.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
VmCmdInband(command, option, inBufLength, inBuf, outBufLengthPtr, outBufPtr,
	    outBufDeallocPtr)
    int command;		/* the command to execute */
    int option;			/* user-specified option (e.g., buffer 
				 * length) */ 
    int inBufLength;		/* bytes in inBuf */
    Address inBuf;		/* input buffer */
    int *outBufLengthPtr;	/* OUT: number of result bytes */
    Address *outBufPtr;		/* OUT: the results */
    Boolean *outBufDeallocPtr;	/* OUT: dealloc outBuf after send */
{
    ReturnStatus status = SUCCESS;

    *outBufLengthPtr = 0;
    *outBufPtr = NULL;
    *outBufDeallocPtr = FALSE;

    switch (command) {
    case VM_DO_COPY_IN_INBAND:
	status = CopyInInbandTest(option, inBufLength, inBuf);
	break;
    case VM_DO_COPY_OUT_INBAND:
	status = CopyOutInbandTest(option, outBufLengthPtr, outBufPtr,
				   outBufDeallocPtr);
	break;
    default:
	status = GEN_INVALID_ARG;
	break;
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * CopyInInbandTest --
 *
 *	Copy from the given buffer to the copy buffer.
 *
 * Results:
 *	Returns a Sprite status code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
CopyInInbandTest(copyLength, inBufLength, inBuf)
    int copyLength;
    int inBufLength;		/* number of bytes to copy */
    Address inBuf;		/* buffer to copy from */
{
    ReturnStatus status = SUCCESS;

    MakeCopyTestBuffer();

    if (copyLength > VM_DO_COPY_MAX_SIZE || copyLength > inBufLength) {
	status = GEN_INVALID_ARG;
    } else {
	bcopy(inBuf, (_VoidPtr)copyBuffer, copyLength);
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * CopyOutInbandTest --
 *
 *	Make a copy of the copy buffer and give a pointer to it.
 *
 * Results:
 *	Returns a Sprite status code.  Fills in a pointer to a buffer, the 
 *	allocated size of the buffer, and a flag telling MIG whether to 
 *	deallocate the buffer when the reply message has been sent.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
CopyOutInbandTest(copyLength, outBufLengthPtr, outBufPtr, outBufDeallocPtr)
    int copyLength;		/* number of bytes to copy out */
    int *outBufLengthPtr;	/* OUT: number of bytes allocated to outBuf */
    Address *outBufPtr;		/* OUT: result buffer */
    Boolean *outBufDeallocPtr;	/* OUT: whether to deallocate outBuf */
{
    vm_address_t outBuf;	/* temporary output buffer */
    vm_size_t bufSize;		/* number of bytes in output buffer */
    kern_return_t kernStatus;
    ReturnStatus status = SUCCESS;

    *outBufPtr = 0;
    *outBufLengthPtr = bufSize = 0;
    *outBufDeallocPtr = FALSE;

    if (copyLength > VM_DO_COPY_MAX_SIZE) {
	status = GEN_INVALID_ARG;
    } else {
	/* 
	 * Allocate a temporary buffer and copy the copy buffer into it.  
	 * Pass the temporary buffer back to the caller, and tell MIG to 
	 * deallocate the buffer when it's no longer needed.
	 */
	bufSize = round_page(copyLength);
	outBuf = 0;
	kernStatus = vm_allocate(mach_task_self(), &outBuf, bufSize, TRUE);
	if (kernStatus != KERN_SUCCESS) {
	    printf("CopyOutInbandTest: can't allocate temp buffer: %s\n",
		   mach_error_string(kernStatus));
	    status = Utils_MapMachStatus(kernStatus);
	} else {
	    bcopy((_VoidPtr)copyBuffer, (_VoidPtr)outBuf, copyLength);
	    *outBufPtr = (Address)outBuf;
	    *outBufLengthPtr = bufSize;
	    *outBufDeallocPtr = TRUE;
	}
    }

    return status;
}
