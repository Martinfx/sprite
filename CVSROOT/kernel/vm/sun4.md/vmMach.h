/*
 * vmMach.h
 *
 *     	Machine dependent virtual memory data structures and procedure 
 *	headers.
 *
 * Copyright (C) 1985 Regents of the University of California
 * All rights reserved.
 *
 *
 * $Header$ SPRITE (Berkeley)
 */

#ifndef _VMMACH
#define _VMMACH

#ifdef KERNEL
#include "vm.h"
#include "vmSunConst.h"
#include "proc.h"
#include "net.h"
#else
#include <kernel/vm.h>
#include <kernel/vmSunConst.h>
#include <kernel/proc.h>
#include <kernel/net.h>
#endif

/*
 * Since the index into the segment table is passed to various
 * machine-dependent routines, its size must depend on the hardware size
 * or else be the largest size used, which would waste space.  So far, this
 * has only been generalized for the various types of sun4s.
 */
#ifdef sun4
#ifdef sun4c
typedef	unsigned short	VMMACH_SEG_NUM;	/* should be unsigned char, but fails */
#else
typedef	unsigned short	VMMACH_SEG_NUM;
#endif	/* sun4c */
#endif	/* sun4 */

/*
 * PMEG segment info list entry.
 */
#ifdef sun4
struct VmMach_PMEGseg {
    struct VmMach_PMEGseg      *nextLink;      /* Linked list ptr. */
    struct Vm_Segment   *segPtr;        /* Software segment. */
    int                 hardSegNum;     /* Hardware segment number. */
    int                 inuse;
};
#endif

/*
 * Machine dependent data for each software segment.  The data for each 
 * segment is a hardware segment table that contains one entry for each
 * hardware segment in the software segment's virtual address space.
 * A hardware context is made up of several software segments.  The offset
 * field in this struct is the offset into the context where this software
 * segment begins.  The offset is in terms of hardware segments.
 */
typedef struct VmMach_SegData {
#ifdef sun4
    VMMACH_SEG_NUM	*segTablePtr;
#else
    unsigned char 	*segTablePtr;
#endif /* sun4 */
    int			offset;
    int			numSegs;
#ifdef sun4
    struct VmMach_PMEGseg	pmegInfo;
#endif
} VmMach_SegData;

/*
 * Machine dependent shared memory data
 */
typedef struct VmMach_SharedData {
    int        *allocVector;           /* Allocated block vector. */
    int         allocFirstFree;         /* First free block. */
} VmMach_SharedData;

/*
 * Machine dependent data for each process.
 */
typedef struct VmMach_ProcData {
    struct VmMach_Context *contextPtr;	/* The context for the process. */
    struct Vm_Segment	*mapSegPtr;	/* Pointer to segment which is mapped
					 * into this processes address
					 * space. */
    unsigned int	mapHardSeg;	/* Address in the mapped seg where 
					 * the mapping begins. */
    struct Vm_SegProcList *sharedPtr;	/* Shared memory data */
    VmMach_SharedData   sharedData;     /* Data for shared memory. */
} VmMach_ProcData;

typedef unsigned int	VmMachPTE;

/*
 * Exported machine dependent functions.
 */

/*
 * Manipulating protection.
 */
extern void VmMach_SetProtForDbg _ARGS_((Boolean readWrite, int numBytes,
	Address addr));
/*
 * Routines to copy data to/from user space.
 */
extern int  VmMachCopyEnd _ARGS_((void));
extern ReturnStatus VmMach_IntMapKernelIntoUser _ARGS_((unsigned int
	kernelVirtAddr, int numBytes, unsigned int userVirtAddr,
	Address *newAddrPtr));


/*
 * Routines for the INTEL device driver.
 */
extern void VmMach_MapIntelPage _ARGS_((Address virtAddr));
extern void VmMach_UnmapIntelPage _ARGS_((Address virtAddr));

/*
 * Routines for DMA
 */
extern Address VmMach_32BitDMAAlloc _ARGS_((int numBytes, Address srcAddr));
extern void VmMach_32BitDMAFree _ARGS_((int numBytes, Address mapAddr));
extern ReturnStatus VmMach_DMAAllocContiguous _ARGS_((
	Net_ScatterGather *inScatGathPtr, int scatGathLength,
	Net_ScatterGather *outScatGathPtr));

/*
 * Device mapping.
 */
extern Address VmMach_MapInDevice _ARGS_((Address devPhysAddr, int type));
extern Address VmMach_MapInBigDevice _ARGS_((Address devPhysAddr, int numBytes,
	int type));
/*
 * Network mapping routines.
 */
extern Address VmMach_NetMemAlloc _ARGS_((int numBytes));
extern void VmMach_NetMapPacket _ARGS_((register Net_ScatterGather
	*inScatGathPtr, register int scatGathLength,
	register Net_ScatterGather *outScatGathPtr));
/*
 * Context routines.
 */
extern int VmMach_GetContext _ARGS_((Proc_ControlBlock *procPtr));
extern void VmMach_FlushCurrentContext _ARGS_((void));

/*
 * File Cache routines.
 */
extern void VmMach_LockCachePage _ARGS_((Address kernelAddress));
extern void VmMach_UnlockCachePage _ARGS_((Address kernelAddress));

#endif /* _VMMACH */
