/*
 * vmSun.c -
 *
 *     	This file contains all hardware-dependent vm C routines for Sun2's, 3's 
 *	and 4's.  I will not attempt to explain the Sun mapping hardware in 
 *	here.  See the sun architecture manuals for details on
 *	the mapping hardware.
 *
 * Copyright 1990 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header$ SPRITE (Berkeley)";
#endif not lint

#include <sprite.h>
#include <vmSunConst.h>
#include <machMon.h>
#include <vm.h>
#include <vmInt.h>
#include <vmMach.h>
#include <vmMachInt.h>
#include <list.h>
#include <mach.h>
#include <proc.h>
#include <sched.h>
#include <stdlib.h>
#include <sync.h>
#include <sys.h>
#include <dbg.h>
#include <net.h>
#include <stdio.h>
#include <bstring.h>
#include <recov.h>

#if (MACH_MAX_NUM_PROCESSORS == 1) /* uniprocessor implementation */
#undef MASTER_LOCK
#undef MASTER_UNLOCK
#define MASTER_LOCK(x) DISABLE_INTR()
#define MASTER_UNLOCK(x) ENABLE_INTR()
#else

/*
 * The master lock to synchronize access to pmegs and context.
 */
static Sync_Semaphore vmMachMutex;
static Sync_Semaphore *vmMachMutexPtr = &vmMachMutex;
#endif

#ifndef sun4c
/*
 * Macros to translate from a virtual page to a physical page and back.
 * For the sun4c, these are no longer macros, since they are much more
 * complicated due to physical memory no longer being contiguous.  For
 * speed perhaps someday they should be converted to complicated macros
 * instead of functions.
 */
#define VirtToPhysPage(pfNum) ((pfNum) << VMMACH_CLUSTER_SHIFT)
#define PhysToVirtPage(pfNum) ((pfNum) >> VMMACH_CLUSTER_SHIFT)
#endif

extern int debugVmStubs; /* Unix compat debug flag. */

/*
 * Convert from page to hardware segment, with correction for
 * any difference between virtAddrPtr offset and segment offset.
 * (This difference will only happen for shared segments.)
*/
#define PageToOffSeg(page,virtAddrPtr) (PageToSeg((page)- \
	segOffset(virtAddrPtr)+(virtAddrPtr->segPtr->offset)))

extern	Address	vmStackEndAddr;

static int GetNumPages _ARGS_((void));
static int GetNumValidPages _ARGS_((Address virtAddr));
static void FlushValidPages _ARGS_ ((Address virtAddr));
#ifdef sun4c
static int VirtToPhysPage _ARGS_((int pfNum));
static int PhysToVirtPage _ARGS_((int pfNum));
#endif /* sun4c */
static void VmMachSetSegMapInContext _ARGS_((unsigned int context,
	Address addr, unsigned int pmeg));
static void MMUInit _ARGS_((int firstFreeSegment));
ENTRY static void CopySegData _ARGS_((register Vm_Segment *segPtr,
	register VmMach_SegData *oldSegDataPtr,
	register VmMach_SegData *newSegDataPtr));
static void SegDelete _ARGS_((Vm_Segment *segPtr));
INTERNAL static int PMEGGet _ARGS_((Vm_Segment  *softSegPtr, int hardSegNum,
	Boolean flags));
INTERNAL static void PMEGFree _ARGS_((int pmegNum));
ENTRY static Boolean PMEGLock _ARGS_ ((register VmMach_SegData *machPtr,
	int segNum));
INTERNAL static void SetupContext _ARGS_((register Proc_ControlBlock
	*procPtr));
static void InitNetMem _ARGS_((void));
ENTRY static void WriteHardMapSeg _ARGS_((VmMach_ProcData *machPtr));
static void PageInvalidate _ARGS_((register Vm_VirtAddr *virtAddrPtr,
	unsigned int virtPage, Boolean segDeletion));
INTERNAL static void DevBufferInit _ARGS_((void));
static void VmMachTrap _ARGS_((void));
#ifndef sun4c
INTERNAL static void Dev32BitDMABufferInit _ARGS_((void));
#endif

/*----------------------------------------------------------------------
 * 
 * 			Hardware data structures
 *
 * Terminology: 
 *	1) Physical page frame: A frame that contains one hardware page.
 *	2) Virtual page frame:  A frame that contains VMMACH_CLUSTER_SIZE 
 *				hardware pages.
 *	3) Software segment: The segment structure used by the hardware
 *			     independent VM module.
 *	4) Hardware segment: A piece of a hardware context.
 *
 * A hardware context corresponds to a process's address space.  A context
 * is made up of many equal sized hardware segments.  The 
 * kernel is mapped into each hardware context so that the kernel has easy
 * access to user data.  One context (context 0) is reserved for use by
 * kernel processes.  The hardware contexts are defined by the array
 * contextArray which contains an entry for each context.  Each entry 
 * contains a pointer back to the process that is executing in the context 
 * and an array which is an exact duplicate of the hardware segment map for
 * the context.  The contexts are managed by keeping all contexts except for
 * the system context in a list that is kept in LRU order.  Whenever a context 
 * is needed the first context off of the list is used and the context is
 * stolen from the current process that owns it if necessary.
 *
 * PMEGs are allocated to software segments in order to allow pages to be mapped
 * into the segment's virtual address space. There are only a few hundred
 * PMEGs, which have to be shared by all segments.  PMEGs that have
 * been allocated to user segments can be stolen at any time.  PMEGs that have
 * been allocated to the system segment cannot be taken away unless the system
 * segment voluntarily gives it up.  In order to manage the PMEGs there are
 * two data structures.  One is an array of PMEG info structures that contains
 * one entry for each PMEG.  The other is an array stored with each software
 * segment struct that contains the PMEGs that have been allocated to the
 * software segment.  Each entry in the array of PMEG info structures
 * contains enough information to remove the PMEG from its software segment.
 * One of the fields in the PMEG info struct is the count of pages that have
 * been validated in the PMEG.  This is used to determine when a PMEG is no
 * longer being actively used.  
 *
 * There are two lists that are used to manage PMEGs.  The pmegFreeList 
 * contains all PMEGs that are either not being used or contain no valid 
 * page map entries; unused ones are inserted at the front of the list 
 * and empty ones at the rear.  The pmegInuseList contains all PMEGs
 * that are being actively used to map user segments and is managed as a FIFO.
 * PMEGs that are being used to map tbe kernel's VAS do not appear on the 
 * pmegInuseList. When a pmeg is needed to map a virtual address, first the
 * free list is checked.  If it is not empty then the first PMEG is pulled 
 * off of the list.  If it is empty then the first PMEG is pulled off of the
 * inUse list.  If the PMEG  that is selected is being used (either actively 
 * or inactively) then it is freed from the software segment that is using it.
 * Once the PMEG is freed up then if it is being allocated for a user segment
 * it is put onto the end of the pmegInuseList.
 *
 * Page frames are allocated to software segments even when there is
 * no PMEG to map it in.  Thus when a PMEG that was mapping a page needs to
 * be removed from the software segment that owns the page, the reference
 * and modify bits stored in the PMEG for the page must be saved.  The
 * array refModMap is used for this.  It contains one entry for each
 * virtual page frame.  Its value for a page frame or'd with the bits stored
 * in the PMEG (if any) comprise the referenced and modified bits for a 
 * virtual page frame.
 * 
 * IMPORTANT SYNCHRONIZATION NOTE:
 *
 * The internal data structures in this file have to be protected by a
 * master lock if this code is to be run on a multi-processor.  Since a
 * process cannot start executing unless VmMach_SetupContext can be
 * executed first, VmMach_SetupContext cannot context switch inside itself;
 * otherwise a deadlock will occur.  However, VmMach_SetupContext mucks with
 * contexts and PMEGS and therefore would have to be synchronized
 * on a multi-processor.  A monitor lock cannot be used because it may force
 * VmMach_SetupContext to be context switched.
 *
 * The routines in this file also muck with other per segment data structures.
 * Access to these data structures is synchronized by our caller (the
 * machine independent module).
 *
 *----------------------------------------------------------------------
 */

/*
 * Machine dependent flags for the flags field in the Vm_VirtAddr struct.
 * We are only allowed to use the second byte of the flags.
 *
 *	USING_MAPPED_SEG		The parsed virtual address falls into
 *					the mapping segment.
 */
#define	USING_MAPPED_SEG	0x100

/*
 * Macros to get to and from hardware segments and pages.
 */
#define PageToSeg(page) ((page) >> (VMMACH_SEG_SHIFT - VMMACH_PAGE_SHIFT))
#define SegToPage(seg) ((seg) << (VMMACH_SEG_SHIFT - VMMACH_PAGE_SHIFT))

#if (VMMACH_CLUSTER_SIZE == 1) 
/*
 * Macro to set all page map entries for the given virtual address.
 */
#define	SET_ALL_PAGE_MAP(virtAddr, pte) { \
	VmMachSetPageMap((Address)(virtAddr), (pte)); \
}
/*
 * Macro to flush all pages for the given virtual address.
 */
#define	FLUSH_ALL_PAGE(virtAddr) { \
	VmMachFlushPage((Address)(virtAddr)); \
}
#else
#define	SET_ALL_PAGE_MAP(virtAddr, pte) { \
    int	__i; \
    for (__i = 0; __i < VMMACH_CLUSTER_SIZE; __i++) { \
	VmMachSetPageMap((virtAddr) + __i * VMMACH_PAGE_SIZE_INT, (pte) + __i); \
    } \
}
#define	FLUSH_ALL_PAGE(virtAddr) { \
    int	__i; \
    for (__i = 0; __i < VMMACH_CLUSTER_SIZE; __i++) { \
	VmMachFlushPage((virtAddr) + __i * VMMACH_PAGE_SIZE_INT); \
    } \
}
#endif

/*
 * PMEG table entry structure.
 */
typedef struct {
    List_Links			links;		/* Links so that the pmeg */
              					/* can be in a list */
    struct VmMach_PMEGseg	segInfo;	/* Info on software segment. */
    int				pageCount;	/* Count of resident pages in
						 * this pmeg. */
    int				lockCount;	/* The number of times that
						 * this PMEG has been locked.*/
    int				flags;		/* Flags defined below. */
} PMEG;

/*
 * Flags to indicate the state of a pmeg.
 *
 *    PMEG_DONT_ALLOC	This pmeg should not be reallocated.  This is 
 *			when a pmeg cannot be reclaimed until it is
 *			voluntarily freed.
 *    PMEG_NEVER_FREE	Don't ever free this pmeg no matter what anybody says.
 */
#define	PMEG_DONT_ALLOC		0x1
#define	PMEG_NEVER_FREE		0x2

/*
 * Pmeg information.  pmegArray contains one entry for each pmeg.  pmegFreeList
 * is a list of all pmegs that aren't being actively used.  pmegInuseList
 * is a list of all pmegs that are being actively used.
 */

static	unsigned int	vmNumPmegs;
	unsigned int	vmPmegMask;
static	PMEG   		*pmegArray;
static	List_Links   	pmegFreeListHeader;
static	List_Links   	*pmegFreeList = &pmegFreeListHeader;
static	List_Links   	pmegInuseListHeader;
static	List_Links   	*pmegInuseList = &pmegInuseListHeader;

#ifdef sun4c
unsigned int	vmCacheLineSize;
unsigned int	vmCacheSize;
unsigned int	vmCacheShift;
#endif

/*
 * The context table structure.
 */
typedef struct VmMach_Context {
    List_Links			links;	 /* Links so that the contexts can be
					     in a list. */
    struct Proc_ControlBlock	*procPtr;	/* A pointer to the process
						 * table entry for the process
						 * that is running in this
						 * context. */
    VMMACH_SEG_NUM		map[VMMACH_NUM_SEGS_PER_CONTEXT];
					/* A reflection of the hardware context
					 * map. */
    unsigned int		context;/* Which context this is. */
    int				flags;	/* Defined below. */
} VmMach_Context;

/*
 * Context flags:
 *
 *     	CONTEXT_IN_USE	This context is used by a process.
 */
#define	CONTEXT_IN_USE	0x1

/*
 * Context information.  contextArray contains one entry for each context. 
 * contextList is a list of contexts in LRU order.
 */
#ifdef sun4c
static	unsigned int	vmNumContexts;
static	VmMach_Context	*contextArray;
#else
static	VmMach_Context	contextArray[VMMACH_NUM_CONTEXTS];
#endif
static	List_Links   	contextListHeader;
static	List_Links   	*contextList = &contextListHeader;

/*
 * Map containing one entry for each virtual page.
 */
static	VmMachPTE		*refModMap;

/*
 * Macro to get a pointer into a software segment's hardware segment table.
 */
#ifdef CLEAN
#define GetHardSegPtr(machPtr, segNum) \
    ((machPtr)->segTablePtr + (segNum) - (machPtr)->offset)
#else
#define GetHardSegPtr(machPtr, segNum) \
    ( ((unsigned)((segNum) - (machPtr)->offset) > (machPtr)->numSegs) ? \
    (panic("Invalid segNum\n"),(machPtr)->segTablePtr) : \
    ((machPtr)->segTablePtr + (segNum) - (machPtr)->offset) )
#endif

/*
 * Macro to check to see if address is in a hardware segment (PMEG) that
 * is used by file cache virtual addresses.
 * These kernel segment pmegs may be stolen.
 */

#define	_ROUND2SEG(x) ((unsigned int)(x)  & ~(VMMACH_SEG_SIZE-1))
#define	IN_FILE_CACHE_SEG(addr) 					     \
          ( ((unsigned int)(addr) >=					     \
		_ROUND2SEG(vmBlockCacheBaseAddr + (VMMACH_SEG_SIZE-1))) &&   \
	    ((unsigned int)(addr) < _ROUND2SEG(vmBlockCacheEndAddr)) )

/*
 * TRUE if stealing of file cache pmegs are permitted. Initialized to FALSE
 * for backward compat with old file cache code.
 */
Boolean vmMachCanStealFileCachePmegs = FALSE;

/*
 * The maximum amount of kernel code + data available.  This is set to however
 * big you want it to be to make sure that the kernel heap has enough memory
 * for all the file handles, etc.
 */
#ifdef sun2
int	vmMachKernMemSize = 2048 * 1024;
#endif
#ifdef sun3
int     vmMachKernMemSize = 8192 * 1024;
#endif
#ifdef sun4
#ifdef sun4c
int	vmMachKernMemSize = 32 * 1024 * 1024;
#else
int	vmMachKernMemSize = 40 * 1024 * 1024;
#endif
#endif

/*
 * The segment that is used to map a segment into a process's virtual address
 * space for cross-address-space copies.
 */
#define	MAP_SEG_NUM (((unsigned int) VMMACH_MAP_SEG_ADDR) >> VMMACH_SEG_SHIFT)

static	VmMach_SegData	*sysMachPtr;
Address			vmMachPTESegAddr;
Address			vmMachPMEGSegAddr;

#ifdef sun4c
/*
 * Structure for mapping virtual page frame numbers to physical page frame
 * numbers and back for each memory board.
 */
typedef struct	{
    unsigned int	endVirPfNum;	/* Ending virtual page frame number
					 * on board. */
    unsigned int	startVirPfNum;	/* Starting virtual page frame number
					 * on board. */
    unsigned int	physStartAddr;	/* Physical address of page frame. */
    unsigned int	physEndAddr;	/* End physical address of page frame.*/
} Memory_Board;

/*
 * Pointer to board after last configured Memory board structure.
 */
static	Memory_Board	*lastMemBoard;

/*
 * Memory_Board structures for each board in the system.  This array is sorted
 * by endVirPfNum.
 */
static	Memory_Board	Mboards[6];
#endif /* sun4c */

/*
 * vmMachHasVACache - TRUE if the machine has a virtual address cache.
 * vmMachHasHwFlush - TRUE if the machine has the hardware assist cache flush
 *		      option.
 */
Boolean	vmMachHasVACache	= TRUE;
Boolean	vmMachHasHwFlush	= FALSE;

/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_BootInit --
 *
 *      Do hardware dependent boot time initialization.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Hardware page map for the kernel is initialized.  Also the various size
 * 	fields are filled in.
 *
 * ----------------------------------------------------------------------------
 */
void
VmMach_BootInit(pageSizePtr, pageShiftPtr, pageTableIncPtr, kernMemSizePtr,
		numKernPagesPtr, maxSegsPtr, maxProcessesPtr)
    int	*pageSizePtr;
    int	*pageShiftPtr;
    int	*pageTableIncPtr;
    int	*kernMemSizePtr;
    int	*numKernPagesPtr;
    int	*maxSegsPtr;
    int *maxProcessesPtr;
{
    register Address	virtAddr;
    register int	i;
    int			kernPages;
    int			numPages;
#ifdef sun4c
    Mach_MemList	*memPtr;
    int			nextVframeNum, numFrames;
#endif

#if (MACH_MAX_NUM_PROCESSORS != 1) /* multiprocessor implementation */
    Sync_SemInitDynamic(&vmMachMutex, "Vm:vmMachMutex");
#endif

#ifdef sun4c
    /*
     * Initialize the physical to virtual page mappings, since memory isn't
     * contiguous.
     */
    lastMemBoard = Mboards;
    nextVframeNum = 0;
    if (romVectorPtr->v_romvec_version >= 2) {
	Mach_MemList mlist[128];
	int	 numMemList, i;

	numMemList = Mach_MonSearchProm("memory", "available", (char *) mlist,
					sizeof(mlist)) / sizeof(Mach_MemList);
	/*
	 * XXX - fix this
	 * Currently the VM module assumes that the kernel
	 * is in phsyically contiguous memory. Kernel page 0 is in 
	 * phsyical page zero (or at least startVirPfNum == 0). The
	 * Prom on the 4/75 returns memlist is reverse order with
	 * phsical zero last.
	 */
	for (i = numMemList - 1; i >= 0; --i) {
	    memPtr = mlist + i;
	    if (memPtr->size != 0) {
		numFrames = memPtr->size / VMMACH_PAGE_SIZE;
		lastMemBoard->startVirPfNum = nextVframeNum;
		lastMemBoard->endVirPfNum = nextVframeNum + numFrames;
		nextVframeNum += numFrames;
		lastMemBoard->physStartAddr =
		    memPtr->address >> VMMACH_PAGE_SHIFT_INT;
		lastMemBoard->physEndAddr = lastMemBoard->physStartAddr +
		    numFrames * VMMACH_CLUSTER_SIZE;
		lastMemBoard++;
	    }
	}
    } else {
	for (memPtr = *(romVectorPtr->availMemory);
		memPtr != (Mach_MemList *) 0;
		memPtr = memPtr->next) {
	    if (memPtr->size != 0) {
		numFrames = memPtr->size / VMMACH_PAGE_SIZE;
		lastMemBoard->startVirPfNum = nextVframeNum;
		lastMemBoard->endVirPfNum = nextVframeNum + numFrames;
		nextVframeNum += numFrames;
		lastMemBoard->physStartAddr =
		    memPtr->address >> VMMACH_PAGE_SHIFT_INT;
		lastMemBoard->physEndAddr = lastMemBoard->physStartAddr +
		    numFrames * VMMACH_CLUSTER_SIZE;
		lastMemBoard++;
	    }
	}
    }
    if (lastMemBoard == Mboards) {
	panic("No memory boards in system configuration.");
    }

    if (Mach_MonSearchProm("*", "vac-linesize", (char *)&vmCacheLineSize,
	    sizeof vmCacheLineSize) == -1) {
	vmCacheLineSize = VMMACH_CACHE_LINE_SIZE_60;
    }
    vmCacheSize = VMMACH_CACHE_SIZE;	/* for assembly code */
    {
	unsigned i = vmCacheLineSize;
	vmCacheShift = 0;
	while (i >>= 1) {
	    ++vmCacheShift;
	}
    }
    if (Mach_MonSearchProm("*", "vac_hwflush", (char *)&vmMachHasHwFlush,
	    sizeof vmMachHasHwFlush) == -1) {
	vmMachHasHwFlush = FALSE;
    }
    if (Mach_MonSearchProm("*", "mmu-npmg", (char *)&vmNumPmegs,
	    sizeof vmNumPmegs) == -1) {
	vmNumPmegs = VMMACH_NUM_PMEGS_60;
    }
#else /* sun4c */
    switch (Mach_GetMachineType()) {
	case SYS_SUN_4_110:
	    vmNumPmegs = VMMACH_NUM_PMEGS_110;
	    vmMachHasVACache = FALSE;
	    break;
	case SYS_SUN_4_260:
	    vmNumPmegs = VMMACH_NUM_PMEGS_260;
	    break;
	case SYS_SUN_4_330:
	    vmNumPmegs = VMMACH_NUM_PMEGS_330;
	    break;
	case SYS_SUN_4_470:
	    vmNumPmegs = VMMACH_NUM_PMEGS_470;
	    break;
	default:
	    panic("What sort of machine type is %x?\n", Mach_GetMachineType());
    }
#endif /* sun4c */
    vmPmegMask = vmNumPmegs - 1;
    
    kernPages = vmMachKernMemSize / VMMACH_PAGE_SIZE_INT;
    /*
     * Map all of the kernel memory that we might need one for one.  We know
     * that the monitor maps the first part of memory one for one but for some
     * reason it doesn't map enough.  We assume that the pmegs have been
     * mapped correctly.
     */
    for (i = 0, virtAddr = (Address)mach_KernStart; 
	i < kernPages;
	i++, virtAddr += VMMACH_PAGE_SIZE_INT) {
	if (VmMachGetSegMap(virtAddr) != VMMACH_INV_PMEG) {
	    VmMachPTE pte = VmMachGetPageMap(virtAddr);
	    if (pte & VMMACH_RESIDENT_BIT) { 
		pte &= VMMACH_PAGE_FRAME_FIELD;
#ifdef sun4
		pte |= VMMACH_DONT_CACHE_BIT;
#endif
		VmMachSetPageMap(virtAddr, 
		    (VmMachPTE)(VMMACH_KRW_PROT | VMMACH_RESIDENT_BIT | pte));
	    } else {
		printf("VmMach_BootInit: Last page ends at %x.\n", virtAddr);
		break;
	    }
	} else {
	    printf("VmMach_BootInit: Last segment ends at %x.\n", virtAddr);
	    break;
	}
    }

    /*
     * Do boot time allocation.
     */
    pmegArray = (PMEG *)Vm_BootAlloc(sizeof(PMEG) * vmNumPmegs);
#ifdef sun4c
    if (Mach_MonSearchProm("*", "mmu-nctx", (char *)&vmNumContexts,
	    sizeof vmNumContexts) == -1) {
	vmNumContexts = VMMACH_NUM_CONTEXTS_60;
    }
    contextArray = (VmMach_Context *)Vm_BootAlloc(sizeof(VmMach_Context) *
	    vmNumContexts);
#endif
    sysMachPtr = (VmMach_SegData *)Vm_BootAlloc(sizeof(VmMach_SegData) + 
	    (sizeof (VMMACH_SEG_NUM) * VMMACH_NUM_SEGS_PER_CONTEXT));
    numPages = GetNumPages();
    refModMap = (VmMachPTE *)Vm_BootAlloc(sizeof(VmMachPTE) * numPages);

    /*
     * Return lots of sizes to the machine independent module who called us.
     */
    *pageSizePtr = VMMACH_PAGE_SIZE;
    *pageShiftPtr = VMMACH_PAGE_SHIFT;
    *pageTableIncPtr = VMMACH_PAGE_TABLE_INCREMENT;
    *kernMemSizePtr = vmMachKernMemSize;
    *maxProcessesPtr = VMMACH_MAX_KERN_STACKS;
    *numKernPagesPtr = numPages;
    /* 
     * We don't care how many software segments there are so return -1 as
     * the max.
     */
    *maxSegsPtr = -1;
}


