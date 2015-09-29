/* 
 * traceLog.c --
 *
 *	This files implements a generalized tracing facility.  
 *	TraceLog_Buffer	contains information about the number and size of
 *	the elements in a circular buffer that is dynamically allocated by
 *	TraceLog_Init.  Calls to TraceLog_Insert add a time-stamped traceLog
 *	record to the buffer.  A buffer of traceLog records can be dumped to
 *	user space via calls to TraceLog_Dump.
 *
 *	The main difference between these routines and the routines in
 *	traceLog.c are that these routines are for continuous dumping of
 *	data to user level.  They are set up to buffer and block data
 *	for dumping in large chunks to user level.  Also, they handle
 *	variable-sized records.
 *
 * 	These routines are for the SOSP91 paper.
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
static char rcsid[] = "$Header: /sprite/src/kernel/utils/RCS/traceLog.c,v 1.9 91/07/24 22:18:19 kupfer Exp $ SPRITE (Berkeley)";
#endif not lint

#include "sprite.h"
#include "traceLog.h"
#include "bstring.h"
#include "timer.h"
#include "stdlib.h"
#include "sysStats.h"
#include "sys.h"
#include "sync.h"
#include "vm.h"
#include "mach.h"
#include <stdio.h>
#include <main.h>
#include <string.h>
#include <status.h>

#define CHECKCONSIST

static void TraceLog_FlushBuffer _ARGS_((TraceLog_Header *tracePtr));

static Sync_Lock traceLogLock = Sync_LockInitStatic("Utils:traceLog_Mutex");
#define LOCKPTR &traceLogLock

Sync_Condition bufferFullCondition; /* Condition that we have a full block
				       to give the user. */

extern int vmShmDebug;	/* Ken's handy debug flag. */

/*
 * Global tracing inhibit flag so it is easy to turn off all system tracing.
 */
Boolean traceLog_Disable = TRUE;

/*
 * Invariants for the trace pointers.
 * If tracePtr->blocked:
 *      firstNewBuffer points to the first buffer with new data.
 *	currentBuffer points to the buffer we're about to fill.
 *	(i.e. currentBuffer = firstNewBuffer when we first block)
 * If !tracePtr->blocked:
 *	firstNewBuffer points to the first buffer with new (undumped) data.
 *	currentBuffer points to the buffer we're currently loading.
 *      If there are no full buffers, firstNewBuffer=currentBuffer
 */


/*
 *----------------------------------------------------------------------
 *
 * TraceLog_Init --
 *
 *	Allocate and initialize a circular traceLog buffer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is allocated for the individual buffer records.  Note 
 *	that the TraceLog_Header is assumed to be allocated
 *	(statically or dynamically) by the calling routine, and its values
 *	are initialized in this routine.
 *
 *----------------------------------------------------------------------
 */

void
TraceLog_Init(tracePtr, numBuffers, size, flags, version)
    register TraceLog_Header *tracePtr;	/* pointer to tracing info */
    int numBuffers;			/* number of buffers to allocate */
    int size;				/* size of each buffer area */
    int flags;				/* TRACELOG_NO_TIMES */
    int version;
{
    int i;
    Time time;
    Timer_Ticks ticks;

    LOCK_MONITOR;
    tracePtr->numBuffers = numBuffers;
    tracePtr->flags = flags;
    tracePtr->dataSize = size;
    tracePtr->firstNewBuffer = 0;
    tracePtr->currentBuffer = 0;
    tracePtr->currentOffset = 0;
    tracePtr->blocked = 0;
    tracePtr->lostRecords = 0;
    tracePtr->totalNumRecords = 0;
    tracePtr->totalLostRecords = 0;
    tracePtr->hdr.machineID = rpc_SpriteID;
    strncpy(tracePtr->hdr.machineType, mach_MachineType, SYS_TRACELOG_TYPELEN);
    tracePtr->hdr.kernel[0] = version;
    tracePtr->hdr.kernel[1] = '/';
    strncpy(tracePtr->hdr.kernel+2, SpriteVersion(), SYS_TRACELOG_KERNELLEN-2);
    Timer_GetRealTimeOfDay(&time, (int *)NIL, (Boolean *)NIL);
    Timer_GetCurrentTicks(&ticks);
    Timer_SubtractTicks(*(Time *)&time, *(Time *)&ticks,
	    (Time *)tracePtr->hdr.bootTime);

    tracePtr->buffers =
	    (TraceLog_Buffer *) malloc(numBuffers *
		    sizeof(TraceLog_Buffer));
    for (i=0;i<numBuffers;i++) {
	tracePtr->buffers[i].size = 0;
	tracePtr->buffers[i].numRecords = 0;
	tracePtr->buffers[i].lostRecords = 0;
	tracePtr->buffers[i].data = (Address) malloc(size);
#ifdef CHECKCONSIST
	tracePtr->buffers[i].mode = TRACELOG_UNUSED;
#endif
    }
#ifdef CHECKCONSIST
	    tracePtr->buffers[0].mode = TRACELOG_INUSE;
#endif
    if (2*sizeof(int) < sizeof(Timer_Ticks)) {
	printf("Warning: Timer_Ticks structure is too big for trace\n");
    }
    UNLOCK_MONITOR;

}


/*
 *----------------------------------------------------------------------
 *
 * TraceLog_Insert --
 *
 *	Save a time stamp and any client-specific data in a circular buffer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Record the information in a traceLog record and advance the
 *	circular buffer pointer.
 *
 *----------------------------------------------------------------------
 */
#define INC_BUFNUM(x) ((x)=((x)+1>=tracePtr->numBuffers?0:(x)+1))
#define REC_HEADER_SIZE (sizeof(Sys_TracelogRecord)-sizeof(ClientData))
void
TraceLog_Insert(tracePtr, dataPtr, origSize, flags)
    TraceLog_Header *tracePtr;	/* The trace log info structure. */
    Address dataPtr;		/* Pointer to the record. */
    int	origSize;		/* Length in bytes of the record. */
    int flags;
{
    Address traceDataAddr;
    Sys_TracelogRecord traceRec;
    int headerSize;
    int size;

    if (traceLog_Disable || tracePtr == (TraceLog_Header *)NIL ||
	    tracePtr->flags & TRACELOG_INHIBIT) {
	return;
    }

    size = (origSize+3)&~3;	/* Make size word aligned. */

    LOCK_MONITOR;

    tracePtr->totalNumRecords++;

    if (size+REC_HEADER_SIZE > tracePtr->dataSize) {
	printf("Record too big: %d\n", size);
	tracePtr->lostRecords++;
	tracePtr->totalLostRecords++;
	UNLOCK_MONITOR;
	return;
    }

    /*
     * Check if we've filled a buffer.
     */
    if (!tracePtr->blocked && (tracePtr->currentOffset+size+REC_HEADER_SIZE >
	    tracePtr->dataSize)) {
	TraceLog_FlushBuffer(tracePtr);
    }

    if (vmShmDebug) {
	printf("buf=%x, num=%d, bytes: %d, flags: %x\n", tracePtr->buffers,
	    tracePtr->currentBuffer,size, flags);
    }

    /*
     * Check if we've run out of buffers.  If so, skip this record
     * and keep track of how many we've lost.
     */
    if (tracePtr->blocked) {
	if (vmShmDebug) {
	    printf("Trace buffer overflow!\n");
	}
	tracePtr->lostRecords++;
	tracePtr->totalLostRecords++;
	UNLOCK_MONITOR;
	return;
    }

#ifdef CHECKCONSIST
    if (tracePtr->buffers[tracePtr->currentBuffer].mode != TRACELOG_INUSE) {
	printf("Warning: buffer# %d has mode %d, not INUSE\n",
		tracePtr->currentBuffer, tracePtr->buffers[tracePtr->
		currentBuffer].mode);
    }
    tracePtr->buffers[tracePtr->currentBuffer].mode = TRACELOG_INUSE;
#endif
    traceDataAddr = tracePtr->buffers[tracePtr->currentBuffer].data +
	    tracePtr->currentOffset;

    if ( !(tracePtr->flags & TRACELOG_NO_TIMES)) {
	Timer_GetCurrentTicks((Timer_Ticks *)traceRec.time);
	headerSize = REC_HEADER_SIZE;
    } else {
	headerSize = sizeof(int);
    }
    traceRec.recordLen = (size+headerSize)|(flags<<16);
    bcopy((Address)&traceRec, traceDataAddr, headerSize);
    bcopy(dataPtr, traceDataAddr+headerSize, origSize);
    tracePtr->buffers[tracePtr->currentBuffer].numRecords++;
    tracePtr->currentOffset += size+headerSize;
    if (tracePtr->flags & TRACELOG_NO_BUF) {
	TraceLog_FlushBuffer(tracePtr);
    }
    UNLOCK_MONITOR;
}