/*
 * ----------------------------------------------------------------------------
 *
 * GetNumPages --
 *
 *     Determine how many pages of physical memory there are.
 *     For the sun4c, this determines how many physical pages of memory
 *     are available after the prom has grabbed some.
 *
 * Results:
 *     The number of physical pages available.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
static int
GetNumPages()
{
#ifdef sun4c
    int	memory = 0;
    Mach_MemList	*memPtr;

    if (romVectorPtr->v_romvec_version >= 2) {	
	Mach_MemList mlist[128];
	int	 numMemList, i;

	numMemList = Mach_MonSearchProm("memory", "available", (char *) mlist,
					sizeof(mlist)) / sizeof(Mach_MemList);
	for (i = 0; i < numMemList; i++) {
	    memPtr = mlist + i;
	    memory += memPtr->size / VMMACH_PAGE_SIZE;
	}
    } else {
	for (memPtr = *(romVectorPtr->availMemory);
		memPtr != (Mach_MemList *) 0;
		memPtr = memPtr->next) {
	    memory += memPtr->size / VMMACH_PAGE_SIZE;
	}
    }
    return memory;
#else
    return (*romVectorPtr->memoryAvail / VMMACH_PAGE_SIZE);
#endif
}

#ifdef sun4c

/*
 * ----------------------------------------------------------------------------
 *
 * VirtToPhysPage --
 *
 *     Translate from a virtual page to a physical page.
 *     This was a macro on the other suns, but for the sun4c, physical
 *     memory isn't contiguous.
 *
 * Results:
 *     An address.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
static int
VirtToPhysPage(pfNum)
    int		pfNum;
{
    register	Memory_Board	*mb;

    for (mb = Mboards; mb < lastMemBoard; mb++) {
	if (pfNum < mb->endVirPfNum && pfNum >= mb->startVirPfNum) {
	    return mb->physStartAddr +
		(pfNum - mb->startVirPfNum) * VMMACH_CLUSTER_SIZE;
	}
    }
    panic("VirtToPhysPage: virtual page %x not found.\n", pfNum);
    return NIL;
}


/*
 * ----------------------------------------------------------------------------
 *
 * PhysToVirtPage --
 *
 *     Translate from a physical page to a virtual page.
 *     This was a macro on the other suns, but for the sun4c, physical
 *     memory isn't contiguous.
 *
 * Results:
 *     An address.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
static int
PhysToVirtPage(pfNum)
    int		pfNum;
{
    register	Memory_Board	*mb;

    for (mb = Mboards; mb < lastMemBoard; mb++) {
	if (pfNum >= mb->physStartAddr && pfNum < mb->physEndAddr) {
	    return mb->startVirPfNum +
		(pfNum - mb->physStartAddr) / VMMACH_CLUSTER_SIZE;
	}
    }
    panic("PhysToVirtPage: physical page %x not found.\n", pfNum);
    return NIL;
}
#endif /* sun4c */


/*
 * ----------------------------------------------------------------------------
 *
 * VmMachSetSegMapInContext --
 *
 *	Set the segment map in a context that may not yet be mapped without
 *	causing a fault.  So far, this is only useful on the sun4c.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     The segment map in another context is modified..
 *
 * ----------------------------------------------------------------------------
 */