/*
 *----------------------------------------------------------------------
 *
 * TraceLog_FlushBuffer --
 *
 *	We're done with the record, so we want to send it off.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The information is filled in for the currentBuffer.
 *	The currentBuffer is incremented to the next buffer.
 *	The data is made available for the consumer.
 *----------------------------------------------------------------------
 */
static void
TraceLog_FlushBuffer(tracePtr)
    register TraceLog_Header *tracePtr;
{
    tracePtr->buffers[tracePtr->currentBuffer].size =
	    tracePtr->currentOffset;
#ifdef CHECKCONSIST
    if (tracePtr->buffers[tracePtr->currentBuffer].mode != TRACELOG_INUSE) {
	printf("Warning: buffer# %d has mode %d, not INUSE\n",
		tracePtr->currentBuffer, tracePtr->buffers[tracePtr->
		currentBuffer].mode);
    }
    tracePtr->buffers[tracePtr->currentBuffer].mode = TRACELOG_DONE;
#endif
    INC_BUFNUM(tracePtr->currentBuffer);
    tracePtr->currentOffset = 0;
    Sync_Broadcast(&bufferFullCondition);
    if (tracePtr->currentBuffer == tracePtr->firstNewBuffer) {
	tracePtr->blocked = 1;
    }
#ifdef CHECKCONSIST
    else {
	if (tracePtr->buffers[tracePtr->currentBuffer].mode !=
		TRACELOG_UNUSED) {
	    printf("Warning: buffer# %d has mode %d, not UNUSED\n",
		    tracePtr->currentBuffer, tracePtr->buffers[tracePtr->
		    currentBuffer].mode);
	}
	tracePtr->buffers[tracePtr->currentBuffer].mode = TRACELOG_INUSE;
    }
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * TraceLog_Dump --
 *
 *	Dump traceLog records into the user's address space.  Data is copied
 *	in the following order:
 *
 *	(1) The number of records being copied is copied.
 *	(2) numRecs TraceLog_Records are copied.
 *	(3) If traceLogData is non-NIL, numRecs records of traceData are copied.
 *
 * Results:
 *	The result from Vm_CopyOut is returned, in addition to the
 *	data specified above.
 *
 * Side effects:
 *	Data is copied out to the user's address space.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
TraceLog_Dump(tracePtr, dataAddr, hdrAddr)
    register TraceLog_Header *tracePtr;
    Address dataAddr;
    Address hdrAddr;
{
    ReturnStatus status = SUCCESS;
    Boolean	abortedBySignal = FALSE;

    if (tracePtr == (TraceLog_Header *) NIL) {
	printf("TraceLog_Dump: traceLog buffer not initialized.\n");
	tracePtr->hdr.numBytes = 0;
	tracePtr->hdr.numRecs = 0;
	status = Vm_CopyOut(sizeof(Sys_TracelogHeaderKern),
		(Address) &tracePtr->hdr, (Address)hdrAddr);
	return(status);
    }

    LOCK_MONITOR;

    /*
     * Wait for a buffer to fill.
     */
    while (!tracePtr->blocked &&
	    tracePtr->firstNewBuffer==tracePtr->currentBuffer) {
	if (Sync_Wait(&bufferFullCondition,TRUE) == TRUE) {
	    abortedBySignal = TRUE;
	    break;
	}
    }

    if (abortedBySignal) {
    /*
     * Forcibly flush the record.
     */
	tracePtr->buffers[tracePtr->currentBuffer].size =
		tracePtr->currentOffset;
	tracePtr->currentOffset = 0;
#ifdef CHECKCONSIST
	tracePtr->buffers[tracePtr->currentBuffer].mode = TRACELOG_DONE;
#endif
	INC_BUFNUM(tracePtr->currentBuffer);
    }
    

#ifdef CHECKCONSIST
    if (tracePtr->buffers[tracePtr->firstNewBuffer].mode != TRACELOG_DONE) {
	printf("Warning: buffer# %d has mode %d, not DONE\n",
		tracePtr->firstNewBuffer, tracePtr->buffers[tracePtr->
		firstNewBuffer].mode);
    }
    tracePtr->buffers[tracePtr->firstNewBuffer].mode = TRACELOG_UNUSED;
#endif

    /*
     * Copy the information into the output header buffer.
     */
    tracePtr->hdr.numBytes = tracePtr->buffers[tracePtr->firstNewBuffer].size;
    tracePtr->hdr.numRecs =
	    tracePtr->buffers[tracePtr->firstNewBuffer].numRecords;
    tracePtr->hdr.lostRecords = 
	    tracePtr->buffers[tracePtr->firstNewBuffer].lostRecords;
    if (vmShmDebug) {
	printf("Lost = %d\n", tracePtr->hdr.lostRecords);
    }
    /*
     * Initialize the now-free buffer.
     */
    tracePtr->buffers[tracePtr->firstNewBuffer].numRecords=0;
    tracePtr->buffers[tracePtr->firstNewBuffer].lostRecords=0;

    if (tracePtr->blocked) {
	if (vmShmDebug) {
	    printf("Unblocked: lost = %d\n", tracePtr->lostRecords);
	}
	tracePtr->blocked = 0;
	tracePtr->buffers[tracePtr->currentBuffer].lostRecords=
		tracePtr->lostRecords;
#ifdef CHECKCONSIST
	tracePtr->buffers[tracePtr->currentBuffer].mode = TRACELOG_INUSE;
#endif
    }
    tracePtr->lostRecords = 0;

    status = Vm_CopyOut(tracePtr->hdr.numBytes, tracePtr->buffers
	    [tracePtr->firstNewBuffer].data, dataAddr);

    if (status != SUCCESS) {
	printf("Dump failed: status= %d, addr = %x\n", status, dataAddr);
	printf("numBytes: %d, srcAddr: %x, dstAdr: %x\n",
		tracePtr->hdr.numBytes,
		tracePtr->buffers[tracePtr->firstNewBuffer].data, dataAddr);
	UNLOCK_MONITOR;
	return status;
    }

    INC_BUFNUM(tracePtr->firstNewBuffer);

    status = Vm_CopyOut(sizeof(Sys_TracelogHeaderKern),
	    (Address) &tracePtr->hdr, (Address)hdrAddr);

    UNLOCK_MONITOR;
    if ((status == SUCCESS) && (abortedBySignal)) {
	status = GEN_ABORTED_BY_SIGNAL;
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * TraceLog_Reset --
 *
 *	Reset the trace log structures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The trace structure is reinitialized.
 *
 *----------------------------------------------------------------------
 */

void
TraceLog_Reset(tracePtr)
    register TraceLog_Header *tracePtr;	/* pointer to tracing info */
{
    int i;
    LOCK_MONITOR;
    tracePtr->firstNewBuffer = 0;
    tracePtr->currentBuffer = 0;
    tracePtr->currentOffset = 0;
    tracePtr->blocked = 0;
    tracePtr->totalNumRecords = 0;
    tracePtr->totalLostRecords = 0;
    for (i=0;i<tracePtr->numBuffers;i++) {
	tracePtr->buffers[i].size = 0;
	tracePtr->buffers[i].numRecords = 0;
	tracePtr->buffers[i].lostRecords = 0;
#ifdef CHECKCONSIST
	tracePtr->buffers[i].mode = TRACELOG_UNUSED;
#endif
    }
#ifdef CHECKCONSIST
	tracePtr->buffers[0].mode = TRACELOG_INUSE;
#endif
    UNLOCK_MONITOR;
}

/*
 *----------------------------------------------------------------------
 *
 * TraceLog_Finish --
 *
 *	Free the trace log structures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Everything is freed up.
 *
 *----------------------------------------------------------------------
 */

void
TraceLog_Finish(tracePtr)
    register TraceLog_Header *tracePtr;	/* pointer to tracing info */
{
    int i;

    LOCK_MONITOR;
    printf("Trace done: %d records, %d lost, %d successful\n",
	tracePtr->totalNumRecords, tracePtr->totalLostRecords,
	tracePtr->totalNumRecords-tracePtr->totalLostRecords);
    for (i=0;i<tracePtr->numBuffers;i++) {
	free(tracePtr->buffers[i].data);
    }
    free ((Address)tracePtr);
    UNLOCK_MONITOR;

}