static void
VmMachSetSegMapInContext(context, addr, pmeg)
    unsigned	int	context;
    Address		addr;
    unsigned	int	pmeg;
{
#ifdef sun4
    /* This matters for a 4/110 */
    if (!VMMACH_ADDR_CHECK(addr)) {
	addr += VMMACH_TOP_OF_HOLE - VMMACH_BOTTOM_OF_HOLE + 1;
    }
#endif
    romVectorPtr->SetSegInContext(context, addr, pmeg);
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_AllocKernSpace --
 *
 *     Allocate memory for machine dependent stuff in the kernels VAS.
 *
 * Results:
 *     None.  Well, it returns something, Mike...  It seems to return the
 *     address of the next free area.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
Address
VmMach_AllocKernSpace(baseAddr)
    Address	baseAddr;
{
    /*
     * If the base address is at the beginning of a segment, we just want
     * to allocate this segment for vmMachPTESegAddr.  If base addr is partway
     * into a segment, we want a whole segment, so move to the next segment
     * to allocate that one.  (baseAddr + VMMACH_SEG_SIZE - 1) moves us to
     * next segment unless we were at the very beginning of one.  Then divide
     * by VMMACH_SEG_SIZE to get segment number.  Then multiply by
     * VMMACH_SEG_SIZE to get address of the begginning of the segment.
     */
    baseAddr = (Address) ((((unsigned int)baseAddr + VMMACH_SEG_SIZE - 1) / 
					VMMACH_SEG_SIZE) * VMMACH_SEG_SIZE);
    vmMachPTESegAddr = baseAddr;	/* first seg for Pte mapping */
    vmMachPMEGSegAddr = baseAddr + VMMACH_SEG_SIZE;	/* next for pmegs */
    return(baseAddr + 2 * VMMACH_SEG_SIZE);	/* end of allocated area */
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_Init --
 *
 *     Initialize all virtual memory data structures.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     All virtual memory linked lists and arrays are initialized.
 *
 * ----------------------------------------------------------------------------
 */
void
VmMach_Init(firstFreePage)
    int	firstFreePage;	/* Virtual page that is the first free for the 
			 * kernel. */
{
    register	int 		i;
    int 			firstFreeSegment;
    Address			virtAddr;
    Address			lastCodeAddr;
    extern	int		etext;
    VMMACH_SEG_NUM		pmeg;

    /*
     * Initialize the kernel's hardware segment table.
     */
    vm_SysSegPtr->machPtr = sysMachPtr;
    sysMachPtr->numSegs = VMMACH_NUM_SEGS_PER_CONTEXT;
    sysMachPtr->offset = PageToSeg(vm_SysSegPtr->offset);
    sysMachPtr->segTablePtr =
	    (VMMACH_SEG_NUM *) ((Address)sysMachPtr + sizeof(VmMach_SegData));
    for (i = 0; i < VMMACH_NUM_SEGS_PER_CONTEXT; i++) {
	sysMachPtr->segTablePtr[i] = VMMACH_INV_PMEG;
    }

    /*
     * Determine which hardware segment is the first that is not in use.
     */
    firstFreeSegment = ((firstFreePage - 1) << VMMACH_PAGE_SHIFT) / 
					VMMACH_SEG_SIZE + 1;
    firstFreeSegment += (unsigned int)mach_KernStart >> VMMACH_SEG_SHIFT;

    /* 
     * Initialize the PMEG and context tables and lists.
     */
    MMUInit(firstFreeSegment);

    /*
     * Initialize the page map.
     */
    bzero((Address)refModMap, sizeof(VmMachPTE) * GetNumPages());

    /*
     * The code segment is read only and all other in use kernel memory
     * is read/write.  Since the loader may put the data in the same page
     * as the last code page, the last code page is also read/write.
     */
    lastCodeAddr = (Address) ((unsigned)&etext - VMMACH_PAGE_SIZE);
    for (i = 0, virtAddr = (Address)mach_KernStart;
	    i < firstFreePage;
	    virtAddr += VMMACH_PAGE_SIZE, i++) {
	register VmMachPTE pte;
	register int physPage;

	physPage = VirtToPhysPage(i);
	{
	    int j;

	    for (j = 0; j < VMMACH_CLUSTER_SIZE; ++j) {
		pte = VmMachGetPageMap(virtAddr + j * VMMACH_PAGE_SIZE_INT)
		    & VMMACH_PAGE_FRAME_FIELD;
		if (pte != physPage + j) {
		    panic("VmMach_Init: weird mapping %x != %x\n",
			pte, physPage + j);
		}
	    }
	}

	if (recov_Transparent && virtAddr >= (Address) mach_RestartTablePtr &&
		virtAddr <
		((Address) mach_RestartTablePtr + Mach_GetRestartTableSize())) {
	    pte = VMMACH_RESIDENT_BIT | VMMACH_KRW_PROT | physPage;
	} else if (virtAddr >= (Address)MACH_CODE_START &&
	    virtAddr <= lastCodeAddr) {
	    pte = VMMACH_RESIDENT_BIT | VMMACH_KR_PROT | physPage;
	} else {
	    pte = VMMACH_RESIDENT_BIT | VMMACH_KRW_PROT | physPage;
#ifdef sun4
	    if (virtAddr >= vmStackEndAddr) {
		pte |= VMMACH_DONT_CACHE_BIT;
	    }
#endif /* sun4 */
	}
	SET_ALL_PAGE_MAP(virtAddr, pte);
    }

    /*
     * Protect the bottom of the kernel stack.
     */
    SET_ALL_PAGE_MAP((Address)mach_StackBottom, (VmMachPTE)0);

    /*
     * Invalid until the end of the last segment
     */
    for (;virtAddr < (Address) (firstFreeSegment << VMMACH_SEG_SHIFT);
	 virtAddr += VMMACH_PAGE_SIZE) {
	SET_ALL_PAGE_MAP(virtAddr, (VmMachPTE)0);
    }

    /* 
     * Zero out the invalid pmeg.
     */
    /* Need I flush something? */
    VmMachPMEGZero(VMMACH_INV_PMEG);

    /*
     * Finally copy the kernel's context to each of the other contexts.
     */
    for (i = 0; i < VMMACH_NUM_CONTEXTS; i++) {
	int		segNum;

	if (i == VMMACH_KERN_CONTEXT) {
	    continue;
	}
#ifdef NOTDEF
	VmMachSetUserContext(i);
	for (segNum = ((unsigned int) mach_KernStart) / VMMACH_SEG_SIZE,
		 segTablePtr = vm_SysSegPtr->machPtr->segTablePtr;
		 segNum < VMMACH_NUM_SEGS_PER_CONTEXT;
		 segNum++, segTablePtr++) {

	    virtAddr = (Address) (segNum * VMMACH_SEG_SIZE);
	    /*
	     * No need to flush stuff since the other contexts haven't
	     * been used yet.
	     */
	    VmMachSetSegMap(virtAddr, (int)*segTablePtr);
	}
#else
	/*
	 * For the sun4c, there is currently a problem just copying things
	 * out of the segTable, so do it from the real hardware.  I need to
	 * figure out what's wrong.
	 */
	for (segNum = ((unsigned int) mach_KernStart) / VMMACH_SEG_SIZE;
		 segNum < VMMACH_NUM_SEGS_PER_CONTEXT; segNum++) {

	    virtAddr = (Address) (segNum * VMMACH_SEG_SIZE);
#ifdef KERNEL_NOT_ABOVE_HOLE
	    if (!VMMACH_ADDR_CHECK(virtAddr)) {
		continue;
	    }
#endif
	    pmeg = VmMachGetSegMap(virtAddr);
	    VmMachSetSegMapInContext((unsigned char) i, virtAddr, pmeg);
	}
#endif
    }
#ifdef NOTDEF
    VmMachSetUserContext(VMMACH_KERN_CONTEXT);
#endif
#ifndef sun4
    if (Mach_GetMachineType() == SYS_SUN_3_50) {
	unsigned int vidPage;

#define	VIDEO_START	0x100000	/* From Sun3 architecture manual */
#define	VIDEO_SIZE	0x20000
	vidPage = VIDEO_START / VMMACH_PAGE_SIZE;
	if (firstFreePage > vidPage) {
	    panic("VmMach_Init: We overran video memory.\n");
	}
	/*
	 * On 3/50's the display is kept in main memory beginning at 1 
	 * Mbyte and going for 128 kbytes.  Reserve this memory so VM
	 * doesn't try to use it.
	 */
	for (;vidPage < (VIDEO_START + VIDEO_SIZE) / VMMACH_PAGE_SIZE;
	     vidPage++) {
	    Vm_ReservePage(vidPage);
	}
    }
#endif /* sun4 */
    /*
     * Turn on caching.
     */
    if (vmMachHasVACache) {
	VmMachClearCacheTags();
    }
#ifndef sun4c
    VmMachInitAddrErrorControlReg();
#endif
    VmMachInitSystemEnableReg();
}


/*
 *----------------------------------------------------------------------
 *
 * MMUInit --
 *
 *	Initialize the context table and lists and the Pmeg table and 
 *	lists.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Context table and Pmeg table are initialized.  Also context list
 *	and pmeg list are initialized.
 *
 *----------------------------------------------------------------------
 */
static void
MMUInit(firstFreeSegment)
    int		firstFreeSegment;
{
    register	int		i;
    register	PMEG		*pmegPtr;
    register	VMMACH_SEG_NUM	*segTablePtr;
    VMMACH_SEG_NUM		pageCluster;

    /*
     * Initialize the context table.
     */
    contextArray[VMMACH_KERN_CONTEXT].flags = CONTEXT_IN_USE;
    List_Init(contextList);
    for (i = 0; i < VMMACH_NUM_CONTEXTS; i++) {
	if (i != VMMACH_KERN_CONTEXT) {
	    contextArray[i].flags = 0;
	    List_Insert((List_Links *) &contextArray[i], 
			LIST_ATREAR(contextList));
	}
	contextArray[i].context = i;
    }

    /*
     * Initialize the page cluster list.
     */
    List_Init(pmegFreeList);
    List_Init(pmegInuseList);

    /*
     * Initialize the pmeg structure.
     */
    bzero((Address)pmegArray, VMMACH_NUM_PMEGS * sizeof(PMEG));
    for (i = 0, pmegPtr = pmegArray; i < VMMACH_NUM_PMEGS; i++, pmegPtr++) {
	pmegPtr->segInfo.segPtr = (Vm_Segment *) NIL;
	pmegPtr->flags = PMEG_DONT_ALLOC;
	pmegPtr->segInfo.nextLink = (struct VmMach_PMEGseg *)NIL;
    }

#ifdef sun2
    /*
     * Segment 0 is left alone because it is required for the monitor.
     */
    pmegArray[0].segPtr = (Vm_Segment *)NIL;
    pmegArray[0].hardSegNum = 0;
    i = 1;
#else
    i = 0;
#endif /* sun2 */

    /*
     * Invalidate all hardware segments from first segment up to the beginning
     * of the kernel.
     */
    for (; i < ((((unsigned int) mach_KernStart) & VMMACH_ADDR_MASK) >>
	    VMMACH_SEG_SHIFT); i++) {
	int	j;
	Address	addr;

	addr = (Address) (i << VMMACH_SEG_SHIFT);
	/*
	 * Copy the invalidation to all the other contexts, so that
	 * the user contexts won't have double-mapped pmegs at the low-address
	 * segments.
	 */
	for (j = 0; j < VMMACH_NUM_CONTEXTS; j++) {
	    /* Yes, do this here since user stuff would be double mapped. */
	    VmMachSetSegMapInContext((unsigned char) j, addr,
		    (unsigned char) VMMACH_INV_PMEG);
	}
    }
    i = ((unsigned int) mach_KernStart >> VMMACH_SEG_SHIFT);

    /*
     * Reserve all pmegs that have kernel code or heap.
     */
    for (segTablePtr = vm_SysSegPtr->machPtr->segTablePtr;
         i < firstFreeSegment;
	 i++, segTablePtr++) {
	pageCluster = VmMachGetSegMap((Address) (i << VMMACH_SEG_SHIFT));
	pmegArray[pageCluster].pageCount = VMMACH_NUM_PAGES_PER_SEG;
	pmegArray[pageCluster].segInfo.segPtr = vm_SysSegPtr;
	pmegArray[pageCluster].segInfo.hardSegNum = i;
	*segTablePtr = pageCluster;
    }

    /*
     * Invalidate all hardware segments that aren't in code or heap and are 
     * before the specially mapped page clusters.
     */
    for (; i < VMMACH_FIRST_SPECIAL_SEG; i++, segTablePtr++) {
	Address	addr;
	/* Yes, do this here, since user stuff would be double-mapped. */
	addr = (Address) (i << VMMACH_SEG_SHIFT);
	VmMachSetSegMap(addr, VMMACH_INV_PMEG);
    }

    /*
     * Mark the invalid pmeg so that it never gets used.
     */
    pmegArray[VMMACH_INV_PMEG].segInfo.segPtr = vm_SysSegPtr;
    pmegArray[VMMACH_INV_PMEG].flags = PMEG_NEVER_FREE;

    /*
     * Now reserve the rest of the page clusters that have been set up by
     * the monitor.  Don't reserve any PMEGs that don't have any valid 
     * mappings in them.
     */
    for (; i < VMMACH_NUM_SEGS_PER_CONTEXT; i++, segTablePtr++) {
	Address		virtAddr;
	int		j;
	VmMachPTE	pte;
	Boolean		inusePMEG;

	virtAddr = (Address) (i << VMMACH_SEG_SHIFT);
        if ((virtAddr >= (Address)VMMACH_DMA_START_ADDR) &&
	    (virtAddr < (Address)(VMMACH_DMA_START_ADDR+VMMACH_DMA_SIZE))) {
	    /*
	     * Blow away anything in DMA space. 
	     */
	    pageCluster = VMMACH_INV_PMEG;
	    VmMachSetSegMap(virtAddr, VMMACH_INV_PMEG);
	} else {
	    pageCluster = VmMachGetSegMap(virtAddr);
	    if (pageCluster != VMMACH_INV_PMEG) {
		inusePMEG = FALSE;
		for (j = 0; 
		     j < VMMACH_NUM_PAGES_PER_SEG_INT; 
		     j++, virtAddr += VMMACH_PAGE_SIZE_INT) {
		    pte = VmMachGetPageMap(virtAddr);
		    if ((pte & VMMACH_RESIDENT_BIT) &&
			(pte & (VMMACH_TYPE_FIELD|VMMACH_PAGE_FRAME_FIELD)) != 0) {
			/*
			 * A PMEG contains a valid mapping if the resident
			 * bit is set and the page frame and type field
			 * are non-zero.  On Sun 2/50's the PROM sets
			 * the resident bit but leaves the page frame equal
			 * to zero.
			 */
			if (!inusePMEG) {
			    pmegArray[pageCluster].segInfo.segPtr =
				    vm_SysSegPtr;
			    pmegArray[pageCluster].segInfo.hardSegNum = i;
			    pmegArray[pageCluster].flags = PMEG_NEVER_FREE;
			    inusePMEG = TRUE;
			}
		    } else {
			VmMachSetPageMap(virtAddr, (VmMachPTE)0);
		    }
		}
		virtAddr -= VMMACH_SEG_SIZE;
		if (!inusePMEG ||
		    (virtAddr >= (Address)VMMACH_DMA_START_ADDR &&
		     virtAddr < (Address)(VMMACH_DMA_START_ADDR+VMMACH_DMA_SIZE))) {
		    VmMachSetSegMap(virtAddr, VMMACH_INV_PMEG);
		    pageCluster = VMMACH_INV_PMEG;
		}
	    }
	}
	*segTablePtr = pageCluster;
    }

#if defined (sun3)
    /*
     * We can't use the hardware segment that corresponds to the
     * last segment of physical memory for some reason.  Zero it out
     * and can't reboot w/o powering the machine off.
     */
    dontUse = (*romVectorPtr->memoryAvail - 1) / VMMACH_SEG_SIZE;
#endif

    /*
     * Now finally, all page clusters that have a NIL segment pointer are
     * put onto the page cluster fifo.  On a Sun-3 one hardware segment is 
     * off limits for some reason.  Zero it out and can't reboot w/o 
     * powering the machine off.
     */
    for (i = 0, pmegPtr = pmegArray; i < VMMACH_NUM_PMEGS; i++, pmegPtr++) {

	if (pmegPtr->segInfo.segPtr == (Vm_Segment *) NIL 
#if defined (sun3)
	    && i != dontUse
#endif
	) {
	    List_Insert((List_Links *) pmegPtr, LIST_ATREAR(pmegFreeList));
	    pmegPtr->flags = 0;
	    VmMachPMEGZero(i);
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_SegInit --
 *
 *      Initialize hardware dependent data for a segment.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Machine dependent data struct and is allocated and initialized.
 *
 * ----------------------------------------------------------------------------
 */
void
VmMach_SegInit(segPtr)
    Vm_Segment	*segPtr;
{
    register	VmMach_SegData	*segDataPtr;
    int				segTableSize;
    int		i;

    if (segPtr->type == VM_CODE) {
	segTableSize =
	    (segPtr->ptSize + segPtr->offset + VMMACH_NUM_PAGES_PER_SEG - 1) / 
						    VMMACH_NUM_PAGES_PER_SEG;
    } else {
	segTableSize = segPtr->ptSize / VMMACH_NUM_PAGES_PER_SEG;
    }
    segDataPtr = (VmMach_SegData *)malloc(sizeof(VmMach_SegData) +
	(segTableSize * sizeof (VMMACH_SEG_NUM)));

    segDataPtr->numSegs = segTableSize;
    segDataPtr->offset = PageToSeg(segPtr->offset);
    segDataPtr->segTablePtr =
	    (VMMACH_SEG_NUM *) ((Address)segDataPtr + sizeof(VmMach_SegData));
    segDataPtr->pmegInfo.inuse = 0;
    for (i = 0; i < segTableSize; i++) {
	segDataPtr->segTablePtr[i] = VMMACH_INV_PMEG;
    }
    segPtr->machPtr = segDataPtr;
    /*
     * Set the minimum and maximum virtual addresses for this segment to
     * be as small and as big as possible respectively because things will
     * be prevented from growing automatically as soon as segments run into
     * each other.
     */
    segPtr->minAddr = (Address)0;
    segPtr->maxAddr = (Address)0xffffffff;
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_SegExpand --
 *
 *	Allocate more space for the machine dependent structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory allocated for a new hardware segment table.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
void
VmMach_SegExpand(segPtr, firstPage, lastPage)
    register	Vm_Segment	*segPtr;	/* Segment to expand. */
    int				firstPage;	/* First page to add. */
    int				lastPage;	/* Last page to add. */
{
    int				newSegTableSize;
    register	VmMach_SegData	*oldSegDataPtr;
    register	VmMach_SegData	*newSegDataPtr;

    newSegTableSize = (segPtr->ptSize + VMMACH_NUM_PAGES_PER_SEG-1 +
	    (segPtr->offset%VMMACH_NUM_PAGES_PER_SEG)) /
	    VMMACH_NUM_PAGES_PER_SEG;
    oldSegDataPtr = segPtr->machPtr;
    if (newSegTableSize <= oldSegDataPtr->numSegs) {
	return;
    }
    newSegDataPtr = 
	(VmMach_SegData *)malloc(sizeof(VmMach_SegData) + (newSegTableSize *
		sizeof (VMMACH_SEG_NUM)));
    newSegDataPtr->numSegs = newSegTableSize;
    newSegDataPtr->offset = PageToSeg(segPtr->offset);
    newSegDataPtr->segTablePtr = (VMMACH_SEG_NUM *) ((Address)newSegDataPtr +
		    sizeof(VmMach_SegData));
    CopySegData(segPtr, oldSegDataPtr, newSegDataPtr);
    free((Address)oldSegDataPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * CopySegData --
 *
 *	Copy over the old hardware segment data into the new expanded
 *	structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The hardware segment table is copied.
 *
 *----------------------------------------------------------------------
 */
ENTRY static void
CopySegData(segPtr, oldSegDataPtr, newSegDataPtr)
    register	Vm_Segment	*segPtr;	/* The segment to add the
						   virtual pages to. */
    register	VmMach_SegData	*oldSegDataPtr;
    register	VmMach_SegData	*newSegDataPtr;
{
    int		i, j;

    MASTER_LOCK(vmMachMutexPtr);

    if (segPtr->type == VM_HEAP) {
	/*
	 * Copy over the hardware segment table into the lower part
	 * and set the rest to invalid.
	 */
	bcopy((Address)oldSegDataPtr->segTablePtr,
		(Address)newSegDataPtr->segTablePtr,
		oldSegDataPtr->numSegs * sizeof (VMMACH_SEG_NUM));
	j = newSegDataPtr->numSegs - oldSegDataPtr->numSegs;

	for (i = 0; i < j; i++) {
	    newSegDataPtr->segTablePtr[oldSegDataPtr->numSegs + i]
		    = VMMACH_INV_PMEG;
	}
    } else {
	/*
	 * Copy the current segment table into the high part of the
	 * new segment table and set the lower part to invalid.
	 */
	bcopy((Address)oldSegDataPtr->segTablePtr,
	    (Address)(newSegDataPtr->segTablePtr + 
	    newSegDataPtr->numSegs - oldSegDataPtr->numSegs),
	    oldSegDataPtr->numSegs * sizeof (VMMACH_SEG_NUM));
	j = newSegDataPtr->numSegs - oldSegDataPtr->numSegs;

	for (i = 0; i < j; i++) {
	    newSegDataPtr->segTablePtr[i] = VMMACH_INV_PMEG;
	}
    }
    segPtr->machPtr = newSegDataPtr;

    MASTER_UNLOCK(vmMachMutexPtr);
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_SegDelete --
 *
 *      Free hardware dependent resources for this software segment.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Machine dependent struct freed and the pointer in the segment
 *	is set to NIL.
 *
 * ----------------------------------------------------------------------------
 */
void
VmMach_SegDelete(segPtr)
    register	Vm_Segment	*segPtr;    /* Pointer to segment to free. */
{
    SegDelete(segPtr);
    free((Address)segPtr->machPtr);
    segPtr->machPtr = (VmMach_SegData *)NIL;
    if (segPtr->type==VM_SHARED && debugVmStubs) {
	printf("Done with seg %d\n", segPtr->segNum);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * SegDelete --
 *
 *      Free up any pmegs used by this segment.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      All pmegs used by this segment are freed.
 *
 * ----------------------------------------------------------------------------
 */
ENTRY static void
SegDelete(segPtr)
    Vm_Segment	*segPtr;    /* Pointer to segment to free. */
{
    register	int 		i;
    register	VMMACH_SEG_NUM	*pmegPtr;
    register	VmMach_SegData	*machPtr;

    MASTER_LOCK(vmMachMutexPtr);

    machPtr = segPtr->machPtr;
    for (i = 0, pmegPtr = (VMMACH_SEG_NUM *) machPtr->segTablePtr;
	     i < machPtr->numSegs; i++, pmegPtr++) {
	if (*pmegPtr != VMMACH_INV_PMEG) {
	    /* Flushing is done in PMEGFree */
	    PMEGFree((int) *pmegPtr);
	}
    }

    MASTER_UNLOCK(vmMachMutexPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_GetContext --
 *
 *	Return the context for a process, given its pcb.
 *
 * Results:
 *	Context number for process. -1 if the process doesn't
 *	have a context allocated.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
VmMach_GetContext(procPtr)
    Proc_ControlBlock	*procPtr;
{
    VmMach_Context	*contextPtr;
    contextPtr = procPtr->vmPtr->machPtr->contextPtr;
    return ((contextPtr == (VmMach_Context *)NIL) ? -1 : contextPtr->context);
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_ProcInit --
 *
 *	Initalize the machine dependent part of the VM proc info.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Machine dependent proc info is initialized.
 *
 *----------------------------------------------------------------------
 */
void
VmMach_ProcInit(vmPtr)
    register	Vm_ProcInfo	*vmPtr;
{
    if (vmPtr->machPtr == (VmMach_ProcData *)NIL) {
	vmPtr->machPtr = (VmMach_ProcData *)malloc(sizeof(VmMach_ProcData));
    }
    vmPtr->machPtr->contextPtr = (VmMach_Context *)NIL;
    vmPtr->machPtr->mapSegPtr = (struct Vm_Segment *)NIL;
    vmPtr->machPtr->sharedData.allocVector = (int *)NIL;
}


/*
 * ----------------------------------------------------------------------------
 *
 * PMEGGet --
 *
 *      Return the next pmeg from the list of available pmegs.  If the 
 *      lock flag is set then the pmeg is removed from the pmeg list.
 *      Otherwise it is moved to the back.
 *
 * Results:
 *      The pmeg number that is allocated.
 *
 * Side effects:
 *      A pmeg is either removed from the pmeg list or moved to the back.
 *
 * ----------------------------------------------------------------------------
 */
INTERNAL static int
PMEGGet(softSegPtr, hardSegNum, flags)
    Vm_Segment 	*softSegPtr;	/* Which software segment this is. */
    int		hardSegNum;	/* Which hardware segment in the software 
				   segment that this is */
    Boolean	flags;		/* Flags that indicate the state of the pmeg. */
{
    register PMEG		*pmegPtr;
    register Vm_Segment		*segPtr;
    register VmMachPTE		*ptePtr;
    register VmMach_Context	*contextPtr;
    register int		i;
    register VmMachPTE		hardPTE;
    VmMachPTE			pteArray[VMMACH_NUM_PAGES_PER_SEG_INT];
    int	     			oldContext;
    int				pmegNum;
    Address			virtAddr;
    Boolean			found = FALSE;
    int				numValidPages;
    struct VmMach_PMEGseg	*curSeg, *nextSeg;

    if (List_IsEmpty(pmegFreeList)) {
	
	LIST_FORALL(pmegInuseList, (List_Links *)pmegPtr) {
	    if (pmegPtr->lockCount == 0) {
		found = TRUE;
		break;
	    }
	}
	if (!found) {
	    panic("Pmeg lists empty\n");
	    return(VMMACH_INV_PMEG);
	}
    } else {
	pmegPtr = (PMEG *)List_First(pmegFreeList);
    }
    pmegNum = pmegPtr - pmegArray;

    oldContext = VmMachGetContextReg();
    if (pmegPtr->segInfo.segPtr != (Vm_Segment *) NIL) {
	/*
	 * Need to steal the pmeg from its current owner.
	 */
	for (curSeg = &pmegPtr->segInfo; curSeg != (struct VmMach_PMEGseg *)NIL;) {
	    vmStat.machDepStat.stealPmeg++;
	    segPtr = curSeg->segPtr;
	    *GetHardSegPtr(segPtr->machPtr, curSeg->hardSegNum) =
		    VMMACH_INV_PMEG;
	    virtAddr = (Address) (curSeg->hardSegNum << VMMACH_SEG_SHIFT);
	    /*
	     * Delete the pmeg from all appropriate contexts.
	     */
	    if (segPtr->type == VM_SYSTEM) {
		/*
		 * For cache accesses of data with the supervisor tag set,
		 * the flush only needs to be done in one context.
		 */
		numValidPages = GetNumValidPages(virtAddr);
		if (numValidPages >
			(VMMACH_CACHE_SIZE / VMMACH_PAGE_SIZE_INT)) {
		    if (vmMachHasVACache) {
			VmMachFlushSegment(virtAddr);
		    }
		} else {
		    /* flush the pages */
		    FlushValidPages(virtAddr);
		}

		for (i = 0; i < VMMACH_NUM_CONTEXTS; i++) {
		    VmMachSetContextReg(i);
		    VmMachSetSegMap(virtAddr, VMMACH_INV_PMEG);
		}
	    } else {
		for (i = 1, contextPtr = &contextArray[1];
		     i < VMMACH_NUM_CONTEXTS; 
		     i++, contextPtr++) {
		    if (contextPtr->flags & CONTEXT_IN_USE) {
			if (contextPtr->map[curSeg->hardSegNum] ==
				pmegNum) {
			    VmMachSetContextReg(i);
			    contextPtr->map[curSeg->hardSegNum] =
				    VMMACH_INV_PMEG;
			    numValidPages = GetNumValidPages(virtAddr);
			    if (numValidPages >
				    (VMMACH_CACHE_SIZE / VMMACH_PAGE_SIZE_INT)) {
				if (vmMachHasVACache) {
				    VmMachFlushSegment(virtAddr);
				}
			    } else {
				/* flush the pages */
				FlushValidPages(virtAddr);
			    }
			    VmMachSetSegMap(virtAddr, VMMACH_INV_PMEG);
			}
			if (contextPtr->map[MAP_SEG_NUM] == pmegNum) {
			    VmMachSetContextReg(i);
			    contextPtr->map[MAP_SEG_NUM] = VMMACH_INV_PMEG;
			    if (vmMachHasVACache) {
				VmMachFlushSegment((Address)VMMACH_MAP_SEG_ADDR);
			    }
			    VmMachSetSegMap((Address)VMMACH_MAP_SEG_ADDR,
					    VMMACH_INV_PMEG);
			}
		    }
		}
	    }
	    nextSeg = curSeg->nextLink;
	    if (curSeg != &pmegPtr->segInfo) {
		curSeg->inuse = 0;
	    }
	    curSeg = nextSeg;
	}
	pmegPtr->segInfo.nextLink = (struct VmMach_PMEGseg *)NIL;
	VmMachSetContextReg(oldContext);
	/*
	 * Read out all reference and modify bits from the pmeg.
	 */
	if (pmegPtr->pageCount > 0) {
	    ptePtr = pteArray;
	    VmMachReadAndZeroPMEG(pmegNum, ptePtr);
	    for (i = 0;
		 i < VMMACH_NUM_PAGES_PER_SEG_INT;
		 i++, ptePtr++) {
		hardPTE = *ptePtr;
		if ((hardPTE & VMMACH_RESIDENT_BIT) &&
		    (hardPTE & (VMMACH_REFERENCED_BIT | VMMACH_MODIFIED_BIT))) {
		    refModMap[PhysToVirtPage(hardPTE & VMMACH_PAGE_FRAME_FIELD)]
		     |= hardPTE & (VMMACH_REFERENCED_BIT | VMMACH_MODIFIED_BIT);
		}
	    }
	}
    }

    /* Initialize the pmeg and delete it from the fifo.  If we aren't 
     * supposed to lock this pmeg, then put it at the rear of the list.
     */
    pmegPtr->segInfo.segPtr = softSegPtr;
    pmegPtr->segInfo.hardSegNum = hardSegNum;
    pmegPtr->pageCount = 0;
    List_Remove((List_Links *) pmegPtr);
    if (!(flags & PMEG_DONT_ALLOC)) {
	List_Insert((List_Links *) pmegPtr, LIST_ATREAR(pmegInuseList));
    }
    pmegPtr->flags = flags;

    return(pmegNum);
}


/*
 *----------------------------------------------------------------------
 *
 * GetNumValidPages --
 *
 *	Return the number of valid pages in a segment.
 *
 * Results:
 *	The number of valid pages.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
GetNumValidPages(virtAddr)
    Address	virtAddr;
{
    int		i;
    int		numValid = 0;
    unsigned	int	pte;

    for (i = 0; i < VMMACH_NUM_PAGES_PER_SEG_INT; i++) {
	pte = VmMachGetPageMap(virtAddr + (i * VMMACH_PAGE_SIZE_INT));
	if (pte & VMMACH_RESIDENT_BIT) {
	    numValid++;
	}
    }

    return numValid;
}


/*
 *----------------------------------------------------------------------
 *
 * FlushValidPages --
 *
 *	Flush the valid pages in a segment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The valid pages in a segment are flushed.
 *
 *----------------------------------------------------------------------
 */
static void
FlushValidPages(virtAddr)
    Address	virtAddr;
{
    int		i;
    unsigned	int	pte;

    if (vmMachHasVACache) {
	for (i = 0; i < VMMACH_NUM_PAGES_PER_SEG_INT; i++) {
	    pte = VmMachGetPageMap(virtAddr + (i * VMMACH_PAGE_SIZE_INT));
	    if (pte & VMMACH_RESIDENT_BIT) {
		VmMachFlushPage(virtAddr + (i * VMMACH_PAGE_SIZE_INT));
	    }
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * PMEGFree --
 *
 *      Return the given pmeg to the pmeg list.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The pmeg is returned to the pmeg list.
 *
 * ----------------------------------------------------------------------------
 */
INTERNAL static void
PMEGFree(pmegNum)
    int 	pmegNum;	/* Which pmeg to free */
{
    register	PMEG	*pmegPtr;
    struct VmMach_PMEGseg	*segPtr, *nextPtr;

    pmegPtr = &pmegArray[pmegNum];
    /*
     * If this pmeg can never be freed then don't free it.  This case can
     * occur when a device is mapped into a user's address space.
     */
    if (pmegPtr->flags & PMEG_NEVER_FREE) {
	return;
    }

    if (pmegPtr->pageCount > 0) {

	/*
	 * Deal with pages that are still cached in this pmeg.
	 */
	VmMachPMEGZero(pmegNum);
    }
    if (pmegPtr->segInfo.segPtr != (Vm_Segment *)NIL) {
	for (segPtr = &pmegPtr->segInfo; segPtr != (struct VmMach_PMEGseg *)NIL;) {
	    if (segPtr->segPtr->machPtr == (VmMach_SegData *)NIL) {
		printf("PMEGFree(%d): seg %d has no machPtr!\n", pmegNum,
			segPtr->segPtr->segNum);
	    } else {
		*GetHardSegPtr(segPtr->segPtr->machPtr, segPtr->hardSegNum) =
			VMMACH_INV_PMEG;
	    }
	    nextPtr = segPtr->nextLink;
	    if (segPtr != &pmegPtr->segInfo) {
		segPtr->inuse = 0;
	    }
	    segPtr = nextPtr;
	}
    }
    pmegPtr->segInfo.nextLink = (struct VmMach_PMEGseg *) NIL;
    pmegPtr->segInfo.segPtr = (Vm_Segment *) NIL;

    /*
     * I really don't understand the code here.  The original was the second
     * line.  The first line was to try to fix an error that shows up in
     * UnmapIntelPage(), but I've tried now to fix that error there, since the
     * second line breaks things elsewhere.
     */
    if (pmegPtr->pageCount == 0 || !(pmegPtr->flags & PMEG_DONT_ALLOC)) {
	List_Remove((List_Links *) pmegPtr);
    }
    pmegPtr->flags = 0;
    pmegPtr->lockCount = 0;
    /*
     * Put this pmeg at the front of the pmeg free list.
     */
    List_Insert((List_Links *) pmegPtr, LIST_ATFRONT(pmegFreeList));
}


/*
 * ----------------------------------------------------------------------------
 *
 * PMEGLock --
 *
 *      Increment the lock count on a pmeg.
 *
 * Results:
 *      TRUE if there was a valid PMEG behind the given hardware segment.
 *
 * Side effects:
 *      The lock count is incremented if there is a valid pmeg at the given
 *	hardware segment.
 *
 * ----------------------------------------------------------------------------
 */
ENTRY static Boolean
PMEGLock(machPtr, segNum)
    register VmMach_SegData	*machPtr;
    int				segNum;
{
    unsigned int pmegNum;

    MASTER_LOCK(vmMachMutexPtr);

    pmegNum = *GetHardSegPtr(machPtr, segNum);
    if (pmegNum != VMMACH_INV_PMEG) {
	pmegArray[pmegNum].lockCount++;
	MASTER_UNLOCK(vmMachMutexPtr);
	return(TRUE);
    } else {
	MASTER_UNLOCK(vmMachMutexPtr);
	return(FALSE);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_SetupContext --
 *
 *      Return the value of the context register for the given process.
 *	It is assumed that this routine is called on a uni-processor right
 *	before the process starts executing.
 *	
 * Results:
 *      None.
 *
 * Side effects:
 *      The context list is modified.
 *
 * ----------------------------------------------------------------------------
 */
ENTRY ClientData
VmMach_SetupContext(procPtr)
    register	Proc_ControlBlock	*procPtr;
{
    register	VmMach_Context	*contextPtr;

    MASTER_LOCK(vmMachMutexPtr);

    while (TRUE) {
	contextPtr = procPtr->vmPtr->machPtr->contextPtr;
	if (contextPtr != (VmMach_Context *)NIL) {
	    if (contextPtr != &contextArray[VMMACH_KERN_CONTEXT]) {
		List_Move((List_Links *)contextPtr, LIST_ATREAR(contextList));
	    }
	    MASTER_UNLOCK(vmMachMutexPtr);
	    return((ClientData)contextPtr->context);
	}
        SetupContext(procPtr);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * SetupContext --
 *
 *      Initialize the context for the given process.  If the process does
 *	not have a context associated with it then one is allocated.
 *
 *	Note that this routine runs unsynchronized even though it is using
 *	internal structures.  See the note above while this is OK.  I
 * 	eliminated the monitor lock because it is unnecessary anyway and
 *	it slows down context-switching.
 *	
 * Results:
 *      None.
 *
 * Side effects:
 *      The context field in the process table entry and the context list are
 * 	both modified if a new context is allocated.
 *
 * ----------------------------------------------------------------------------
 */
INTERNAL static void
SetupContext(procPtr)
    register	Proc_ControlBlock	*procPtr;
{
    register	VmMach_Context	*contextPtr;
    register	VmMach_SegData	*segDataPtr;
    register	Vm_ProcInfo	*vmPtr;
    int		stolenContext	= FALSE;

    vmPtr = procPtr->vmPtr;
    contextPtr = vmPtr->machPtr->contextPtr;

    if (procPtr->genFlags & (PROC_KERNEL | PROC_NO_VM)) {
	/*
	 * This is a kernel process or a process that is exiting.
	 * Set the context to kernel and return.
	 */
	VmMachSetContextReg(VMMACH_KERN_CONTEXT);
	vmPtr->machPtr->contextPtr = &contextArray[VMMACH_KERN_CONTEXT];
	return;
    }

    if (contextPtr == (VmMach_Context *)NIL) {
	/*
	 * In this case there is no context setup for this process.  Therefore
	 * we have to find a context, initialize the context table entry and 
	 * initialize the context stuff in the proc table.
	 */
	if (List_IsEmpty((List_Links *) contextList)) {
	    panic("SetupContext: Context list empty\n");
	}
	/* 
	 * Take the first context off of the context list.
	 */
	contextPtr = (VmMach_Context *) List_First(contextList);
	if (contextPtr->flags & CONTEXT_IN_USE) {
	    contextPtr->procPtr->vmPtr->machPtr->contextPtr =
							(VmMach_Context *)NIL;
	    vmStat.machDepStat.stealContext++;
	    stolenContext = TRUE;
	}
	/*
	 * Initialize the context table entry.
	 */
	contextPtr->flags = CONTEXT_IN_USE;
	contextPtr->procPtr = procPtr;
	vmPtr->machPtr->contextPtr = contextPtr;
	VmMachSetContextReg((int)contextPtr->context);
	if (stolenContext && vmMachHasVACache) {
	    VmMach_FlushCurrentContext();
	}
	/*
	 * Set the context map.
	 */
	{
	    int			i;
	    unsigned int	j;

	    /*
	     * Since user addresses are never higher than the bottom of the
	     * hole in the address space, this will save something like 30ms
	     * by ending at VMMACH_BOTTOM_OF_HOLE rather than mach_KernStart.
	     */
	    j = ((unsigned int)VMMACH_BOTTOM_OF_HOLE) >> VMMACH_SEG_SHIFT;
	    for (i = 0; i < j; i++) {
		contextPtr->map[i] = VMMACH_INV_PMEG;
	    }
	}
	segDataPtr = vmPtr->segPtrArray[VM_CODE]->machPtr;
	bcopy((Address)segDataPtr->segTablePtr, 
	    (Address) (contextPtr->map + segDataPtr->offset),
	    segDataPtr->numSegs * sizeof (VMMACH_SEG_NUM));

	segDataPtr = vmPtr->segPtrArray[VM_HEAP]->machPtr;
	bcopy((Address)segDataPtr->segTablePtr, 
		(Address) (contextPtr->map + segDataPtr->offset),
		segDataPtr->numSegs * sizeof (VMMACH_SEG_NUM));

	segDataPtr = vmPtr->segPtrArray[VM_STACK]->machPtr;
	bcopy((Address)segDataPtr->segTablePtr, 
		(Address) (contextPtr->map + segDataPtr->offset),
		segDataPtr->numSegs * sizeof (VMMACH_SEG_NUM));
	if (vmPtr->sharedSegs != (List_Links *)NIL) {
	    Vm_SegProcList *segList;
	    LIST_FORALL(vmPtr->sharedSegs,(List_Links *)segList) {
		segDataPtr = segList->segTabPtr->segPtr->machPtr;
		bcopy((Address)segDataPtr->segTablePtr, 
			(Address) (contextPtr->map+PageToSeg(segList->offset)),
			segDataPtr->numSegs);
	    }
	}
	if (vmPtr->machPtr->mapSegPtr != (struct Vm_Segment *)NIL) {
	    contextPtr->map[MAP_SEG_NUM] = vmPtr->machPtr->mapHardSeg;
	} else {
	    contextPtr->map[MAP_SEG_NUM] = VMMACH_INV_PMEG;
	}
	/*
	 * Push map out to hardware.
	 */
	VmMachCopyUserSegMap(contextPtr->map);
    } else {
	VmMachSetContextReg((int)contextPtr->context);
    }
    List_Move((List_Links *)contextPtr, LIST_ATREAR(contextList));
}


/*
 * ----------------------------------------------------------------------------
 *
 * Vm_FreeContext --
 *
 *      Free the given context.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The context table and context lists are modified.
 *
 * ----------------------------------------------------------------------------
 */
ENTRY void
VmMach_FreeContext(procPtr)
    register	Proc_ControlBlock	*procPtr;
{
    register	VmMach_Context	*contextPtr;
    register	VmMach_ProcData	*machPtr;

    MASTER_LOCK(vmMachMutexPtr);

    machPtr = procPtr->vmPtr->machPtr;
    contextPtr = machPtr->contextPtr;
    if (contextPtr == (VmMach_Context *)NIL ||
        contextPtr->context == VMMACH_KERN_CONTEXT) {
	MASTER_UNLOCK(vmMachMutexPtr);
	return;
    }

    List_Move((List_Links *)contextPtr, LIST_ATFRONT(contextList));
    contextPtr->flags = 0;
    machPtr->contextPtr = (VmMach_Context *)NIL;

    MASTER_UNLOCK(vmMachMutexPtr);
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_ReinitContext --
 *
 *      Free the current context and set up another one.  This is called
 *	by routines such as Proc_Exec that add things to the context and
 *	then have to abort or start a process running with a new image.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The context table and context lists are modified.
 *
 * ----------------------------------------------------------------------------
 */
void
VmMach_ReinitContext(procPtr)
    register	Proc_ControlBlock	*procPtr;
{
    VmMach_FreeContext(procPtr);
    MASTER_LOCK(vmMachMutexPtr);
    procPtr->vmPtr->machPtr->contextPtr = (VmMach_Context *)NIL;
    SetupContext(procPtr);
    MASTER_UNLOCK(vmMachMutexPtr);
}

#if defined(sun2)

static int	 allocatedPMEG;
static VmMachPTE intelSavedPTE;		/* The page table entry that is stored
					 * at the address that the intel page
					 * has to overwrite. */
static unsigned int intelPage;		/* The page frame that was allocated.*/
#endif


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_MapIntelPage --
 *
 *      Allocate and validate a page for the Intel Ethernet chip.  This routine
 *	is required in order to initialize the chip.  The chip expects 
 *	certain stuff to be at a specific virtual address when it is 
 *	initialized.  This routine sets things up so that the expected
 *	virtual address is accessible.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The old pte stored at the virtual address and the page frame that is
 *	allocated are stored in static globals.
 *
 * ----------------------------------------------------------------------------
 */
#ifndef sun4c
/*ARGSUSED*/
void
VmMach_MapIntelPage(virtAddr) 
    Address	virtAddr; /* Virtual address where a page has to be validated
			     at. */
{
#if defined(sun2)
    VmMachPTE		pte;
    int			pmeg;

    /*
     * See if there is a PMEG already.  If not allocate one.
     */
    pmeg = VmMachGetSegMap(virtAddr);
    if (pmeg == VMMACH_INV_PMEG) {
	MASTER_LOCK(vmMachMutexPtr);
	/* No flush, since PMEGGet takes care of that. */
	allocatedPMEG = PMEGGet(vm_SysSegPtr, 
				(unsigned)virtAddr >> VMMACH_SEG_SHIFT,
				PMEG_DONT_ALLOC);
	MASTER_UNLOCK(vmMachMutexPtr);
	VmMachSetSegMap(virtAddr, allocatedPMEG);
    } else {
	allocatedPMEG = VMMACH_INV_PMEG;
	intelSavedPTE = VmMachGetPageMap(virtAddr);
    }

    /*
     * Set up the page table entry.
     */
    intelPage = Vm_KernPageAllocate();
    pte = VMMACH_RESIDENT_BIT | VMMACH_KRW_PROT | VirtToPhysPage(intelPage);
    /* No flush since this should never be cached. */
    SET_ALL_PAGE_MAP(virtAddr, pte);
#endif /* sun2  */
#ifdef sun4
    VmMachPTE		pte;
    int			pmeg;
    int			oldContext;
    int			i;

    /*
     * See if there is a PMEG already.  If not allocate one.
     */
    pmeg = VmMachGetSegMap(virtAddr);
    if (pmeg == VMMACH_INV_PMEG) {
	MASTER_LOCK(vmMachMutexPtr);
	/* No flush, since PMEGGet takes care of that. */
	pmeg = PMEGGet(vm_SysSegPtr, 
				(int)((unsigned)virtAddr) >> VMMACH_SEG_SHIFT,
				PMEG_DONT_ALLOC);
	MASTER_UNLOCK(vmMachMutexPtr);
	VmMachSetSegMap(virtAddr, pmeg);
    } 
    oldContext = VmMachGetContextReg();
    for (i = 0; i < VMMACH_NUM_CONTEXTS; i++) {
	VmMachSetContextReg(i);
	VmMachSetSegMap(virtAddr, pmeg);
    }
    VmMachSetContextReg(oldContext);
    /*
     * Set up the page table entry.
     */
    pte = VmMachGetPageMap(virtAddr);
    if (pte == 0) {
	pte = VMMACH_RESIDENT_BIT | VMMACH_KRW_PROT | VMMACH_DONT_CACHE_BIT |
	      VirtToPhysPage(Vm_KernPageAllocate());
	SET_ALL_PAGE_MAP(virtAddr, pte);
    } 
#endif
}
#endif


/*
 * ----------------------------------------------------------------------------
 *
 * Vm_UnmapIntelPage --
 *
 *      Deallocate and invalidate a page for the intel chip.  This is a special
 *	case routine that is only for the intel ethernet chip.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The hardware segment table associated with the segment
 *      is modified to invalidate the page.
 *
 * ----------------------------------------------------------------------------
 */
/*ARGSUSED*/
#ifndef sun4c
void
VmMach_UnmapIntelPage(virtAddr) 
    Address	virtAddr;
{
#if defined(sun2)
    PMEG		*pmegPtr;
    Boolean	found = FALSE;

    if (allocatedPMEG != VMMACH_INV_PMEG) {
	/*
	 * Free up the PMEG.
	 */
	/* No flush since this should never be cached. */
	VmMachSetPageMap(virtAddr, (VmMachPTE)0);
	VmMachSetSegMap(virtAddr, VMMACH_INV_PMEG);

	MASTER_LOCK(vmMachMutexPtr);

	/*
	 * This is a little gross, but for some reason this pmeg has a 0
	 * page count.  Since it has the PMEG_DONT_ALLOC flag set, it wasn't
	 * removed from the pmegInuseList in PMEGGet().  So the remove done
	 * in PMEGFree would remove it twice, unless we check.
	 */
	LIST_FORALL(pmegInuseList, (List_Links *)pmegPtr) {
	    if (pmegPtr == &pmegArray[allocatedPMEG]) {
		found = TRUE;
	    }
	}
	if (!found) {
	    pmegPtr = &pmegArray[allocatedPMEG];
	    List_Insert((List_Links *) pmegPtr, LIST_ATREAR(pmegInuseList));
	}
	PMEGFree(allocatedPMEG);

	MASTER_UNLOCK(vmMachMutexPtr);
    } else {
	/*
	 * Restore the saved pte and free the allocated page.
	 */
	/* No flush since this should never be cached. */
	VmMachSetPageMap(virtAddr, intelSavedPTE);
    }
    Vm_KernPageFree(intelPage);
#endif
}
#endif /* sun4c */


static Address		netMemAddr;
static unsigned int	netLastPage;


/*
 * ----------------------------------------------------------------------------
 *
 * InitNetMem --
 *
 *      Initialize the memory mappings for the network.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      PMEGS are allocated and initialized.
 *
 * ----------------------------------------------------------------------------
 */
static void
InitNetMem()
{
    VMMACH_SEG_NUM		pmeg;
    register VMMACH_SEG_NUM	*segTablePtr;
    int				i;
    int				j;
    int				lastSegNum;
    int				segNum;
    Address			virtAddr;

    /*
     * Allocate pmegs  for net mapping.
     */
    segNum = ((unsigned)VMMACH_NET_MAP_START) >> VMMACH_SEG_SHIFT;
    lastSegNum = ((unsigned)(VMMACH_NET_MAP_START+VMMACH_NET_MAP_SIZE-1)) /
			  VMMACH_SEG_SIZE;

    for (i = 0, virtAddr = (Address)VMMACH_NET_MAP_START,
	    segTablePtr = GetHardSegPtr(vm_SysSegPtr->machPtr, segNum);
	 segNum <= lastSegNum;
         i++, virtAddr += VMMACH_SEG_SIZE, segNum++) {
	pmeg = VmMachGetSegMap(virtAddr);
	if (pmeg == VMMACH_INV_PMEG) {
	    *(segTablePtr + i) = PMEGGet(vm_SysSegPtr, segNum, PMEG_DONT_ALLOC);
	    VmMachSetSegMap(virtAddr, (int)*(segTablePtr + i));
	} else {
	    *(segTablePtr + i) = pmeg;
	}
	/*
	 * Propagate the new pmeg mapping to all contexts.
	 */
	for (j = 0; j < VMMACH_NUM_CONTEXTS; j++) {
	    if (j == VMMACH_KERN_CONTEXT) {
		continue;
	    }
	    VmMachSetContextReg(j);
	    VmMachSetSegMap(virtAddr, (int)*(segTablePtr + i));
	}
	VmMachSetContextReg(VMMACH_KERN_CONTEXT);
    }
    /*
     * Repeat for the network memory range. 
     */
    segNum = ((unsigned)VMMACH_NET_MEM_START) >> VMMACH_SEG_SHIFT;
    lastSegNum = ((unsigned)(VMMACH_NET_MEM_START+VMMACH_NET_MEM_SIZE-1)) /
			 VMMACH_SEG_SIZE;

    for (i = 0, virtAddr = (Address)VMMACH_NET_MEM_START,
	    segTablePtr = GetHardSegPtr(vm_SysSegPtr->machPtr, segNum);
	 segNum <= lastSegNum;
         i++, virtAddr += VMMACH_SEG_SIZE, segNum++) {
	pmeg = VmMachGetSegMap(virtAddr);
	if (pmeg == VMMACH_INV_PMEG) {
	    *(segTablePtr + i) = PMEGGet(vm_SysSegPtr, segNum, PMEG_DONT_ALLOC);
	    VmMachSetSegMap(virtAddr, (int)*(segTablePtr + i));
	} else {
	    *(segTablePtr + i) = pmeg;
	}
	/*
	 * Propagate the new pmeg mapping to all contexts.
	 */
	for (j = 0; j < VMMACH_NUM_CONTEXTS; j++) {
	    if (j == VMMACH_KERN_CONTEXT) {
		continue;
	    }
	    VmMachSetContextReg(j);
	    VmMachSetSegMap(virtAddr, (int)*(segTablePtr + i));
	}
	VmMachSetContextReg(VMMACH_KERN_CONTEXT);
    }

    netMemAddr = (Address)VMMACH_NET_MEM_START;
    netLastPage = (((unsigned)VMMACH_NET_MEM_START) >> VMMACH_PAGE_SHIFT) - 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_NetMemAlloc --
 *
 *      Allocate physical memory for a network driver.
 *
 * Results:
 *      The address where the memory is allocated at.
 *
 * Side effects:
 *      Memory allocated.
 *
 * ----------------------------------------------------------------------------
 */
Address
VmMach_NetMemAlloc(numBytes)
    int	numBytes;	/* Number of bytes of memory to allocated. */
{
    VmMachPTE	pte;
    Address	retAddr;
    Address	maxAddr;
    Address	virtAddr;
    static Boolean initialized = FALSE;

    if (!initialized) {
	InitNetMem();
	initialized = TRUE;
    }

    retAddr = netMemAddr;
    netMemAddr += (numBytes + 7) & ~7;	/* is this necessary for sun4? */
    /*
     * Panic if we are out of memory.  
     */
    if (netMemAddr > (Address) (VMMACH_NET_MEM_START + VMMACH_NET_MEM_SIZE)) {
	panic("VmMach_NetMemAlloc: Out of network memory\n");
    }

    maxAddr = (Address) ((netLastPage + 1) * VMMACH_PAGE_SIZE - 1);

    /*
     * Add new pages to the virtual address space until we have added enough
     * to handle this memory request.
     */
    while (netMemAddr - 1 > maxAddr) {
	maxAddr += VMMACH_PAGE_SIZE;
	netLastPage++;
	virtAddr = (Address) (netLastPage << VMMACH_PAGE_SHIFT);
	pte = VMMACH_RESIDENT_BIT | VMMACH_KRW_PROT |
#ifdef sun4c
	      /*
	       * For some reason on the sparcStation, we can't allow the
	       * network pages to be cached. This is really a problem and it
	       * totally breaks the driver.
	       */
	      VMMACH_DONT_CACHE_BIT | 
#endif
	      VirtToPhysPage(Vm_KernPageAllocate());
	SET_ALL_PAGE_MAP(virtAddr, pte);
    }

    bzero(retAddr, numBytes);
    return(retAddr);
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_NetMapPacket --
 *
 *	Map the packet pointed to by the scatter-gather array.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The outScatGathArray is filled in with pointers to where the
 *	packet was mapped in.
 *
 *----------------------------------------------------------------------
 */
void
VmMach_NetMapPacket(inScatGathPtr, scatGathLength, outScatGathPtr)
    register Net_ScatterGather	*inScatGathPtr;
    register int		scatGathLength;
    register Net_ScatterGather	*outScatGathPtr;
{
    register Address	mapAddr;
    register Address	endAddr;
#ifndef sun4c
    int			segNum;
    int			pageNum = 0;
#endif

#ifdef sun4c
    /*
     * The network driver on the sparcstation never accesses the data
     * through the cache, so there should be no need to flush it.
     */
    for (mapAddr = (Address)VMMACH_NET_MAP_START;
	    scatGathLength > 0;
	    scatGathLength--, inScatGathPtr++, outScatGathPtr++) {
        outScatGathPtr->length = inScatGathPtr->length;
        if (inScatGathPtr->length == 0) {
            continue;
        }
        /*
         * Map the piece of the packet in.  Note that we know that a packet
         * piece is no longer than 1536 bytes so we know that we will need
         * at most two page table entries to map a piece in.
         */
        VmMachSetPageMap(mapAddr, VmMachGetPageMap(inScatGathPtr->bufAddr));
        outScatGathPtr->bufAddr = mapAddr +
	    ((unsigned)inScatGathPtr->bufAddr & VMMACH_OFFSET_MASK_INT);
        mapAddr += VMMACH_PAGE_SIZE_INT;
        endAddr = inScatGathPtr->bufAddr + inScatGathPtr->length - 1;
        if (((unsigned)inScatGathPtr->bufAddr & ~VMMACH_OFFSET_MASK_INT) !=
            ((unsigned)endAddr & ~VMMACH_OFFSET_MASK_INT)) {
            VmMachSetPageMap(mapAddr, VmMachGetPageMap(endAddr));
            mapAddr += VMMACH_PAGE_SIZE_INT;
        }
    }
#else
    for (segNum = 0 ; scatGathLength > 0;
	    scatGathLength--, inScatGathPtr++, outScatGathPtr++) {
	outScatGathPtr->length = inScatGathPtr->length;
	if (inScatGathPtr->length == 0) {
	    continue;
	}
	/*
	 * For the first (VMMACH_NUM_NET_SEGS) - 1 elements in the
	 * scatter gather array, map each element into a segment, aligned
	 * with the mapping pages so that cache flushes will be avoided.
	 */
	if (segNum < VMMACH_NUM_NET_SEGS - 1) {
	    /* do silly mapping */
	    mapAddr = (Address)VMMACH_NET_MAP_START +
		    (segNum * VMMACH_SEG_SIZE);
	    /* align to same cache boundary */
	    mapAddr += ((unsigned int)inScatGathPtr->bufAddr &
		    (VMMACH_CACHE_SIZE - 1));
	    /* set addr to beginning of page */
	    mapAddr = (Address)((unsigned int) mapAddr & ~VMMACH_OFFSET_MASK);
	    if (vmMachHasVACache) {
		VmMachFlushPage(mapAddr);
	    }
	    VmMachSetPageMap(mapAddr, VmMachGetPageMap(inScatGathPtr->bufAddr));
	    outScatGathPtr->bufAddr = (Address) ((unsigned)mapAddr +
		    ((unsigned)inScatGathPtr->bufAddr & VMMACH_OFFSET_MASK));
	    mapAddr += VMMACH_PAGE_SIZE_INT;
	    endAddr = (Address)inScatGathPtr->bufAddr +
		    inScatGathPtr->length - 1;
	    if (((unsigned)inScatGathPtr->bufAddr & ~VMMACH_OFFSET_MASK_INT) !=
		((unsigned)endAddr & ~VMMACH_OFFSET_MASK_INT)) {
		if (vmMachHasVACache) {
		    VmMachFlushPage(mapAddr);
		}
		VmMachSetPageMap(mapAddr, VmMachGetPageMap(endAddr));
	    }
	    segNum++;
	} else {
	    /*
	     * For elements beyond the last one, map them all into the
	     * last mapping segment.  Cache flushing will be necessary for
	     * these.
	     */
	    mapAddr = (Address)VMMACH_NET_MAP_START +
		    (segNum * VMMACH_SEG_SIZE) + (pageNum * VMMACH_PAGE_SIZE);
	    if (vmMachHasVACache) {
		VmMachFlushPage(inScatGathPtr->bufAddr);
		VmMachFlushPage(mapAddr);
	    }
	    VmMachSetPageMap(mapAddr, VmMachGetPageMap(inScatGathPtr->bufAddr));
	    outScatGathPtr->bufAddr = (Address) ((unsigned)mapAddr +
		    ((unsigned)inScatGathPtr->bufAddr & VMMACH_OFFSET_MASK));
	    mapAddr += VMMACH_PAGE_SIZE_INT;
	    pageNum++;
	    endAddr = (Address)inScatGathPtr->bufAddr +
		    inScatGathPtr->length - 1;
	    if (((unsigned)inScatGathPtr->bufAddr & ~VMMACH_OFFSET_MASK_INT) !=
		((unsigned)endAddr & ~VMMACH_OFFSET_MASK_INT)) {
		if (vmMachHasVACache) {
		    VmMachFlushPage(endAddr);
		    VmMachFlushPage(mapAddr);
		}
		VmMachSetPageMap(mapAddr, VmMachGetPageMap(endAddr));
		pageNum++;
	    }
	    printf("MapPacket: segNum is %d, pageNum is %d\n", segNum, pageNum);
	}
    }
#endif /* sun4c */
}



/*
 *----------------------------------------------------------------------
 *
 * VmMach_VirtAddrParse --
 *
 *	See if the given address falls into the special mapping segment.
 *	If so parse it for our caller.
 *
 * Results:
 *	TRUE if the address fell into the special mapping segment, FALSE
 *	otherwise.
 *
 * Side effects:
 *	*transVirtAddrPtr may be filled in.
 *
 *----------------------------------------------------------------------
 */
Boolean
VmMach_VirtAddrParse(procPtr, virtAddr, transVirtAddrPtr)
    Proc_ControlBlock		*procPtr;
    Address			virtAddr;
    register	Vm_VirtAddr	*transVirtAddrPtr;
{
    Address	origVirtAddr;
    Boolean	retVal;

#ifdef sun4
    if (!VMMACH_ADDR_CHECK(virtAddr)) {
	panic("VmMach_VirtAddrParse: virt addr 0x%x falls in illegal range!\n",
		virtAddr);
    }
#endif sun4
    if (virtAddr >= (Address)VMMACH_MAP_SEG_ADDR && 
        virtAddr < (Address)mach_KernStart) {
	/*
	 * The address falls into the special mapping segment.  Translate
	 * the address back to the segment that it falls into.
	 */
	transVirtAddrPtr->segPtr = procPtr->vmPtr->machPtr->mapSegPtr;
	origVirtAddr = 
	    (Address)(procPtr->vmPtr->machPtr->mapHardSeg << VMMACH_SEG_SHIFT);
	transVirtAddrPtr->sharedPtr = procPtr->vmPtr->machPtr->sharedPtr;
 	if (transVirtAddrPtr->segPtr->type == VM_SHARED) {
 	    origVirtAddr -= ( transVirtAddrPtr->segPtr->offset
 		    >>(VMMACH_SEG_SHIFT-VMMACH_PAGE_SHIFT))
 		    << VMMACH_SEG_SHIFT;
	    origVirtAddr += segOffset(transVirtAddrPtr)<<VMMACH_PAGE_SHIFT;
 	}
	origVirtAddr += (unsigned int)virtAddr & (VMMACH_SEG_SIZE - 1);
	transVirtAddrPtr->page = (unsigned) (origVirtAddr) >> VMMACH_PAGE_SHIFT;
	transVirtAddrPtr->offset = (unsigned)virtAddr & VMMACH_OFFSET_MASK;
	transVirtAddrPtr->flags = USING_MAPPED_SEG;
	retVal = TRUE;
    } else {
	retVal = FALSE;
    }
    return(retVal);
}



/*
 *----------------------------------------------------------------------
 *
 * VmMach_CopyInProc --
 *
 *	Copy from another process's address space into the current address
 *	space.   This is done by mapping the other processes segment into
 *	the current VAS and then doing the copy.  It assumed that this 
 *	routine is called with the source process locked such that its
 *	VM will not go away while we are doing this copy.
 *
 * Results:
 *	SUCCESS if the copy succeeded, SYS_ARG_NOACCESS if fromAddr is invalid.
 *
 * Side effects:
 *	What toAddr points to is modified.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
VmMach_CopyInProc(numBytes, fromProcPtr, fromAddr, virtAddrPtr,
	      toAddr, toKernel)
    int 	numBytes;		/* The maximum number of bytes to 
					   copy in. */
    Proc_ControlBlock	*fromProcPtr;	/* Which process to copy from.*/
    Address		fromAddr;	/* The address to copy from */
    Vm_VirtAddr		*virtAddrPtr;
    Address		toAddr;		/* The address to copy to */
    Boolean		toKernel;	/* This copy is happening to the
					 * kernel's address space. */
{
    ReturnStatus		status = SUCCESS;
    register VmMach_ProcData	*machPtr;
    Proc_ControlBlock		*toProcPtr;
    int				segOffset;
    int				bytesToCopy;
    int				oldContext;

    toProcPtr = Proc_GetCurrentProc();
    machPtr = toProcPtr->vmPtr->machPtr;
    machPtr->mapSegPtr = virtAddrPtr->segPtr;
    machPtr->mapHardSeg = (unsigned int) (fromAddr) >> VMMACH_SEG_SHIFT;
    machPtr->sharedPtr = virtAddrPtr->sharedPtr;
    if (virtAddrPtr->sharedPtr != (Vm_SegProcList*)NIL) {
	/*
	 * Mangle the segment offset so that it matches the offset
	 * of the mapped segment.
	 */
	if (debugVmStubs) {
	    printf("Copying in shared segment\n");
	}
	machPtr->mapHardSeg -= (virtAddrPtr->sharedPtr->offset<<
		VMMACH_PAGE_SHIFT_INT)>>VMMACH_SEG_SHIFT;
	machPtr->mapHardSeg += machPtr->mapSegPtr->machPtr->offset;
    }

#ifdef sun4
    /*
     * Since this is a cross-address-space copy, we must make sure everything
     * has been flushed to the stack from our windows so that we don't
     * miss stuff on the stack not yet flushed. 
     */
    Mach_FlushWindowsToStack();
#endif

    /*
     * Do a hardware segment's worth at a time until done.
     */
    while (numBytes > 0 && status == SUCCESS) {
	segOffset = (unsigned int)fromAddr & (VMMACH_SEG_SIZE - 1);
	bytesToCopy = VMMACH_SEG_SIZE - segOffset;
	if (bytesToCopy > numBytes) {
	    bytesToCopy = numBytes;
	}
	/*
	 * First try quick and dirty copy.  If it fails, do regular copy.
	 */
	if (fromProcPtr->vmPtr->machPtr->contextPtr != (VmMach_Context *)NIL) {
	    unsigned int	fromContext;
	    unsigned int	toContext;

	    toContext = VmMachGetContextReg();
	    fromContext = fromProcPtr->vmPtr->machPtr->contextPtr->context;

	    status = VmMachQuickNDirtyCopy(bytesToCopy, fromAddr, toAddr,
		fromContext, toContext);
	    VmMachSetContextReg((int)toContext);

	    if (status == SUCCESS) {
		numBytes -= bytesToCopy;
		fromAddr += bytesToCopy;
		toAddr += bytesToCopy;
		continue;
	    }
	}
	/*
	 * Flush segment in context of fromProcPtr.  If the context is NIL, then
	 * we can't and don't have to flush it, since it will be flushed
	 * before being reused.
	 */
	if (vmMachHasVACache &&
	    fromProcPtr->vmPtr->machPtr->contextPtr != (VmMach_Context *)NIL) {

	    oldContext = VmMachGetContextReg();
	    VmMachSetContextReg(
		    (int)fromProcPtr->vmPtr->machPtr->contextPtr->context);
	    VmMachFlushSegment(fromAddr);
	    VmMachSetContextReg(oldContext);
	}
	/*
	 * Push out the hardware segment.
	 */
	WriteHardMapSeg(machPtr);
	/*
	 * Do the copy.
	 */
	toProcPtr->vmPtr->vmFlags |= VM_COPY_IN_PROGRESS;
	status = VmMachDoCopy(bytesToCopy,
			      (Address)(VMMACH_MAP_SEG_ADDR + segOffset),
			      toAddr);
	toProcPtr->vmPtr->vmFlags &= ~VM_COPY_IN_PROGRESS;
	if (status == SUCCESS) {
	    numBytes -= bytesToCopy;
	    fromAddr += bytesToCopy;
	    toAddr += bytesToCopy;
	} else {
	    status = SYS_ARG_NOACCESS;
	}
	/*
	 * Zap the hardware segment.
	 */
	if (vmMachHasVACache) {
	    VmMachFlushSegment((Address) VMMACH_MAP_SEG_ADDR);
	}
	VmMachSetSegMap((Address)VMMACH_MAP_SEG_ADDR, VMMACH_INV_PMEG); 
	machPtr->mapHardSeg++;
    }
    machPtr->mapSegPtr = (struct Vm_Segment *)NIL;
    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_CopyOutProc --
 *
 *	Copy from the current VAS to another processes VAS.  This is done by 
 *	mapping the other processes segment into the current VAS and then 
 *	doing the copy.  It assumed that this routine is called with the dest
 *	process locked such that its VM will not go away while we are doing
 *	the copy.
 *
 * Results:
 *	SUCCESS if the copy succeeded, SYS_ARG_NOACCESS if fromAddr is invalid.
 *
 * Side effects:
 *	What toAddr points to is modified.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
VmMach_CopyOutProc(numBytes, fromAddr, fromKernel, toProcPtr, toAddr,
		   virtAddrPtr)
    int 		numBytes;	/* The maximum number of bytes to 
					   copy in. */
    Address		fromAddr;	/* The address to copy from */
    Boolean		fromKernel;	/* This copy is happening to the
					 * kernel's address space. */
    Proc_ControlBlock	*toProcPtr;	/* Which process to copy from.*/
    Address		toAddr;		/* The address to copy to */
    Vm_VirtAddr		*virtAddrPtr;
{
    ReturnStatus		status = SUCCESS;
    register VmMach_ProcData	*machPtr;
    Proc_ControlBlock		*fromProcPtr;
    int				segOffset;
    int				bytesToCopy;
    int				oldContext;


    fromProcPtr = Proc_GetCurrentProc();
    machPtr = fromProcPtr->vmPtr->machPtr;
    machPtr->mapSegPtr = virtAddrPtr->segPtr;
    machPtr->mapHardSeg = (unsigned int) (toAddr) >> VMMACH_SEG_SHIFT;
    machPtr->sharedPtr = virtAddrPtr->sharedPtr;
    if (virtAddrPtr->sharedPtr != (Vm_SegProcList*)NIL) {
	/*
	 * Mangle the segment offset so that it matches the offset
	 * of the mapped segment.
	 */
	if (debugVmStubs) {
	    printf("Copying out shared segment\n");
	}
	machPtr->mapHardSeg -= (virtAddrPtr->sharedPtr->offset<<
		VMMACH_PAGE_SHIFT_INT)>>VMMACH_SEG_SHIFT;
	machPtr->mapHardSeg += machPtr->mapSegPtr->machPtr->offset;
    }

#ifdef sun4
    /*
     * Since this is a cross-address-space copy, we must make sure everything
     * has been flushed to the stack from our windows so that we don't
     * get stuff from windows overwriting stuff we copy to the stack later
     * when they're flushed.
     */
    Mach_FlushWindowsToStack();
#endif

    /*
     * Do a hardware segment's worth at a time until done.
     */
    while (numBytes > 0 && status == SUCCESS) {
	segOffset = (unsigned int)toAddr & (VMMACH_SEG_SIZE - 1);
	bytesToCopy = VMMACH_SEG_SIZE - segOffset;
	if (bytesToCopy > numBytes) {
	    bytesToCopy = numBytes;
	}
	/*
	 * First try quick and dirty copy.  If it fails, do regular copy.
	 */
	if (toProcPtr->vmPtr->machPtr->contextPtr != (VmMach_Context *)NIL) {
	    unsigned int	fromContext;
	    unsigned int	toContext;

	    fromContext = VmMachGetContextReg();
	    toContext = toProcPtr->vmPtr->machPtr->contextPtr->context;

	    status = VmMachQuickNDirtyCopy(bytesToCopy, fromAddr, toAddr,
		    fromContext, toContext);
	    VmMachSetContextReg((int)fromContext);

	    if (status == SUCCESS) {
		numBytes -= bytesToCopy;
		fromAddr += bytesToCopy;
		toAddr += bytesToCopy;
		continue;
	    }
	}
	/*
	 * Flush segment in context of toProcPtr.  If the context is NIL, then
	 * we can't and don't have to flush it, since it will be flushed before
	 * being re-used.
	 */
	if (vmMachHasVACache &&
	    toProcPtr->vmPtr->machPtr->contextPtr != (VmMach_Context *)NIL) {

	    oldContext = VmMachGetContextReg();
	    VmMachSetContextReg(
		    (int)toProcPtr->vmPtr->machPtr->contextPtr->context);
	    VmMachFlushSegment(toAddr);
	    VmMachSetContextReg(oldContext);
	}
	/*
	 * Push out the hardware segment.
	 */
	WriteHardMapSeg(machPtr);
	/*
	 * Do the copy.
	 */
	fromProcPtr->vmPtr->vmFlags |= VM_COPY_IN_PROGRESS;
	status = VmMachDoCopy(bytesToCopy, fromAddr,
			  (Address) (VMMACH_MAP_SEG_ADDR + segOffset));
	fromProcPtr->vmPtr->vmFlags &= ~VM_COPY_IN_PROGRESS;
	if (status == SUCCESS) {
	    numBytes -= bytesToCopy;
	    fromAddr += bytesToCopy;
	    toAddr += bytesToCopy;
	} else {
	    status = SYS_ARG_NOACCESS;
	}
	/*
	 * Zap the hardware segment.
	 */

	/* Flush this in current context */
	if (vmMachHasVACache) {
	    VmMachFlushSegment((Address) VMMACH_MAP_SEG_ADDR);
	}
	VmMachSetSegMap((Address)VMMACH_MAP_SEG_ADDR, VMMACH_INV_PMEG); 

	machPtr->mapHardSeg++;
    }
    machPtr->mapSegPtr = (struct Vm_Segment *)NIL;
    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * WriteHardMapSeg --
 *
 *	Push the hardware segment map entry out to the hardware for the
 *	given mapped segment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Hardware segment modified.
 *
 *----------------------------------------------------------------------
 */
ENTRY static void
WriteHardMapSeg(machPtr)
    VmMach_ProcData	*machPtr;
{
    MASTER_LOCK(vmMachMutexPtr);

    if (machPtr->contextPtr != (VmMach_Context *) NIL) {
	machPtr->contextPtr->map[MAP_SEG_NUM] = 
	    (int)*GetHardSegPtr(machPtr->mapSegPtr->machPtr,
	    machPtr->mapHardSeg);
    }
    VmMachSetSegMap((Address)VMMACH_MAP_SEG_ADDR, 
	 (int)*GetHardSegPtr(machPtr->mapSegPtr->machPtr, machPtr->mapHardSeg));

    MASTER_UNLOCK(vmMachMutexPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_SetSegProt --
 *
 *	Change the protection in the page table for the given range of bytes
 *	for the given segment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Page table may be modified for the segment.
 *
 *----------------------------------------------------------------------
 */
ENTRY void
VmMach_SetSegProt(segPtr, firstPage, lastPage, makeWriteable)
    register Vm_Segment		*segPtr;    /* Segment to change protection
					       for. */
    register int		firstPage;  /* First page to set protection
					     * for. */
    int				lastPage;   /* First page to set protection
					     * for. */
    Boolean			makeWriteable;/* TRUE => make the pages 
					       *	 writable.
					       * FALSE => make readable only.*/
{
    register	VmMachPTE	pte;
    register	Address		virtAddr;
    register	VMMACH_SEG_NUM	*pmegNumPtr;
    register	PMEG		*pmegPtr;
    register	Boolean		skipSeg = FALSE;
    Boolean			nextSeg = TRUE;
    Address			tVirtAddr;
    Address			pageVirtAddr;
    int				i;
    int				oldContext;

    MASTER_LOCK(vmMachMutexPtr);

    pmegNumPtr = (VMMACH_SEG_NUM *)
	    GetHardSegPtr(segPtr->machPtr, PageToSeg(firstPage)) - 1;
    virtAddr = (Address)(firstPage << VMMACH_PAGE_SHIFT);
    while (firstPage <= lastPage) {
	if (nextSeg) {
	    pmegNumPtr++;
	    if (*pmegNumPtr != VMMACH_INV_PMEG) {
		pmegPtr = &pmegArray[*pmegNumPtr];
		if (pmegPtr->pageCount != 0) {
		    if (vmMachHasVACache) {
			/* Flush this segment in all contexts */
			oldContext =  VmMachGetContextReg();
			for (i = 0; i < VMMACH_NUM_CONTEXTS; i++) {
			    VmMachSetContextReg(i);
			    VmMachFlushSegment(virtAddr);
			}
			VmMachSetContextReg(oldContext);
		    }
		    VmMachSetSegMap(vmMachPTESegAddr, (int)*pmegNumPtr);
		    skipSeg = FALSE;
		} else {
		    skipSeg = TRUE;
		}
	    } else {
		skipSeg = TRUE;
	    }
	    nextSeg = FALSE;
	}
	if (!skipSeg) {
	    /*
	     * Change the hardware page table.
	     */
	    tVirtAddr =
		((unsigned int)virtAddr & VMMACH_PAGE_MASK) + vmMachPTESegAddr;
	    for (i = 0; i < VMMACH_CLUSTER_SIZE; i++) {
		pageVirtAddr = tVirtAddr + i * VMMACH_PAGE_SIZE_INT;
		pte = VmMachGetPageMap(pageVirtAddr);
		if (pte & VMMACH_RESIDENT_BIT) {
		    pte &= ~VMMACH_PROTECTION_FIELD;
		    pte |= makeWriteable ? VMMACH_URW_PROT : VMMACH_UR_PROT;
#ifdef sun4
		    if (virtAddr >= vmStackEndAddr) {
			pte |= VMMACH_DONT_CACHE_BIT;
		    } else {
			pte &= ~VMMACH_DONT_CACHE_BIT;
		    }
#endif /* sun4 */
		    VmMachSetPageMap(pageVirtAddr, pte);
		}
	    }
	    virtAddr += VMMACH_PAGE_SIZE;
	    firstPage++;
	    if (((unsigned int)virtAddr & VMMACH_PAGE_MASK) == 0) {
		nextSeg = TRUE;
	    }
	} else {
	    int	segNum;

	    segNum = PageToSeg(firstPage) + 1;
	    firstPage = SegToPage(segNum);
	    virtAddr = (Address)(firstPage << VMMACH_PAGE_SHIFT);
	    nextSeg = TRUE;
	}
    }

    MASTER_UNLOCK(vmMachMutexPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_SetPageProt --
 *
 *	Set the protection in hardware and software for the given virtual
 *	page.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Page table may be modified for the segment.
 *
 *----------------------------------------------------------------------
 */
ENTRY void
VmMach_SetPageProt(virtAddrPtr, softPTE)
    register	Vm_VirtAddr	*virtAddrPtr;	/* The virtual page to set the
						 * protection for.*/
    Vm_PTE			softPTE;	/* Software pte. */
{
    register	VmMachPTE 	hardPTE;
    register	VmMach_SegData	*machPtr;
    Address   			virtAddr;
    int				pmegNum;
    int				i;
#ifdef sun4
    Address			testVirtAddr;
#endif /* sun4 */
    int				j;
    int				oldContext;

    MASTER_LOCK(vmMachMutexPtr);

    machPtr = virtAddrPtr->segPtr->machPtr;
    pmegNum = *GetHardSegPtr(machPtr, PageToOffSeg(virtAddrPtr->page,
	    virtAddrPtr));
    if (pmegNum != VMMACH_INV_PMEG) {
	virtAddr = ((virtAddrPtr->page << VMMACH_PAGE_SHIFT) & 
			VMMACH_PAGE_MASK) + vmMachPTESegAddr;	
#ifdef sun4
	testVirtAddr = (Address) (virtAddrPtr->page << VMMACH_PAGE_SHIFT);
#endif sun4
	for (i = 0; 
	     i < VMMACH_CLUSTER_SIZE; 
	     i++, virtAddr += VMMACH_PAGE_SIZE_INT) {
	    hardPTE = VmMachReadPTE(pmegNum, virtAddr);
	    hardPTE &= ~VMMACH_PROTECTION_FIELD;
	    hardPTE |= (softPTE & (VM_COW_BIT | VM_READ_ONLY_PROT)) ? 
					VMMACH_UR_PROT : VMMACH_URW_PROT;
#ifdef sun4
	    if (testVirtAddr >= vmStackEndAddr) {
		hardPTE |= VMMACH_DONT_CACHE_BIT;
	    } else {
		hardPTE &= ~VMMACH_DONT_CACHE_BIT;
	    }
#endif /* sun4 */
	    if (vmMachHasVACache) {
		/* Flush this page in all contexts */
		oldContext =  VmMachGetContextReg();
		for (j = 0; j < VMMACH_NUM_CONTEXTS; j++) {
		    VmMachSetContextReg(j);
		    FLUSH_ALL_PAGE(testVirtAddr);
		}
		VmMachSetContextReg(oldContext);
	    }
	    VmMachWritePTE(pmegNum, virtAddr, hardPTE);
	}
    }

    MASTER_UNLOCK(vmMachMutexPtr);
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_AllocCheck --
 *
 *      Determine if this page can be reallocated.  A page can be reallocated
 *	if it has not been referenced or modified.
 *  
 * Results:
 *      None.
 *
 * Side effects:
 *      The given page will be invalidated in the hardware if it has not
 *	been referenced and *refPtr and *modPtr will have the hardware 
 *	reference and modify bits or'd in.
 *
 * ----------------------------------------------------------------------------
 */
ENTRY void
VmMach_AllocCheck(virtAddrPtr, virtFrameNum, refPtr, modPtr)
    register	Vm_VirtAddr	*virtAddrPtr;
    unsigned	int		virtFrameNum;
    register	Boolean		*refPtr;
    register	Boolean		*modPtr;
{
    register VmMach_SegData	*machPtr;
    register VmMachPTE 		hardPTE;  
    int				pmegNum; 
    Address			virtAddr;
    int				i;
    int				origMod;

    MASTER_LOCK(vmMachMutexPtr);

    origMod = *modPtr;

    *refPtr |= refModMap[virtFrameNum] & VMMACH_REFERENCED_BIT;
    *modPtr = refModMap[virtFrameNum] & VMMACH_MODIFIED_BIT;
    if (!*refPtr || !*modPtr) {
	machPtr = virtAddrPtr->segPtr->machPtr;
	pmegNum = *GetHardSegPtr(machPtr, PageToOffSeg(virtAddrPtr->page,
		virtAddrPtr));
	if (pmegNum != VMMACH_INV_PMEG) {
	    hardPTE = 0;
	    virtAddr = 
		((virtAddrPtr->page << VMMACH_PAGE_SHIFT) & VMMACH_PAGE_MASK) + 
		    vmMachPTESegAddr;
	    for (i = 0; i < VMMACH_CLUSTER_SIZE; i++ ) {
		hardPTE |= VmMachReadPTE(pmegNum, 
					virtAddr + i * VMMACH_PAGE_SIZE_INT);
	    }
	    *refPtr |= hardPTE & VMMACH_REFERENCED_BIT;
	    *modPtr |= hardPTE & VMMACH_MODIFIED_BIT;
	}
    }
    if (!*refPtr) {
	/*
	 * Invalidate the page so that it will force a fault if it is
	 * referenced.  Since our caller has blocked all faults on this
	 * page, by invalidating it we can guarantee that the reference and
	 * modify information that we are returning will be valid until
	 * our caller reenables faults on this page.
	 */
	PageInvalidate(virtAddrPtr, virtFrameNum, FALSE);

	if (origMod && !*modPtr) {
	    /*
	     * This page had the modify bit set in software but not in
	     * hardware.
	     */
	    vmStat.notHardModPages++;
	}
    }
    *modPtr |= origMod;

    MASTER_UNLOCK(vmMachMutexPtr);

}


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_GetRefModBits --
 *
 *      Pull the reference and modified bits out of hardware.
 *  
 * Results:
 *      None.
 *
 * Side effects:
 *      
 *
 * ----------------------------------------------------------------------------
 */
ENTRY void
VmMach_GetRefModBits(virtAddrPtr, virtFrameNum, refPtr, modPtr)
    register	Vm_VirtAddr	*virtAddrPtr;
    unsigned	int		virtFrameNum;
    register	Boolean		*refPtr;
    register	Boolean		*modPtr;
{
    register VmMach_SegData	*machPtr;
    register VmMachPTE 		hardPTE;  
    int				pmegNum; 
    Address			virtAddr;
    int				i;

    MASTER_LOCK(vmMachMutexPtr);

    *refPtr = refModMap[virtFrameNum] & VMMACH_REFERENCED_BIT;
    *modPtr = refModMap[virtFrameNum] & VMMACH_MODIFIED_BIT;
    if (!*refPtr || !*modPtr) {
	machPtr = virtAddrPtr->segPtr->machPtr;
	pmegNum = *GetHardSegPtr(machPtr, PageToOffSeg(virtAddrPtr->page,
		virtAddrPtr));
	if (pmegNum != VMMACH_INV_PMEG) {
	    hardPTE = 0;
	    virtAddr = 
		((virtAddrPtr->page << VMMACH_PAGE_SHIFT) & VMMACH_PAGE_MASK) + 
		    vmMachPTESegAddr;
	    for (i = 0; i < VMMACH_CLUSTER_SIZE; i++ ) {
		hardPTE |= VmMachReadPTE(pmegNum, 
					virtAddr + i * VMMACH_PAGE_SIZE_INT);
	    }
	    if (!*refPtr) {
		*refPtr = hardPTE & VMMACH_REFERENCED_BIT;
	    }
	    if (!*modPtr) {
		*modPtr = hardPTE & VMMACH_MODIFIED_BIT;
	    }
	}
    }

    MASTER_UNLOCK(vmMachMutexPtr);
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_ClearRefBit --
 *
 *      Clear the reference bit at the given virtual address.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Hardware reference bit cleared.
 *
 * ----------------------------------------------------------------------------
 */
ENTRY void
VmMach_ClearRefBit(virtAddrPtr, virtFrameNum)
    register	Vm_VirtAddr	*virtAddrPtr;
    unsigned 	int		virtFrameNum;
{
    register	VmMach_SegData	*machPtr;
    int				pmegNum;
    Address			virtAddr;
    int				i;
    VmMachPTE			pte;

    MASTER_LOCK(vmMachMutexPtr);

    refModMap[virtFrameNum] &= ~VMMACH_REFERENCED_BIT;
    machPtr = virtAddrPtr->segPtr->machPtr;
    pmegNum = *GetHardSegPtr(machPtr, PageToOffSeg(virtAddrPtr->page,
	    virtAddrPtr));
    if (pmegNum != VMMACH_INV_PMEG) {
	virtAddr = ((virtAddrPtr->page << VMMACH_PAGE_SHIFT) & 
			VMMACH_PAGE_MASK) + vmMachPTESegAddr;
	for (i = 0; 
	     i < VMMACH_CLUSTER_SIZE;
	     i++, virtAddr += VMMACH_PAGE_SIZE_INT) {
	    pte = VmMachReadPTE(pmegNum, virtAddr);
	    pte &= ~VMMACH_REFERENCED_BIT;
	    VmMachWritePTE(pmegNum, virtAddr, pte);
	}
    }

    MASTER_UNLOCK(vmMachMutexPtr);
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_ClearModBit --
 *
 *      Clear the modified bit at the given virtual address.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Hardware modified bit cleared.
 *
 * ----------------------------------------------------------------------------
 */
ENTRY void
VmMach_ClearModBit(virtAddrPtr, virtFrameNum)
    register	Vm_VirtAddr	*virtAddrPtr;
    unsigned	int		virtFrameNum;
{
    register	VmMach_SegData	*machPtr;
    int				pmegNum;
    Address			virtAddr;
    int				i;
    Vm_PTE			pte;

    MASTER_LOCK(vmMachMutexPtr);

    refModMap[virtFrameNum] &= ~VMMACH_MODIFIED_BIT;
    machPtr = virtAddrPtr->segPtr->machPtr;
    pmegNum = *GetHardSegPtr(machPtr, PageToOffSeg(virtAddrPtr->page,
	    virtAddrPtr));
    if (pmegNum != VMMACH_INV_PMEG) {
	virtAddr = ((virtAddrPtr->page << VMMACH_PAGE_SHIFT) & 
			VMMACH_PAGE_MASK) + vmMachPTESegAddr;
	for (i = 0; 
	     i < VMMACH_CLUSTER_SIZE; 
	     i++, virtAddr += VMMACH_PAGE_SIZE_INT) {
	    pte = VmMachReadPTE(pmegNum, virtAddr);
	    pte &= ~VMMACH_MODIFIED_BIT;
	    VmMachWritePTE(pmegNum, virtAddr, pte);
	}
    }

    MASTER_UNLOCK(vmMachMutexPtr);
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_PageValidate --
 *
 *      Validate a page for the given virtual address.  It is assumed that when
 *      this routine is called that the user context register contains the
 *	context in which the page will be validated.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The page table and hardware segment tables associated with the segment
 *      are modified to validate the page.
 *
 * ----------------------------------------------------------------------------
 */

ENTRY void
VmMach_PageValidate(virtAddrPtr, pte) 
    register	Vm_VirtAddr	*virtAddrPtr;
    Vm_PTE			pte;
{
    register  Vm_Segment	*segPtr;
    register  VMMACH_SEG_NUM	*segTablePtr;
    register  PMEG		*pmegPtr;
    register  int		hardSeg;
    register  VmMachPTE		hardPTE;
    register  VmMachPTE		tHardPTE;
    struct	VmMach_PMEGseg	*pmegSegPtr;
    Vm_PTE    *ptePtr;
    Address	addr;
    Boolean	reLoadPMEG;  	/* TRUE if we had to reload this PMEG. */
    int		i;
    int		tmpSegNum;

    MASTER_LOCK(vmMachMutexPtr);

    segPtr = virtAddrPtr->segPtr;
    addr = (Address) (virtAddrPtr->page << VMMACH_PAGE_SHIFT);
#ifdef sun4
	if (!VMMACH_ADDR_CHECK(addr)) {
	panic("VmMach_PageValidate: virt addr 0x%x falls into illegal range!\n",
		addr);
    }
#endif sun4

    /*
     * Find out the hardware segment that has to be mapped.
     * If this is a mapping segment, this gives us the seg num
     * for the segment in the other process and the segPtr is the mapSegPtr
     * which is set to the segPtr of the other process.
     */

    hardSeg = PageToOffSeg(virtAddrPtr->page, virtAddrPtr);

    segTablePtr = (VMMACH_SEG_NUM *) GetHardSegPtr(segPtr->machPtr, hardSeg);
    pmegPtr = &pmegArray[*segTablePtr]; /* Software seg's pmeg. */
    tmpSegNum = VmMachGetSegMap(addr);  /* Hardware seg's pmeg. */
    if (tmpSegNum != VMMACH_INV_PMEG && tmpSegNum != (int)*segTablePtr) {
	if (!(Proc_GetCurrentProc()->vmPtr->vmFlags & VM_COPY_IN_PROGRESS)) {
	    if (*segTablePtr != VMMACH_INV_PMEG) {
		if (debugVmStubs) {
		    printf("VmMach_PageValidate: multiple pmegs used!\n");
		    printf(" seg = %d, pmeg %d,%d, proc=%x %s\n",
			    segPtr->segNum, *segTablePtr, tmpSegNum,
			    Proc_GetCurrentProc()->processID,
			    Proc_GetCurrentProc()->argString);
		    printf("  old seg = %x\n",
			    pmegArray[tmpSegNum].segInfo.segPtr->segNum);
		    printf("Freeing pmeg %d\n", *segTablePtr);
		}
		PMEGFree(*segTablePtr);
	    }
	    if (debugVmStubs) {
		printf("Multiple segs in hard segment: seg = %d, pmeg %d,%d, proc=%x %s\n",
		    segPtr->segNum, *segTablePtr, tmpSegNum,
		    Proc_GetCurrentProc()->processID,
		    Proc_GetCurrentProc()->argString);
	    }

	    *segTablePtr = (VMMACH_SEG_NUM) tmpSegNum;
	    pmegPtr = &pmegArray[*segTablePtr];
	    if (virtAddrPtr->segPtr->machPtr->pmegInfo.inuse) {
		printf("VmMach_PageValidate: segment sharing table overflow\n");
	    } else {
		pmegSegPtr = &virtAddrPtr->segPtr->machPtr->pmegInfo;
		pmegSegPtr->inuse = 1;
		pmegSegPtr->nextLink = pmegPtr->segInfo.nextLink;
		pmegPtr->segInfo.nextLink = pmegSegPtr;
		pmegSegPtr->segPtr = virtAddrPtr->segPtr;
		pmegSegPtr->hardSegNum = hardSeg;
	    }
	}
    }

    reLoadPMEG = FALSE;
    if (*segTablePtr == VMMACH_INV_PMEG) {
	int flags;
	/*
	 * If there is not already a pmeg for this hardware segment, then get
	 * one and initialize it.  If this is for the kernel then make
	 * sure that the pmeg cannot be taken away from the kernel.
	 * We make an exception for PMEGs allocated only for the block cache.
	 * If we fault on kernel pmeg we reload all the mappings for the
	 * pmeg because we can't tolerate "quick" faults in some places in the
	 * kernel.
	 */
	flags = 0;
	if (segPtr == vm_SysSegPtr) {
	   if (IN_FILE_CACHE_SEG(addr) && vmMachCanStealFileCachePmegs) {
	      /*
	       * In block cache virtual addresses.
	       */
	      reLoadPMEG = TRUE;
	   } else {
	      /*
	       * Normal kernel PMEGs must still be wired.
	       */
	      flags = PMEG_DONT_ALLOC;
	   }
	}
        *segTablePtr = PMEGGet(segPtr, hardSeg, flags);
	pmegPtr = &pmegArray[*segTablePtr];
    } else {
	pmegPtr = &pmegArray[*segTablePtr];
	if (pmegPtr->pageCount == 0) {
	    /*
	     * We are using a PMEG that had a pagecount of 0.  In this case
	     * it was put onto the end of the free pmeg list in anticipation
	     * of someone stealing this empty pmeg.  Now we have to move
	     * it off of the free list.
	     */
	    if (pmegPtr->flags & PMEG_DONT_ALLOC) {
		List_Remove((List_Links *)pmegPtr);
	    } else {
		List_Move((List_Links *)pmegPtr, LIST_ATREAR(pmegInuseList));
	    }
	}
    }
    hardPTE = VMMACH_RESIDENT_BIT | VirtToPhysPage(Vm_GetPageFrame(pte));
#ifdef sun4
    if (addr < (Address) VMMACH_DEV_START_ADDR) {
	    hardPTE &= ~VMMACH_DONT_CACHE_BIT;
    } else {
	hardPTE |= VMMACH_DONT_CACHE_BIT;
    }
#endif /* sun4 */
    if (segPtr == vm_SysSegPtr) {
	int	oldContext;
	/*
	 * Have to propagate the PMEG to all contexts.
	 */
	oldContext = VmMachGetContextReg();
	for (i = 0; i < VMMACH_NUM_CONTEXTS; i++) {
	    VmMachSetContextReg(i);
	    VmMachSetSegMap(addr, (int)*segTablePtr);
	}
	VmMachSetContextReg(oldContext);
	hardPTE |= VMMACH_KRW_PROT;
    } else {
	Proc_ControlBlock	*procPtr;
	VmProcLink		*procLinkPtr;
	VmMach_Context  	*contextPtr;

	procPtr = Proc_GetCurrentProc();
	if (virtAddrPtr->flags & USING_MAPPED_SEG) {
	    addr = (Address) (VMMACH_MAP_SEG_ADDR + 
				((unsigned int)addr & (VMMACH_SEG_SIZE - 1)));
	    /* PUT IT IN SOFTWARE OF MAP AREA FOR PROCESS */
	    procPtr->vmPtr->machPtr->contextPtr->map[MAP_SEG_NUM] =
		    *segTablePtr;
	} else{
	    /* update it for regular seg num */
	    procPtr->vmPtr->machPtr->contextPtr->map[hardSeg] = *segTablePtr;
	}
	VmMachSetSegMap(addr, (int)*segTablePtr);

        if (segPtr != (Vm_Segment *) NIL) {
            LIST_FORALL(segPtr->procList, (List_Links *)procLinkPtr) {
		if (procLinkPtr->procPtr->vmPtr != (Vm_ProcInfo *) NIL &&
			procLinkPtr->procPtr->vmPtr->machPtr !=
			(VmMach_ProcData *) NIL &&
			(contextPtr =
			procLinkPtr->procPtr->vmPtr->machPtr->contextPtr) !=
			(VmMach_Context *) NIL) {
		    contextPtr->map[hardSeg] = *segTablePtr;
		}
	    }
	}

	if ((pte & (VM_COW_BIT | VM_READ_ONLY_PROT)) ||
		(virtAddrPtr->flags & VM_READONLY_SEG)) {
	    hardPTE |= VMMACH_UR_PROT;
	} else {
	    hardPTE |= VMMACH_URW_PROT;
	}
    }
    tHardPTE = VmMachGetPageMap(addr);
    if (tHardPTE & VMMACH_RESIDENT_BIT) {
	hardPTE |= tHardPTE & (VMMACH_REFERENCED_BIT | VMMACH_MODIFIED_BIT);
	for (i = 1; i < VMMACH_CLUSTER_SIZE; i++ ) {
	    hardPTE |= VmMachGetPageMap(addr + i * VMMACH_PAGE_SIZE_INT) & 
			    (VMMACH_REFERENCED_BIT | VMMACH_MODIFIED_BIT);
	}
    } else {
	if (*segTablePtr == VMMACH_INV_PMEG) {
	    panic("Invalid pmeg\n");
	}
	pmegArray[*segTablePtr].pageCount++;
    }
    /* Flush something? */
    SET_ALL_PAGE_MAP(addr, hardPTE);
    if (reLoadPMEG) {
	/*
	 * Reload all pte's for this pmeg.
	 */
	unsigned int a = (hardSeg << VMMACH_SEG_SHIFT);
	int	pageCount = 0;
	for (i = 0; i < VMMACH_NUM_PAGES_PER_SEG; i++ ) { 
	    ptePtr = VmGetPTEPtr(vm_SysSegPtr, (a >> VMMACH_PAGE_SHIFT));
	    if ((*ptePtr & VM_PHYS_RES_BIT)) {
		hardPTE = VMMACH_RESIDENT_BIT | VMMACH_KRW_PROT | 
				VirtToPhysPage(Vm_GetPageFrame(*ptePtr));
		SET_ALL_PAGE_MAP(a, hardPTE);
		pageCount++;
	     }
	     a += VMMACH_PAGE_SIZE;
	}
	if (pmegPtr-pmegArray == VMMACH_INV_PMEG) {
	    panic("Invalid pmeg\n");
	}
	pmegPtr->pageCount = pageCount;
    }

    MASTER_UNLOCK(vmMachMutexPtr);
    return;
}


/*
 * ----------------------------------------------------------------------------
 *
 * PageInvalidate --
 *
 *      Invalidate a page for the given segment.  
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The page table and hardware segment tables associated with the segment
 *      are modified to invalidate the page.
 *
 * ----------------------------------------------------------------------------
 */
static void
PageInvalidate(virtAddrPtr, virtPage, segDeletion) 
    register	Vm_VirtAddr	*virtAddrPtr;
    unsigned 	int		virtPage;
    Boolean			segDeletion;
{
    register VmMach_SegData	*machPtr;
    register PMEG		*pmegPtr;
    VmMachPTE			hardPTE;
    int				pmegNum;
    Address			addr;
    int				i;
    Address			testVirtAddr;
    VmProcLink      		*flushProcLinkPtr;
    Vm_Segment      		*flushSegPtr;
    Vm_ProcInfo     		*flushVmPtr;
    VmMach_ProcData 		*flushMachPtr;
    VmMach_Context  		*flushContextPtr;
    unsigned int    		flushContext;
    unsigned int    		oldContext;

    refModMap[virtPage] = 0;
    if (segDeletion) {
	return;
    }
    machPtr = virtAddrPtr->segPtr->machPtr;
    pmegNum = *GetHardSegPtr(machPtr, PageToOffSeg(virtAddrPtr->page,
	    virtAddrPtr));
    if (pmegNum == VMMACH_INV_PMEG) {
	return;
    }
    testVirtAddr = (Address) (virtAddrPtr->page << VMMACH_PAGE_SHIFT);
    addr = ((virtAddrPtr->page << VMMACH_PAGE_SHIFT) &
				VMMACH_PAGE_MASK) + vmMachPTESegAddr;
#ifdef sun4
    if (!VMMACH_ADDR_CHECK(addr)) {
	panic("PageInvalidate: virt addr 0x%x falls into illegal range!\n",
		addr);
    }
#endif sun4
    hardPTE = VmMachReadPTE(pmegNum, addr);
    /*
     * Invalidate the page table entry.  There's no need to flush the page if
     * the invalidation is due to segment deletion, since the whole segment
     * will already have been flushed.  Flush the page in the context in which
     * it was validated.
     */
    if (!segDeletion && vmMachHasVACache) {
	int	flushedP = FALSE;

        flushSegPtr = virtAddrPtr->segPtr;
        if (flushSegPtr != (Vm_Segment *) NIL) {
            LIST_FORALL(flushSegPtr->procList, (List_Links *)flushProcLinkPtr) {
                flushVmPtr = flushProcLinkPtr->procPtr->vmPtr;
                if (flushVmPtr != (Vm_ProcInfo *) NIL) {
                    flushMachPtr = flushVmPtr->machPtr;
                    if (flushMachPtr != (VmMach_ProcData *) NIL) {
                        flushContextPtr = flushMachPtr->contextPtr;
                        if (flushContextPtr != (VmMach_Context *) NIL) {
                            flushContext = flushContextPtr->context;
                            /* save old context */
                            oldContext = VmMachGetContextReg();
                            /* move to page's context */
                            VmMachSetContextReg((int)flushContext);
                            /* flush page in its context */
                            FLUSH_ALL_PAGE(testVirtAddr);
                            /* back to old context */
                            VmMachSetContextReg((int)oldContext);
			    flushedP = TRUE;
                        }
                    }
                }
            }
        }
	if (!flushedP) {
	    FLUSH_ALL_PAGE(testVirtAddr);
	}
    }
    for (i = 0; i < VMMACH_CLUSTER_SIZE; i++, addr += VMMACH_PAGE_SIZE_INT) {
	VmMachWritePTE(pmegNum, addr, (VmMachPTE)0);
    }
    pmegPtr = &pmegArray[pmegNum];
    if (hardPTE & VMMACH_RESIDENT_BIT) {
	pmegPtr->pageCount--;
	if (pmegPtr->pageCount == 0) {
	    /*
	     * When the pageCount goes to zero, the pmeg is put onto the end
	     * of the free list so that it can get freed if someone else
	     * needs a pmeg.  It isn't freed here because there is a fair
	     * amount of overhead when freeing a pmeg so its best to keep
	     * it around in case it is needed again.
	     */
	    if (pmegPtr->flags & PMEG_DONT_ALLOC) {
		List_Insert((List_Links *)pmegPtr, 
			    LIST_ATREAR(pmegFreeList));
	    } else {
		List_Move((List_Links *)pmegPtr, 
			  LIST_ATREAR(pmegFreeList));
	    }
	}
    }
    return;
}



/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_PageInvalidate --
 *
 *      Invalidate a page for the given segment.  
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The page table and hardware segment tables associated with the segment
 *      are modified to invalidate the page.
 *
 * ----------------------------------------------------------------------------
 */
ENTRY void
VmMach_PageInvalidate(virtAddrPtr, virtPage, segDeletion) 
    register	Vm_VirtAddr	*virtAddrPtr;
    unsigned 	int		virtPage;
    Boolean			segDeletion;
{
    MASTER_LOCK(vmMachMutexPtr);

    PageInvalidate(virtAddrPtr, virtPage, segDeletion);

    MASTER_UNLOCK(vmMachMutexPtr);
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_PinUserPages --
 *
 *	Force a user page to be resident in memory.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
void
VmMach_PinUserPages(mapType, virtAddrPtr, lastPage)
    int		mapType;
    Vm_VirtAddr	*virtAddrPtr;
    int		lastPage;
{
    int				*intPtr;
    int				dummy;
    register VmMach_SegData	*machPtr;
    register int		firstSeg;
    register int		lastSeg;

    machPtr = virtAddrPtr->segPtr->machPtr;

    firstSeg = PageToOffSeg(virtAddrPtr->page, virtAddrPtr);
    lastSeg = PageToOffSeg(lastPage, virtAddrPtr);
    /*
     * Lock down the PMEG behind the first segment.
     */
    intPtr = (int *) (virtAddrPtr->page << VMMACH_PAGE_SHIFT);
    while (!PMEGLock(machPtr, firstSeg)) {
	/*
	 * Touch the page to bring it into memory.  We know that we can
	 * safely touch it because we wouldn't have been called if these
	 * weren't good addresses.
	 */
	dummy = *intPtr;
    }
    /*
     * Lock down the rest of the segments.
     */
    for (firstSeg++; firstSeg <= lastSeg; firstSeg++) {
	intPtr = (int *)(firstSeg << VMMACH_SEG_SHIFT);
	while (!PMEGLock(machPtr, firstSeg)) {
	    dummy = *intPtr;
	}
    }
#ifdef lint
    dummy = dummy;
#endif
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_UnpinUserPages --
 *
 *	Allow a page that was pinned to be unpinned.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
ENTRY void
VmMach_UnpinUserPages(virtAddrPtr, lastPage)
    Vm_VirtAddr	*virtAddrPtr;
    int		lastPage;
{
    register int	firstSeg;
    register int	lastSeg;
    int			pmegNum;
    register VmMach_SegData	*machPtr;

    MASTER_LOCK(vmMachMutexPtr);

    machPtr = virtAddrPtr->segPtr->machPtr;
    firstSeg = PageToOffSeg(virtAddrPtr->page, virtAddrPtr);
    lastSeg = PageToOffSeg(lastPage, virtAddrPtr);
    for (; firstSeg <= lastSeg; firstSeg++) {
	pmegNum = *GetHardSegPtr(machPtr, firstSeg);
	if (pmegNum == VMMACH_INV_PMEG) {
	    MASTER_UNLOCK(vmMachMutexPtr);
	    panic("Pinned PMEG invalid???\n");
	    return;
	}
	pmegArray[pmegNum].lockCount--;
    }

    MASTER_UNLOCK(vmMachMutexPtr);
    return;
}


/*
 ----------------------------------------------------------------------
 *
 * VmMach_MapInDevice --
 *
 *	Map a device at some physical address into kernel virtual address.
 *	This is for use by the controller initialization routines.
 *	This routine looks for a free page in the special range of
 *	kernel virtual that is reserved for this kind of thing and
 *	sets up the page table so that it references the device.
 *
 * Results:
 *	The kernel virtual address needed to reference the device is returned.
 *
 * Side effects:
 *	The hardware page table is modified.  This may steal another
 *	page from kernel virtual space, unless a page can be cleverly re-used.
 *
 *----------------------------------------------------------------------
 */
Address
VmMach_MapInDevice(devPhysAddr, type)
    Address	devPhysAddr;	/* Physical address of the device to map in */
    int		type;		/* Value for the page table entry type field.
				 * This depends on the address space that
				 * the devices live in, ie. VME D16 or D32 */
{
    Address 		virtAddr;
    Address		freeVirtAddr = (Address)0;
    Address		freePMEGAddr = (Address)0;
    int			page;
    int			pageFrame;
    VmMachPTE		pte;

    /*
     * Get the page frame for the physical device so we can
     * compare it against existing pte's.
     */
    pageFrame = ((unsigned)devPhysAddr >> VMMACH_PAGE_SHIFT_INT)
	& VMMACH_PAGE_FRAME_FIELD;

    /*
     * Spin through the segments and their pages looking for a free
     * page or a virtual page that is already mapped to the physical page.
     */
    for (virtAddr = (Address)VMMACH_DEV_START_ADDR;
         virtAddr < (Address)VMMACH_DEV_END_ADDR; ) {
	if (VmMachGetSegMap(virtAddr) == VMMACH_INV_PMEG) {
	    /* 
	     * If we can't find any free mappings we can use this PMEG.
	     */
	    if (freePMEGAddr == 0) {
		freePMEGAddr = virtAddr;
	    }
	    virtAddr += VMMACH_SEG_SIZE;
	    continue;
	}
	/*
	 * Careful, use the correct page size when incrementing virtAddr.
	 * Use the real hardware size (ignore software klustering) because
	 * we are at a low level munging page table entries ourselves here.
	 */
	for (page = 0;
	     page < VMMACH_NUM_PAGES_PER_SEG_INT;
	     page++, virtAddr += VMMACH_PAGE_SIZE_INT) {
	    pte = VmMachGetPageMap(virtAddr);
	    if (!(pte & VMMACH_RESIDENT_BIT)) {
	        if (freeVirtAddr == 0) {
		    /*
		     * Note the unused page in this special area of the
		     * kernel virtual address space.
		     */
		    freeVirtAddr = virtAddr;
		}
	    } else if ((pte & VMMACH_PAGE_FRAME_FIELD) == pageFrame &&
		       VmMachGetPageType(pte) == type) {
		/*
		 * A page is already mapped for this physical address.
		 */
		return(virtAddr + ((int)devPhysAddr & VMMACH_OFFSET_MASK_INT));
	    }
	}
    }

    pte = VMMACH_RESIDENT_BIT | VMMACH_KRW_PROT | pageFrame;
#if defined(sun3) || defined(sun4)	/* Not just for porting purposes */
    pte |= VMMACH_DONT_CACHE_BIT;
#endif
    VmMachSetPageType(pte, type);
    if (freeVirtAddr != 0) {
	VmMachSetPageMap(freeVirtAddr, pte);
	/*
	 * Return the kernel virtual address used to access it.
	 */
	return(freeVirtAddr + ((int)devPhysAddr & VMMACH_OFFSET_MASK_INT));
    } else if (freePMEGAddr != 0) {
	int oldContext;
	int pmeg;
	int i;

	/*
	 * Map in a new PMEG so we can use it for mapping.
	 */
	pmeg = PMEGGet(vm_SysSegPtr, 
		       (int) ((unsigned)freePMEGAddr >> VMMACH_SEG_SHIFT),
		       PMEG_DONT_ALLOC);
	oldContext = VmMachGetContextReg();
	for (i = 0; i < VMMACH_NUM_CONTEXTS; i++) {
	    VmMachSetContextReg(i);
	    VmMachSetSegMap(freePMEGAddr, pmeg);
	}
	VmMachSetContextReg(oldContext);
	VmMachSetPageMap(freePMEGAddr, pte);
	return(freePMEGAddr + ((int)devPhysAddr & VMMACH_OFFSET_MASK_INT));
    } else {
	return((Address)NIL);
    }
}

/*
 ----------------------------------------------------------------------
 *
 * VmMach_MapInBigDevice --
 *
 *	Map a device at some physical address into kernel virtual address.
 *	This is for use by the controller initialization routines.
 *	This is suitable for devices that need to map in more than
 *	one page of contiguous memory.
 *	The routine sets up the page table so that it references the device.
 *
 * Results:
 *	The kernel virtual address needed to reference the device is returned.
 *	NIL is returned upon failure.
 *
 * Side effects:
 *	The hardware page table is modified.  This may steal
 *	pages from kernel virtual space, unless pages can be cleverly re-used.
 *
 *----------------------------------------------------------------------
 */
Address
VmMach_MapInBigDevice(devPhysAddr, numBytes, type)
    Address	devPhysAddr;	/* Physical address of the device to map in. */
    int		numBytes;	/* Bytes needed by device. */
    int		type;		/* Value for the page table entry type field.
				 * This depends on the address space that
				 * the devices live in, ie. VME D16 or D32. */
{
    Address 		virtAddr;
    Address		freeVirtAddr = (Address)0;
    Address		freePMEGAddr = (Address)0;
    int			page;
    int			pageFrame;
    VmMachPTE		pte;
    int			numPages;
    int			i;
    Boolean		foundPages;

    /*
     * Get the page frame for the physical device so we can
     * compare it against existing pte's.
     */
    pageFrame = ((unsigned)devPhysAddr >> VMMACH_PAGE_SHIFT_INT)
	& VMMACH_PAGE_FRAME_FIELD;

    numPages = (numBytes / VMMACH_PAGE_SIZE_INT) + 1;
    if (numPages > VMMACH_NUM_PAGES_PER_SEG_INT) {
	printf("Can only map in one segment's worth of pages right now.\n");
	return (Address) NIL;
    }
    /* For only one pages, just call the old routine. */
    if (numPages <= 1) {
	return VmMach_MapInDevice(devPhysAddr, type);
    }

    /*
     * Spin through the segments and their pages looking for a free
     * page or a virtual page that is already mapped to the physical page.
     */
    for (virtAddr = (Address)VMMACH_DEV_START_ADDR;
         virtAddr < (Address)VMMACH_DEV_END_ADDR; ) {
	if (VmMachGetSegMap(virtAddr) == VMMACH_INV_PMEG) {
	    /* 
	     * If we can't find any free mappings we can use this PMEG.
	     */
	    if (freePMEGAddr == 0) {
		freePMEGAddr = virtAddr;
	    }
	    virtAddr += VMMACH_SEG_SIZE;
	    continue;
	}
	/*
	 * Careful, use the correct page size when incrementing virtAddr.
	 * Use the real hardware size (ignore software klustering) because
	 * we are at a low level munging page table entries ourselves here.
	 */
	foundPages = FALSE;
	for (page = 0;
	     page < VMMACH_NUM_PAGES_PER_SEG_INT;
	     page++, virtAddr += VMMACH_PAGE_SIZE_INT) {

	    /* Are there enough pages left in the segment? */
	    if (VMMACH_NUM_PAGES_PER_SEG_INT - page < numPages) {
		/* If we just continue, virtAddr will be incremented okay. */
		continue;
	    }

	    pte = VmMachGetPageMap(virtAddr);
	    if ((pte & VMMACH_RESIDENT_BIT) &&
		(pte & VMMACH_PAGE_FRAME_FIELD) == pageFrame &&
		       VmMachGetPageType(pte) == type) {
		/*
		 * A page is already mapped for this physical address.
		 */
		printf("A page is already mapped for this device!\n");
		return (Address) NIL;
#ifdef NOTDEF
		/*
		 * Instead, we could loop through trying to find all
		 * mapped pages, but I don't think we'll ever find any
		 * for these devices.
		 */
		return (virtAddr + ((int)devPhysAddr & VMMACH_OFFSET_MASK_INT));
#endif NOTDEF
	    }

	    if (!(pte & VMMACH_RESIDENT_BIT)) {
		/*
		 * Note the unused page in this special area of the
		 * kernel virtual address space.
		 */
		freeVirtAddr = virtAddr;

		/* See if we have enough other consecutive pages. */
		for (i = 1; i < numPages; i++) {
		    pte = VmMachGetPageMap(virtAddr +
			    (i * VMMACH_PAGE_SIZE_INT));
		    /* If this is already taken, give up and continue. */
		    if (pte & VMMACH_RESIDENT_BIT) {
			/* Maybe I should check if it's this device mapped? */
			break;
		    }
		}
		/* Did we find enough pages? */
		if (i == numPages) {
		    foundPages = TRUE;
		    break;
		}
		/* The address wasn't good. */
		freeVirtAddr = (Address) 0;

		/* So that we'll test the right page next time around. */
		page += i;
		virtAddr += (i * VMMACH_PAGE_SIZE_INT);
	    }
	}
	if (foundPages) {
	    break;
	}
    }

    /* Did we find a set of pages? */
    if (freeVirtAddr != 0) {
	virtAddr = freeVirtAddr;
	for (i = 0; i < numPages; i++) {
	    pte = VMMACH_RESIDENT_BIT | VMMACH_KRW_PROT | pageFrame;
#if defined(sun3) || defined(sun4)
	    pte |= VMMACH_DONT_CACHE_BIT;
#endif
	    VmMachSetPageType(pte, type);
	    VmMachSetPageMap(virtAddr, pte);
	    pageFrame++;
	    virtAddr += VMMACH_PAGE_SIZE_INT;
	}

	/*
	 * Return the kernel virtual address used to access it.
	 */
	return (freeVirtAddr + ((int)devPhysAddr & VMMACH_OFFSET_MASK_INT));

    /* Or did we find a whole free pmeg? */
    } else if (freePMEGAddr != 0) {
	int oldContext;
	int pmeg;

	/*
	 * Map in a new PMEG so we can use it for mapping.
	 */
	pmeg = PMEGGet(vm_SysSegPtr, 
		       (int) ((unsigned)freePMEGAddr >> VMMACH_SEG_SHIFT),
		       PMEG_DONT_ALLOC);
	oldContext = VmMachGetContextReg();
	for (i = 0; i < VMMACH_NUM_CONTEXTS; i++) {
	    VmMachSetContextReg(i);
	    VmMachSetSegMap(freePMEGAddr, pmeg);
	}
	VmMachSetContextReg(oldContext);

	virtAddr = freePMEGAddr;
	for (i = 0; i < numPages; i++) {
	    pte = VMMACH_RESIDENT_BIT | VMMACH_KRW_PROT | pageFrame;
#if defined(sun3) || defined(sun4)
	    pte |= VMMACH_DONT_CACHE_BIT;
#endif
	    VmMachSetPageType(pte, type);
	    VmMachSetPageMap(virtAddr, pte);
	    pageFrame++;
	    virtAddr += VMMACH_PAGE_SIZE_INT;
	}

	return (freePMEGAddr + ((int)devPhysAddr & VMMACH_OFFSET_MASK_INT));

    }

    /* Nothing was found. */
    return (Address) NIL;
}


/*----------------------------------------------------------------------
 *
 * DevBufferInit --
 *
 *	Initialize a range of virtual memory to allocate from out of the
 *	device memory space.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The buffer struct is initialized and the hardware page map is zeroed
 *	out in the range of addresses.
 *
 *----------------------------------------------------------------------
 */
INTERNAL static void
DevBufferInit()
{
    Address		virtAddr;
    unsigned char	pmeg;
    int			oldContext;
    int			i;
    Address	baseAddr;
    Address	endAddr;

    /*
     * Round base up to next page boundary and end down to page boundary.
     */
    baseAddr = (Address)VMMACH_DMA_START_ADDR;
    endAddr = (Address)(VMMACH_DMA_START_ADDR + VMMACH_DMA_SIZE);

    /* 
     * Set up the hardware pages tables in the range of addresses given.
     */
    for (virtAddr = baseAddr; virtAddr < endAddr; ) {
	if (VmMachGetSegMap(virtAddr) != VMMACH_INV_PMEG) {
	    printf("DevBufferInit: DMA space already valid at 0x%x\n",
		   (unsigned int) virtAddr);
	}
	/* 
	 * Need to allocate a PMEG.
	 */
	pmeg = PMEGGet(vm_SysSegPtr, 
		       (int) ((unsigned)virtAddr >> VMMACH_SEG_SHIFT),
		       PMEG_DONT_ALLOC);
	oldContext = VmMachGetContextReg();
	for (i = 0; i < VMMACH_NUM_CONTEXTS; i++) {
	    VmMachSetContextReg(i);
	    VmMachSetSegMap(virtAddr, (int)pmeg);
	}
	VmMachSetContextReg(oldContext);
	virtAddr += VMMACH_SEG_SIZE;
    }
    return;
}


static	Boolean	dmaPageBitMap[VMMACH_DMA_SIZE / VMMACH_PAGE_SIZE_INT];

static Boolean dmaInitialized = FALSE;

/*
 ----------------------------------------------------------------------
 *
 * VmMach_DMAAlloc --
 *
 *	Allocate a set of virtual pages to a routine for mapping purposes.
 *	
 * Results:
 *	Pointer into kernel virtual address space of where to access the
 *	memory, or NIL if the request couldn't be satisfied.
 *
 * Side effects:
 *	The hardware page table is modified.
 *
 *----------------------------------------------------------------------
 */
ENTRY Address
VmMach_DMAAlloc(numBytes, srcAddr)
    int		numBytes;		/* Number of bytes to map in. */
    Address	srcAddr;	/* Kernel virtual address to start mapping in.*/
{
    Address	beginAddr;
    Address	endAddr;
    int		numPages;
    int		i, j;
    VmMachPTE	pte;
    Boolean	foundIt = FALSE;
    Address	newAddr;
 
    MASTER_LOCK(vmMachMutexPtr);
    if (!dmaInitialized) {
	/* Where to allocate the memory from. */
	dmaInitialized = TRUE;
	DevBufferInit();
    }

    /* calculate number of pages needed */
						/* beginning of first page */
    beginAddr = (Address) (((unsigned int)(srcAddr)) & ~VMMACH_OFFSET_MASK_INT);
						/* beginning of last page */
    endAddr = (Address) ((((unsigned int) srcAddr) + numBytes - 1) &
	    ~VMMACH_OFFSET_MASK_INT);
    numPages = (((unsigned int) endAddr) >> VMMACH_PAGE_SHIFT_INT) -
	    (((unsigned int) beginAddr) >> VMMACH_PAGE_SHIFT_INT) + 1;

    /* see if request can be satisfied */
    for (i = 0; i < (VMMACH_DMA_SIZE / VMMACH_PAGE_SIZE_INT); i++) {
	if (dmaPageBitMap[i] == 1) {
	    continue;
	}
	/*
	 * Must be aligned in the cache to avoid write-backs of stale data
	 * from other references to stuff on this page.
	 */
	newAddr = (Address)(VMMACH_DMA_START_ADDR + (i * VMMACH_PAGE_SIZE_INT));
	if (((unsigned int) newAddr & (VMMACH_CACHE_SIZE - 1)) !=
		((unsigned int) beginAddr & (VMMACH_CACHE_SIZE - 1))) {
	    continue;
	}
	for (j = 1; j < numPages &&
		((i + j) < (VMMACH_DMA_SIZE / VMMACH_PAGE_SIZE_INT)); j++) {
	    if (dmaPageBitMap[i + j] == 1) {
		break;
	    }
	}
	if (j == numPages &&
		((i + j) < (VMMACH_DMA_SIZE / VMMACH_PAGE_SIZE_INT))) {
	    foundIt = TRUE;
	    break;
	}
    }
    if (!foundIt) {
	MASTER_UNLOCK(vmMachMutexPtr);
	panic(
	    "VmMach_DMAAlloc: unable to satisfy request for %d bytes at 0x%x\n",
		numBytes, srcAddr);
#ifdef NOTDEF
	return (Address) NIL;
#endif NOTDEF
    }
    for (j = 0; j < numPages; j++) {
	dmaPageBitMap[i + j] = 1;	/* allocate page */
	pte = VmMachGetPageMap(srcAddr);
	pte |= VMMACH_RESIDENT_BIT | VMMACH_KRW_PROT;
	VmMachSetPageMap(((i + j) * VMMACH_PAGE_SIZE_INT) +
		VMMACH_DMA_START_ADDR, pte);
	srcAddr += VMMACH_PAGE_SIZE_INT;
    }
    beginAddr = (Address) (VMMACH_DMA_START_ADDR + (i * VMMACH_PAGE_SIZE_INT) +
	    (((unsigned int) srcAddr) & VMMACH_OFFSET_MASK_INT));

    MASTER_UNLOCK(vmMachMutexPtr);
    return beginAddr;
}

/*
 ----------------------------------------------------------------------
 *
 * VmMach_DMAAllocContiguous --
 *
 *	WARNING:  this routine doesn't work yet!!
 *	Allocate a set of virtual pages to a routine for mapping purposes.
 *	
 * Results:
 *	Pointer into kernel virtual address space of where to access the
 *	memory, or NIL if the request couldn't be satisfied.
 *
 * Side effects:
 *	The hardware page table is modified.
 *
 *----------------------------------------------------------------------
 */
#ifndef sun4c
ReturnStatus
VmMach_DMAAllocContiguous(inScatGathPtr, scatGathLength, outScatGathPtr)
    register Net_ScatterGather	*inScatGathPtr;
    register int		scatGathLength;
    register Net_ScatterGather	*outScatGathPtr;
{
    Address	beginAddr = 0;
    Address	endAddr;
    int		numPages;
    int		i, j;
    VmMachPTE	pte;
    Boolean	foundIt = FALSE;
    int		virtPage;
    Net_ScatterGather		*inPtr;
    Net_ScatterGather		*outPtr;
    int				pageOffset;
    Address			srcAddr;
    Address			newAddr;

    if (!dmaInitialized) {
	dmaInitialized = TRUE;
	DevBufferInit();
    }
    /* calculate number of pages needed */
    inPtr = inScatGathPtr;
    outPtr = outScatGathPtr;
    numPages = 0;
    for (i = 0; i < scatGathLength; i++) {
	if (inPtr->length > 0) {
	    /* beginning of first page */
	    beginAddr = (Address) (((unsigned int)(inPtr->bufAddr)) & 
		    ~VMMACH_OFFSET_MASK_INT);
	    /* beginning of last page */
	    endAddr = (Address) ((((unsigned int) inPtr->bufAddr) + 
		inPtr->length - 1) & ~VMMACH_OFFSET_MASK_INT);
	    /* 
	     * Temporarily store the number of pages in the out scatter/gather
	     * array.
	     */
	    outPtr->length =
		    (((unsigned int) endAddr) >> VMMACH_PAGE_SHIFT_INT) -
		    (((unsigned int) beginAddr) >> VMMACH_PAGE_SHIFT_INT) + 1;
	} else {
	    outPtr->length = 0;
	}
	if ((i == 0) && (outPtr->length != 1)) {
	    panic("Help! Help! I'm being repressed!\n");
	}
	numPages += outPtr->length;
	inPtr++;
	outPtr++;
    }

    /* see if request can be satisfied */
    for (i = 0; i < (VMMACH_DMA_SIZE / VMMACH_PAGE_SIZE_INT); i++) {
	if (dmaPageBitMap[i] == 1) {
	    continue;
	}
	/*
	 * Must be aligned in the cache to avoid write-backs of stale data
	 * from other references to stuff on this page.
	 */
	newAddr = (Address)(VMMACH_DMA_START_ADDR + (i * VMMACH_PAGE_SIZE_INT));
	if (((unsigned int) newAddr & (VMMACH_CACHE_SIZE - 1)) !=
		((unsigned int) beginAddr & (VMMACH_CACHE_SIZE - 1))) {
	    continue;
	}
	for (j = 1; j < numPages &&
		((i + j) < (VMMACH_DMA_SIZE / VMMACH_PAGE_SIZE_INT)); j++) {
	    if (dmaPageBitMap[i + j] == 1) {
		break;
	    }
	}
	if (j == numPages &&
		((i + j) < (VMMACH_DMA_SIZE / VMMACH_PAGE_SIZE_INT))) {
	    foundIt = TRUE;
	    break;
	}
    }
    if (!foundIt) {
	return FAILURE;
    }
    pageOffset = i;
    inPtr = inScatGathPtr;
    outPtr = outScatGathPtr;
    for (i = 0; i < scatGathLength; i++) {
	srcAddr = inPtr->bufAddr;
	numPages = outPtr->length;
	for (j = 0; j < numPages; j++) {
	    dmaPageBitMap[pageOffset + j] = 1;	/* allocate page */
	    virtPage = ((unsigned int) srcAddr) >> VMMACH_PAGE_SHIFT;
	    pte = VMMACH_RESIDENT_BIT | VMMACH_KRW_PROT |
		  VirtToPhysPage(Vm_GetKernPageFrame(virtPage));
	    SET_ALL_PAGE_MAP(((pageOffset + j) * VMMACH_PAGE_SIZE_INT) +
		    VMMACH_DMA_START_ADDR, pte);
	    srcAddr += VMMACH_PAGE_SIZE;
	}
	outPtr->bufAddr = (Address) (VMMACH_DMA_START_ADDR + 
		(pageOffset * VMMACH_PAGE_SIZE_INT) + 
		(((unsigned int) srcAddr) & VMMACH_OFFSET_MASK));
	pageOffset += numPages;
	outPtr->length = inPtr->length;
	inPtr++;
	outPtr++;
    }
    return SUCCESS;
}
#endif /* sun4c */


/*
 ----------------------------------------------------------------------
 *
 * VmMach_DMAFree --
 *
 *	Free a previously allocated set of virtual pages for a routine that
 *	used them for mapping purposes.
 *	
 * Results:
 *	None.
 *
 * Side effects:
 *	The hardware page table is modified.
 *
 *----------------------------------------------------------------------
 */
ENTRY void
VmMach_DMAFree(numBytes, mapAddr)
    int		numBytes;		/* Number of bytes to map in. */
    Address	mapAddr;	/* Kernel virtual address to unmap.*/
{
    Address	beginAddr;
    Address	endAddr;
    int		numPages;
    int		i, j;
 
    MASTER_LOCK(vmMachMutexPtr);
    /* calculate number of pages to free */
						/* beginning of first page */
    beginAddr = (Address) (((unsigned int) mapAddr) & ~VMMACH_OFFSET_MASK_INT);
						/* beginning of last page */
    endAddr = (Address) ((((unsigned int) mapAddr) + numBytes - 1) &
	    ~VMMACH_OFFSET_MASK_INT);
    numPages = (((unsigned int) endAddr) >> VMMACH_PAGE_SHIFT_INT) -
	    (((unsigned int) beginAddr) >> VMMACH_PAGE_SHIFT_INT) + 1;

    i = (((unsigned int) mapAddr) >> VMMACH_PAGE_SHIFT_INT) -
	(((unsigned int) VMMACH_DMA_START_ADDR) >> VMMACH_PAGE_SHIFT_INT);
    for (j = 0; j < numPages; j++) {
	dmaPageBitMap[i + j] = 0;	/* free page */
	if (vmMachHasVACache) {
	    VmMachFlushPage(mapAddr);
	}
	VmMachSetPageMap(mapAddr, (VmMachPTE) 0);
	mapAddr += VMMACH_PAGE_SIZE_INT;
    }
    MASTER_UNLOCK(vmMachMutexPtr);
    return;
}



/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_MapKernelIntoUser --
 *
 *      Map a portion of kernel memory into the user's heap segment.  
 *	It will only map objects on hardware segment boundaries.  This is 
 *	intended to be used to map devices such as video memory.
 *
 *	NOTE: It is assumed that the user process knows what the hell it is
 *	      doing.
 *
 * Results:
 *      Return the virtual address that it chose to map the memory at.
 *
 * Side effects:
 *      The hardware segment table for the user process's segment is modified
 *	to map in the addresses.
 *
 * ----------------------------------------------------------------------------
 */
ReturnStatus
VmMach_MapKernelIntoUser(kernelVirtAddr, numBytes, userVirtAddr,
			 realVirtAddrPtr) 
    unsigned int	kernelVirtAddr;		/* Kernel virtual address
					 	 * to map in. */
    int	numBytes;				/* Number of bytes to map. */
    unsigned int	userVirtAddr; 		/* User virtual address to
					 	 * attempt to start mapping
						 * in at. */
    unsigned int	*realVirtAddrPtr;	/* Where we were able to start
					 	 * mapping at. */
{
    Address             newUserVirtAddr;
    ReturnStatus        status;

    status = VmMach_IntMapKernelIntoUser(kernelVirtAddr, numBytes,
            userVirtAddr, &newUserVirtAddr);

    if (status != SUCCESS) {
        return status;
    }

    return Vm_CopyOut(4, (Address) &newUserVirtAddr, (Address) realVirtAddrPtr);
}


/*
 * ----------------------------------------------------------------------------
 *
 * Vm_IntMapKernelIntoUser --
 *
 *      Map a portion of kernel memory into the user's heap segment.
 *      It will only map objects on hardware segment boundaries.  This is
 *      intended to be used to map devices such as video memory.
 *
 *      This routine can be called from within the kernel since it doesn't
 *      do a Vm_CopyOut of the new user virtual address.
 *
 *      NOTE: It is assumed that the user process knows what the hell it is
 *            doing.
 *
 * Results:
 *      SUCCESS or FAILURE status.
 *      Return the virtual address that it chose to map the memory at in
 *      an out parameter.
 *
 * Side effects:
 *      The hardware segment table for the user process's segment is modified
 *      to map in the addresses.
 *
 * ----------------------------------------------------------------------------
 */
ReturnStatus
VmMach_IntMapKernelIntoUser(kernelVirtAddr, numBytes, userVirtAddr, newAddrPtr)
    unsigned int        kernelVirtAddr;         /* Kernel virtual address
                                                 * to map in. */
    int numBytes;                               /* Number of bytes to map. */
    unsigned int        userVirtAddr;           /* User virtual address to
                                                 * attempt to start mapping
                                                 * in at. */
    Address             *newAddrPtr;            /* New user address. */
{
    int                         numSegs;
    int                         firstPage;
    int                         numPages;
    Proc_ControlBlock           *procPtr;
    register    Vm_Segment      *segPtr;
    int                         hardSegNum;
    int                         i;
    unsigned int                pte;

    procPtr = Proc_GetCurrentProc();
    segPtr = procPtr->vmPtr->segPtrArray[VM_HEAP];

    numSegs = numBytes >> VMMACH_SEG_SHIFT;
    numPages = numSegs * VMMACH_SEG_SIZE / VMMACH_PAGE_SIZE;

    /*
     * Make user virtual address hardware segment aligned (round up) and
     * make sure that there is enough space to map things.
     */
    hardSegNum =
            (unsigned int) (userVirtAddr + VMMACH_SEG_SIZE - 1) >> VMMACH_SEG_SHIFT;
    userVirtAddr = hardSegNum << VMMACH_SEG_SHIFT;
    if (hardSegNum + numSegs > VMMACH_NUM_SEGS_PER_CONTEXT) {
        return(SYS_INVALID_ARG);
    }

    /*
     * Make sure will fit into the kernel's VAS.  Assume that is hardware
     * segment aligned.
     */
    hardSegNum = (unsigned int) (kernelVirtAddr) >> VMMACH_SEG_SHIFT;
    if (hardSegNum + numSegs > VMMACH_NUM_SEGS_PER_CONTEXT) {
        return(SYS_INVALID_ARG);
    }

    /*
     * Invalidate all virtual memory for the heap segment of this process
     * in the given range of virtual addresses that we are to map.  This
     * assures us that there aren't any hardware pages allocated for this
     * segment in this range of addresses.
     */
    firstPage = (unsigned int) (userVirtAddr) >> VMMACH_PAGE_SHIFT;
    (void)Vm_DeleteFromSeg(segPtr, firstPage, firstPage + numPages - 1);

    /*
     * Now go into the kernel's hardware segment table and copy the
     * segment table entries into the heap segments hardware segment table.
     */

    bcopy((Address)GetHardSegPtr(vm_SysSegPtr->machPtr, hardSegNum),
        (Address)GetHardSegPtr(segPtr->machPtr,
                (unsigned int)userVirtAddr >> VMMACH_SEG_SHIFT),
                numSegs * sizeof (VMMACH_SEG_NUM));
    for (i = 0; i < numSegs * VMMACH_NUM_PAGES_PER_SEG_INT; i++) {
        pte = VmMachGetPageMap((Address)(kernelVirtAddr +
                (i * VMMACH_PAGE_SIZE_INT)));
        pte &= ~VMMACH_KR_PROT;
        pte |= VMMACH_URW_PROT;
        VmMachSetPageMap((Address)(kernelVirtAddr + (i*VMMACH_PAGE_SIZE_INT)),
                pte);
    }

    /*
     * Make sure this process never migrates.
     */
    Proc_NeverMigrate(procPtr);

    /*
     * Reinitialize this process's context using the new segment table.
     */
    VmMach_ReinitContext(procPtr);

    *newAddrPtr = (Address) userVirtAddr;
    return SUCCESS;
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_FlushPage --
 *
 *	Flush the page at the given virtual address from all caches.  We
 *	don't have to do anything on the Sun-2 and Sun-3 workstations
 *	that we have.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The given page is flushed from the caches.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
void
VmMach_FlushPage(virtAddrPtr, invalidate)
    Vm_VirtAddr	*virtAddrPtr;
    Boolean	invalidate;	/* Should invalidate the pte after flushing. */
{
    Address	virtAddr;
    int		i;

    /* on sun4, ignore invalidate parameter? */
    virtAddr = (Address) (virtAddrPtr->page << VMMACH_PAGE_SHIFT);
    if (vmMachHasVACache) {
	for (i = 0; i < VMMACH_CLUSTER_SIZE; ++i) {
	    VmMachFlushPage(virtAddr + i * VMMACH_PAGE_SIZE_INT);
	}
    }
    if (invalidate) {
	SET_ALL_PAGE_MAP(virtAddr, (VmMachPTE)0);
    }
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_SetProtForDbg --
 *
 *	Set the protection of the kernel pages for the debugger.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The protection is set for the given range of kernel addresses.
 *
 *----------------------------------------------------------------------
 */
void
VmMach_SetProtForDbg(readWrite, numBytes, addr)
    Boolean	readWrite;	/* TRUE if should make pages writable, FALSE
				 * if should make read-only. */
    int		numBytes;	/* Number of bytes to change protection for. */
    Address	addr;		/* Address to start changing protection at. */
{
    register	Address		virtAddr;
    register	VmMachPTE 	pte;
    register	int		firstPage;
    register	int		lastPage;
    int		oldContext;
    int		i;

    /*
     * This should only be called with kernel text pages so we modify the
     * PTE for the address in all the contexts. Note that we must flush
     * the page from the change before changing the protections to avoid
     * write back errors.
     */
    oldContext = VmMachGetContextReg();
    for (i = 0; i < VMMACH_NUM_CONTEXTS; i++) {
	VmMachSetContextReg(i);
	firstPage = (unsigned)addr >> VMMACH_PAGE_SHIFT_INT;
	lastPage = ((unsigned)addr + numBytes - 1) >> VMMACH_PAGE_SHIFT_INT;
	for (; firstPage <= lastPage; firstPage++) {
	    virtAddr = (Address) (firstPage << VMMACH_PAGE_SHIFT_INT);
	    pte = VmMachGetPageMap(virtAddr);
	    pte &= ~VMMACH_PROTECTION_FIELD;
	    pte |= readWrite ? VMMACH_KRW_PROT : VMMACH_KR_PROT;
	    if (vmMachHasVACache) {
		VmMachFlushPage(virtAddr);
	    }
	    VmMachSetPageMap(virtAddr, pte);
	}
    }
    VmMachSetContextReg(oldContext);
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_Cmd --
 *
 *	Machine dependent vm commands.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
VmMach_Cmd(command, arg)
    int	command;
    int arg;
{
    switch (command & 0xf) {
    case 0:
	if (vmMachHasVACache) {
	    VmMach_FlushCurrentContext();
	}
	break;
    case 1:
	if (vmMachHasVACache) {
	    VmMachFlushSegment((Address)arg);
	}
	break;
    case 2:
	if (vmMachHasVACache) {
	    VmMachFlushPage((Address)arg);
	}
	break;
    case 3:
	VmMachDoNothing((Address)arg);
	break;
    case 4:
	vmMachHasHwFlush = arg;
	break;
    default:
	return GEN_INVALID_ARG;
    }
    return SUCCESS;

}

VmMachDoNothing(arg)
int arg;
{
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_HandleSegMigration --
 *
 *	Do machine-dependent aspects of segment migration.  On the sun4's,
 *	this means flush the segment from the virtually addressed cache.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void
VmMach_HandleSegMigration(segPtr)
    Vm_Segment		*segPtr;	/* Pointer to segment to be migrated. */
{
    Address	virtAddr;

    if (vmMachHasVACache) {
	virtAddr = (Address) (segPtr->offset << VMMACH_PAGE_SHIFT);
	VmMachFlushSegment(virtAddr);
    }

    return;
}


/*
 *----------------------------------------------------------------------
 *
 * VmMach_FlushCode --
 *
 *      Does nothing on this machine.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
void
VmMach_FlushCode(procPtr, virtAddrPtr, virtPage, numBytes)
    Proc_ControlBlock   *procPtr;
    Vm_VirtAddr         *virtAddrPtr;
    unsigned            virtPage;
    int                 numBytes;
{
}


/*
 * Dummy function which will turn out to be the function that the debugger
 * prints out on a backtrace after a trap.  The debugger gets confused
 * because trap stacks originate from assembly language stacks.  I decided
 * to make a dummy procedure because it was to confusing seeing the
 * previous procedure (VmMach_MapKernelIntoUser) on every backtrace.
 */
static void
VmMachTrap()
{
}

#ifndef sun4c

/*----------------------------------------------------------------------
 *
 * Dev32BitBufferInit --
 *
 *	Initialize a range of virtual memory to allocate from out of the
 *	device memory space.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The buffer struct is initialized and the hardware page map is zeroed
 *	out in the range of addresses.
 *
 *----------------------------------------------------------------------
 */
INTERNAL static void
Dev32BitDMABufferInit()
{
    Address		virtAddr;
    unsigned char	pmeg;
    int			oldContext;
    Address	baseAddr;
    Address	endAddr;

    if ((VMMACH_32BIT_DMA_SIZE & (VMMACH_CACHE_SIZE - 1)) != 0) {
	panic(
"Dev32BitDMABufferInit: 32-bit DMA area must be a multiple of cache size.\n");
    }

    VmMachSetup32BitDVMA(); 
    /*
     * Round base up to next page boundary and end down to page boundary.
     */
    baseAddr = (Address)VMMACH_32BIT_DMA_START_ADDR;
    endAddr = (Address)(VMMACH_32BIT_DMA_START_ADDR + VMMACH_32BIT_DMA_SIZE);

    /* 
     * Set up the hardware pages tables in the range of addresses given.
     */
    oldContext = VmMachGetContextReg();
    VmMachSetContextReg(0);
    for (virtAddr = baseAddr; virtAddr < endAddr; ) {
	if (VmMachGetSegMap(virtAddr) != VMMACH_INV_PMEG) {
	    printf("Dev32BitDMABufferInit: DMA space already valid at 0x%x\n",
		   (unsigned int) virtAddr);
	}
	/* 
	 * Need to allocate a PMEG.
	 */
	pmeg = PMEGGet(vm_SysSegPtr, 
		       (int) ((unsigned)virtAddr >> VMMACH_SEG_SHIFT),
		       PMEG_DONT_ALLOC);
	if (pmeg == VMMACH_INV_PMEG) {
	    panic("Dev32BitDMABufferInit: unable to get a pmeg.\n");
	}
        VmMachSetSegMap(virtAddr, (int)pmeg);
	virtAddr += VMMACH_SEG_SIZE;
    }
    VmMachSetContextReg(oldContext);
}


static	Boolean	userdmaPageBitMap[VMMACH_32BIT_DMA_SIZE / VMMACH_PAGE_SIZE_INT];


/*
 ----------------------------------------------------------------------
 *
 * VmMach_32BitDMAAlloc --
 *
 *	Allocate a set of virtual pages to a routine for mapping purposes.
 *	
 * Results:
 *	Pointer into kernel virtual address space of where to access the
 *	memory, or NIL if the request couldn't be satisfied.
 *
 * Side effects:
 *	The hardware page table is modified.
 *
 *----------------------------------------------------------------------
 */
ENTRY Address
VmMach_32BitDMAAlloc(numBytes, srcAddr)
    int		numBytes;		/* Number of bytes to map in. */
    Address	srcAddr;	/* Kernel virtual address to start mapping in.*/
{
    Address	beginAddr;
    Address	endAddr;
    int		numPages;
    int		i, j;
    VmMachPTE	pte;
    Boolean	foundIt = FALSE;
    static initialized = FALSE;
    unsigned	oldContext;
    int		align;
 
    MASTER_LOCK(vmMachMutexPtr);
    if (!initialized) {
	initialized = TRUE;
	Dev32BitDMABufferInit();
    }

    /* calculate number of pages needed */
						/* beginning of first page */
    beginAddr = (Address) (((unsigned int)(srcAddr)) & ~VMMACH_OFFSET_MASK_INT);
						/* beginning of last page */
    endAddr = (Address) ((((unsigned int) srcAddr) + numBytes - 1) &
	    ~VMMACH_OFFSET_MASK_INT);
    numPages = (((unsigned int) endAddr) >> VMMACH_PAGE_SHIFT_INT) -
	    (((unsigned int) beginAddr) >> VMMACH_PAGE_SHIFT_INT) + 1;

    /* set first addr to first entry that is also cache aligned */
    align = (unsigned int) beginAddr & (VMMACH_CACHE_SIZE - 1);
    align -= (unsigned int) VMMACH_32BIT_DMA_START_ADDR &
	    (VMMACH_CACHE_SIZE - 1);
    if ((int) align < 0) {
	align += VMMACH_CACHE_SIZE;
    }
    /* see if request can be satisfied, incrementing by cache size in loop */
    for (i = align / VMMACH_PAGE_SIZE_INT;
	    i < (VMMACH_32BIT_DMA_SIZE / VMMACH_PAGE_SIZE_INT);
	    i += (VMMACH_CACHE_SIZE / VMMACH_PAGE_SIZE_INT)) {
	if (userdmaPageBitMap[i] == 1) {
	    continue;
	}
	for (j = 1; (j < numPages) &&
		((i + j) < (VMMACH_32BIT_DMA_SIZE / VMMACH_PAGE_SIZE_INT));
		j++) {
	    if (userdmaPageBitMap[i + j] == 1) {
		break;
	    }
	}
	if ((j == numPages) &&
		((i + j) < (VMMACH_32BIT_DMA_SIZE / VMMACH_PAGE_SIZE_INT))) {
	    foundIt = TRUE;
	    break;
	}
    }

    if (!foundIt) {
	MASTER_UNLOCK(vmMachMutexPtr);
	panic(
    "VmMach_32BitDMAAlloc: unable to satisfy request for %d bytes at 0x%x\n",
		numBytes, srcAddr);
#ifdef NOTDEF
	return (Address) NIL;
#endif NOTDEF
    }
    oldContext = VmMachGetContextReg();
    VmMachSetContextReg(0);
    for (j = 0; j < numPages; j++) {
	userdmaPageBitMap[i + j] = 1;	/* allocate page */
	pte = VmMachGetPageMap(srcAddr);
	pte = (pte & ~VMMACH_PROTECTION_FIELD) | VMMACH_RESIDENT_BIT | 
		    VMMACH_URW_PROT;

	SET_ALL_PAGE_MAP(((i + j) * VMMACH_PAGE_SIZE_INT) +
		VMMACH_32BIT_DMA_START_ADDR, pte);
	srcAddr += VMMACH_PAGE_SIZE;
    }
    VmMachSetContextReg((int)oldContext);
    beginAddr = (Address) (VMMACH_32BIT_DMA_START_ADDR +
	    (i * VMMACH_PAGE_SIZE_INT) +
	    (((unsigned int) srcAddr) & VMMACH_OFFSET_MASK));

    /* set high VME addr bit */
    beginAddr = (Address)((unsigned) beginAddr | VMMACH_VME_ADDR_BIT);
    MASTER_UNLOCK(vmMachMutexPtr);
    return (Address) beginAddr;
}


/*
 ----------------------------------------------------------------------
 *
 * VmMach_32BitDMAFree --
 *
 *	Free a previously allocated set of virtual pages for a routine that
 *	used them for mapping purposes.
 *	
 * Results:
 *	None.
 *
 * Side effects:
 *	The hardware page table is modified.
 *
 *----------------------------------------------------------------------
 */
ENTRY void
VmMach_32BitDMAFree(numBytes, mapAddr)
    int		numBytes;		/* Number of bytes to map in. */
    Address	mapAddr;	/* Kernel virtual address to unmap.*/
{
    Address	beginAddr;
    Address	endAddr;
    int		numPages;
    int		i, j;
    int         oldContext;

    MASTER_LOCK(vmMachMutexPtr);
    /* calculate number of pages to free */
    /* Clear the VME high bit from the address */
    mapAddr = (Address) ((unsigned)mapAddr & ~VMMACH_VME_ADDR_BIT);
						/* beginning of first page */
    beginAddr = (Address) (((unsigned int) mapAddr) & ~VMMACH_OFFSET_MASK_INT);
						/* beginning of last page */
    endAddr = (Address) ((((unsigned int) mapAddr) + numBytes - 1) &
	    ~VMMACH_OFFSET_MASK_INT);
    numPages = (((unsigned int) endAddr) >> VMMACH_PAGE_SHIFT_INT) -
	    (((unsigned int) beginAddr) >> VMMACH_PAGE_SHIFT_INT) + 1;

    i = (((unsigned int) mapAddr) >> VMMACH_PAGE_SHIFT_INT) -
	(((unsigned int) VMMACH_32BIT_DMA_START_ADDR) >> VMMACH_PAGE_SHIFT_INT);
    oldContext = VmMachGetContextReg();
    VmMachSetContextReg(0);
    for (j = 0; j < numPages; j++) {
	userdmaPageBitMap[i + j] = 0;	/* free page */
	if (vmMachHasVACache) {
	    VmMachFlushPage(mapAddr);
	}
	SET_ALL_PAGE_MAP(mapAddr, (VmMachPTE) 0);
	mapAddr = (Address)((unsigned int) mapAddr + VMMACH_PAGE_SIZE_INT);
    }
    VmMachSetContextReg(oldContext);
    MASTER_UNLOCK(vmMachMutexPtr);
    return;
}

#endif /* not sun4c */


#define CHECK(x) (((x)<0||(x)>=VMMACH_SHARED_NUM_BLOCKS)?\
	(panic("Alloc out of bounds"),0):0)
#define ALLOC(x,s)	(CHECK(x),sharedData->allocVector[(x)]=s)
#define FREE(x)		(CHECK(x),sharedData->allocVector[(x)]=0)
#define SIZE(x)		(CHECK(x),sharedData->allocVector[(x)])
#define ISFREE(x)	(CHECK(x),sharedData->allocVector[(x)]==0)



/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_Alloc --
 *
 *      Allocates a region of shared memory;
 *
 * Results:
 *      SUCCESS if the region can be allocated.
 *	The starting address is returned in addr.
 *
 * Side effects:
 *      The allocation vector is updated.
 *
 * ----------------------------------------------------------------------------
 */
static ReturnStatus
VmMach_Alloc(sharedData, regionSize, addr)
    VmMach_SharedData	*sharedData;	/* Pointer to shared memory info.  */
    int			regionSize;	/* Size of region to allocate. */
    Address		*addr;		/* Address of region. */
{
    int numBlocks = (regionSize+VMMACH_SHARED_BLOCK_SIZE-1) /
	    VMMACH_SHARED_BLOCK_SIZE;
    int i, blockCount, firstBlock;

    if (sharedData->allocVector == (int *)NULL || sharedData->allocVector ==
	    (int *)NIL) {
	dprintf("VmMach_Alloc: allocVector uninitialized!\n");
    }

    /*
     * Loop through the alloc vector until we find numBlocks free blocks
     * consecutively.
     */
    blockCount = 0;
    for (i=sharedData->allocFirstFree;
	    i<=VMMACH_SHARED_NUM_BLOCKS-1 && blockCount<numBlocks;i++) {
	if (ISFREE(i)) {
	    blockCount++;
	} else {
	    blockCount = 0;
	    if (i==sharedData->allocFirstFree) {
		sharedData->allocFirstFree++;
	    }
	}
    }
    if (blockCount < numBlocks) {
	dprintf("VmMach_Alloc: got %d blocks of %d of %d total\n",
		blockCount,numBlocks,VMMACH_SHARED_NUM_BLOCKS);
	return VM_NO_SEGMENTS;
    }
    firstBlock = i-blockCount;
    if (firstBlock == sharedData->allocFirstFree) {
	sharedData->allocFirstFree += blockCount;
    }
    *addr = (Address)(firstBlock*VMMACH_SHARED_BLOCK_SIZE +
	    VMMACH_SHARED_START_ADDR);
    for (i = firstBlock; i<firstBlock+numBlocks; i++) {
	ALLOC(i,numBlocks);
    }
    dprintf("VmMach_Alloc: got %d blocks at %d (%x)\n",
	    numBlocks,firstBlock,*addr);
    return SUCCESS;
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_Unalloc --
 *
 *      Frees a region of shared address space.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The allocation vector is updated.
 *
 * ----------------------------------------------------------------------------
 */

static void
VmMach_Unalloc(sharedData, addr)
    VmMach_SharedData	*sharedData;	/* Pointer to shared memory info. */
    Address	addr;		/* Address of region. */
{
    int firstBlock = ((int)addr-VMMACH_SHARED_START_ADDR) /
	    VMMACH_SHARED_BLOCK_SIZE;
    int numBlocks;
    int i;

    if (firstBlock<0 || firstBlock>=VMMACH_SHARED_NUM_BLOCKS) {
	if (debugVmStubs) {
	    printf("VmMach_Unalloc: addr %x out of range\n", addr);
	}
	return;
    }

    numBlocks = SIZE(firstBlock);

    dprintf("VmMach_Unalloc: freeing %d blocks at %x\n",numBlocks,addr);
    if (firstBlock < sharedData->allocFirstFree) {
	sharedData->allocFirstFree = firstBlock;
    }
    for (i=0;i<numBlocks;i++) {
	if (ISFREE(i+firstBlock)) {
	    if (debugVmStubs) {
		printf("Freeing free shared address %d %d %x\n",i,i+firstBlock,
			(int)addr);
	    }
	    return;
	}
	FREE(i+firstBlock);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_SharedStartAddr --
 *
 *      Determine the starting address for a shared segment.
 *
 * Results:
 *      Returns the proper start address for the segment.
 *
 * Side effects:
 *      Allocates part of the shared address space.
 *
 * ----------------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
VmMach_SharedStartAddr(procPtr,size,reqAddr, fixed)
    Proc_ControlBlock	*procPtr;
    int             size;           /* Length of shared segment. */
    Address         *reqAddr;        /* Requested start address. */
    int		    fixed;	    /* 1 if fixed address requested. */
{
    int numBlocks = (size+VMMACH_SHARED_BLOCK_SIZE-1) /
	    VMMACH_SHARED_BLOCK_SIZE;
    int firstBlock = (((int)*reqAddr)-VMMACH_SHARED_START_ADDR) /
	    VMMACH_SHARED_BLOCK_SIZE;
    int i;
    VmMach_SharedData	*sharedData = &procPtr->vmPtr->machPtr->sharedData;

    if (fixed==0) {
	return VmMach_Alloc(sharedData, size, reqAddr);
    } else {
	for (i = firstBlock; i<firstBlock+numBlocks; i++) {
	    if (i>0) {
		ALLOC(i,numBlocks);
	    }
	}
	return SUCCESS;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_SharedProcStart --
 *
 *      Perform machine dependent initialization of shared memory
 *	for this process.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The storage allocation structures are initialized.
 *
 * ----------------------------------------------------------------------------
 */
void
VmMach_SharedProcStart(procPtr)
    Proc_ControlBlock	*procPtr;
{
    VmMach_SharedData	*sharedData = &procPtr->vmPtr->machPtr->sharedData;
    dprintf("VmMach_SharedProcStart: initializing proc's allocVector\n");
    if (sharedData->allocVector != (int *)NIL) {
	panic("VmMach_SharedProcStart: allocVector not NIL\n");
    }
    sharedData->allocVector =
	    (int *)malloc(VMMACH_SHARED_NUM_BLOCKS*sizeof(int));
    if (debugVmStubs) {
	printf("Initializing allocVector for %x to %x\n", procPtr->processID,
		sharedData->allocVector);
    }
    sharedData->allocFirstFree = 0;
    bzero((Address) sharedData->allocVector, VMMACH_SHARED_NUM_BLOCKS*
	    sizeof(int));
    procPtr->vmPtr->sharedStart = (Address) 0x00000000;
    procPtr->vmPtr->sharedEnd = (Address) 0xffff0000;
}

/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_SharedSegFinish --
 *
 *      Perform machine dependent cleanup of shared memory
 *	for this segment.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The storage allocation structures are freed.
 *
 * ----------------------------------------------------------------------------
 */
void
VmMach_SharedSegFinish(procPtr,addr)
    Proc_ControlBlock	*procPtr;
    Address		addr;
{
    VmMach_Unalloc(&procPtr->vmPtr->machPtr->sharedData,addr);
}

/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_SharedProcFinish --
 *
 *      Perform machine dependent cleanup of shared memory
 *	for this process.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The storage allocation structures are freed.
 *
 * ----------------------------------------------------------------------------
 */
void
VmMach_SharedProcFinish(procPtr)
    Proc_ControlBlock	*procPtr;
{
    dprintf("VmMach_SharedProcFinish: freeing process's allocVector\n");
    if (debugVmStubs) {
	printf("VmMach_SharedProcFinish: freeing process's allocVector %x\n",
		procPtr->vmPtr->machPtr->sharedData.allocVector);
    }
    free((Address)procPtr->vmPtr->machPtr->sharedData.allocVector);
    procPtr->vmPtr->machPtr->sharedData.allocVector = (int *)NIL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_CopySharedMem --
 *
 *      Copies machine-dependent shared memory data structures to handle
 *      a fork.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The new process gets a copy of the shared memory structures.
 *
 * ----------------------------------------------------------------------------
 */
void
VmMach_CopySharedMem(parentProcPtr, childProcPtr)
    Proc_ControlBlock   *parentProcPtr; /* Parent process. */
    Proc_ControlBlock   *childProcPtr;  /* Child process. */
{
    VmMach_SharedData   *childSharedData =
            &childProcPtr->vmPtr->machPtr->sharedData;
    VmMach_SharedData   *parentSharedData =
            &parentProcPtr->vmPtr->machPtr->sharedData;

    VmMach_SharedProcStart(childProcPtr);

    bcopy((Address)parentSharedData->allocVector,
	    (Address)childSharedData->allocVector,
            VMMACH_SHARED_NUM_BLOCKS*sizeof(int));
    childSharedData->allocFirstFree = parentSharedData->allocFirstFree;
}

/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_LockCachePage --
 *
 *      Perform machine dependent locking of a kernel resident file cache
 *	page.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *
 * ----------------------------------------------------------------------------
 */
void
VmMach_LockCachePage(kernelAddress)
    Address	kernelAddress;	/* Address on page to lock. */
{
    Vm_VirtAddr	virtAddr;
    register  VMMACH_SEG_NUM	*segTablePtr, pmeg;
    register  int		hardSeg;
    Vm_PTE    *ptePtr;
    VmMachPTE		hardPTE;
    /*
     * Ignore pages not in cache pmeg range.
     */
    if (!IN_FILE_CACHE_SEG(kernelAddress)) {
	return;
    }

    MASTER_LOCK(vmMachMutexPtr);

    pmeg = VmMachGetSegMap(kernelAddress);
    if (pmeg == VMMACH_INV_PMEG) {
	int	oldContext, i;
	unsigned int a;
	/*
	 *  If not a valid PMEG install a new pmeg and load its mapping. 
	 */
	virtAddr.segPtr = vm_SysSegPtr;
	virtAddr.page = ((unsigned int) kernelAddress) >> VMMACH_PAGE_SHIFT;
	virtAddr.offset = 0;
	virtAddr.flags = 0;
	virtAddr.sharedPtr = (Vm_SegProcList *) NIL;
    
	hardSeg = PageToOffSeg(virtAddr.page, (&virtAddr));
	segTablePtr = (VMMACH_SEG_NUM *) 
			    GetHardSegPtr(vm_SysSegPtr->machPtr, hardSeg);
	if (*segTablePtr != VMMACH_INV_PMEG) {
	    panic("VmMach_LockCachePage: Bad segTable entry.\n");
	}
	*segTablePtr = pmeg = PMEGGet(vm_SysSegPtr, hardSeg, 0);
	/*
	 * Have to propagate the PMEG to all contexts.
	 */
	oldContext = VmMachGetContextReg();
	for (i = 0; i < VMMACH_NUM_CONTEXTS; i++) {
	    VmMachSetContextReg(i);
	    VmMachSetSegMap(kernelAddress, pmeg);
	}
	VmMachSetContextReg(oldContext);
	/*
	 * Reload the entire PMEG.
	 */
	a = (hardSeg << VMMACH_SEG_SHIFT);
	for (i = 0; i < VMMACH_NUM_PAGES_PER_SEG; i++ ) { 
	    ptePtr = VmGetPTEPtr(vm_SysSegPtr, (a >> VMMACH_PAGE_SHIFT));
	    if ((*ptePtr & VM_PHYS_RES_BIT)) {
		hardPTE = VMMACH_RESIDENT_BIT | VMMACH_KRW_PROT | 
				VirtToPhysPage(Vm_GetPageFrame(*ptePtr));
		SET_ALL_PAGE_MAP(a, hardPTE);
		if (pmeg==VMMACH_INV_PMEG) {
		    panic("Invalid pmeg\n");
		}
		pmegArray[pmeg].pageCount++;
	     }
	     a += VMMACH_PAGE_SIZE;
	}
    }
    pmegArray[pmeg].lockCount++;

    MASTER_UNLOCK(vmMachMutexPtr);
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_UnlockCachePage --
 *
 *      Perform machine dependent unlocking of a kernel resident page.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *
 * ----------------------------------------------------------------------------
 */
void
VmMach_UnlockCachePage(kernelAddress)
    Address	kernelAddress;	/* Address on page to unlock. */
{
    register  VMMACH_SEG_NUM	pmeg;

    if (!IN_FILE_CACHE_SEG(kernelAddress)) {
	return;
    }

    MASTER_LOCK(vmMachMutexPtr);

    pmeg = VmMachGetSegMap(kernelAddress);

    pmegArray[pmeg].lockCount--;
    if (pmegArray[pmeg].lockCount < 0) {
	panic("VmMach_UnlockCachePage lockCount < 0\n");
    }

    MASTER_UNLOCK(vmMachMutexPtr);
    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * VmMach_FlushCurrentContext --
 *
 *	Flush the current context from the cache.
 *
 *	void VmMach_FlushCurrentContext()
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All data cached from the current context is flushed from the cache.
 *
 * ----------------------------------------------------------------------------
 */
void
VmMach_FlushCurrentContext()
{
    if (vmMachHasVACache) {
	VmMachFlushCurrentContext();
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * Vm_TouchPages --
 *
 *	Touch the range of pages.
 *
 *	ReturnStatus
 *	Vm_TouchPages(firstPage, numPages)
 *	    int	firstPage;	First page to touch.
 *	    int	numPages;	Number of pages to touch.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------
 */
ReturnStatus
Vm_TouchPages(firstPage, numPages)
    int firstPage, numPages;
{
    return VmMachTouchPages(firstPage * VMMACH_CLUSTER_SIZE,
	numPages * VMMACH_CLUSTER_SIZE);
}
