/* 
 * devRaidUtil.c --
 *
 *	Routines for allocating, initializing and deallocating various
 *	RAID data structures.
 *	Routines for mapping logical RAID sectors to physical devices.
 *
 * Copyright 1989 Regents of the University of California
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
static char rcsid[] = "$Header: /cdrom/src/kernel/Cvsroot/kernel/raid/devRaidUtil.c,v 1.14 92/06/25 17:21:14 eklee Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include "sync.h"
#include "sprite.h"
#include "fs.h"
#include "dev.h"
#include "devBlockDevice.h"
#include "devRaid.h"
#include "semaphore.h"
#include "stdlib.h"
#include "dbg.h"
#include "devRaidUtil.h"
#include "devRaidProto.h"


/*
 *----------------------------------------------------------------------
 *
 * InitRaidBlockRequest --
 *
 *	Initialize RaidBlockRequest.
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
InitRaidBlockRequest(reqPtr, raidPtr, operation, col, row, diskSector,
	numSectorsToTransfer, buffer, ctrlData)
    RaidBlockRequest	*reqPtr;
    Raid		*raidPtr;
    int			 operation;
    int			 col, row;
    unsigned		 diskSector;
    int			 numSectorsToTransfer;
    Address		 buffer;
    int			 ctrlData;
{
    reqPtr->devReq.operation     = operation;
    reqPtr->devReq.startAddress  = SectorToByte(raidPtr, diskSector);
    reqPtr->devReq.startAddrHigh = 0;
    reqPtr->devReq.bufferLen	 = SectorToByte(raidPtr, numSectorsToTransfer);
    reqPtr->devReq.buffer        = buffer;
    reqPtr->devReq.ctrlData[0]   = ctrlData;
    reqPtr->state                = REQ_READY;
    reqPtr->status               = FAILURE;
    reqPtr->raidPtr              = raidPtr;
    reqPtr->col                  = col;
    reqPtr->row                  = row;
    reqPtr->diskPtr              = raidPtr->disk[col][row];
    reqPtr->version              = reqPtr->diskPtr->version;
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_MakeBlockDeviceRequest --
 *
 *	Allocate and initialize DevBlockDeviceRequest.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

DevBlockDeviceRequest *
Raid_MakeBlockDeviceRequest(raidPtr, operation, diskSector, numSectorsToTransfer,
	buffer, doneProc, clientData, ctrlData)
    Raid		*raidPtr;
    int			 operation;
    unsigned		 diskSector;
    int			 numSectorsToTransfer;
    Address		 buffer;
    void	       (*doneProc)();
    ClientData		 clientData;
    int			 ctrlData;
{
    DevBlockDeviceRequest	*requestPtr =
	(DevBlockDeviceRequest *) Malloc(sizeof(DevBlockDeviceRequest));

    requestPtr->operation     = operation;
    requestPtr->startAddress  = SectorToByte(raidPtr, diskSector);
    requestPtr->startAddrHigh = 0;
    requestPtr->bufferLen     = SectorToByte(raidPtr, numSectorsToTransfer);
    requestPtr->buffer        = buffer;
    requestPtr->doneProc      = doneProc;
    requestPtr->clientData    = clientData;
    requestPtr->ctrlData[0]   = ctrlData;

    return requestPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_FreeBlockDeviceRequest --
 *
 *	Free DevBlockDeviceRequest.
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
Raid_FreeBlockDeviceRequest(requestPtr)
    DevBlockDeviceRequest	*requestPtr;
{
    Free((char *) requestPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_MakeIOControl --
 *
 *	Allocate and initialize RaidIOControl.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

RaidIOControl *
Raid_MakeIOControl(doneProc, clientData)
    void	       (*doneProc)();
    ClientData		 clientData;
{
    RaidIOControl	*IOControlPtr;
    IOControlPtr = (RaidIOControl *) Malloc(sizeof(RaidIOControl));

    Sync_SemInitDynamic(&IOControlPtr->mutex, "RAID IOControl Sema");
    IOControlPtr->numIO			= 0;
    IOControlPtr->doneProc		= doneProc;
    IOControlPtr->clientData		= clientData;
    IOControlPtr->status		= SUCCESS;
    IOControlPtr->amountTransferred	= 0;
    IOControlPtr->numFailed		= 0;
    IOControlPtr->failedReqPtr		= (RaidBlockRequest *) NIL;

    return IOControlPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_FreeIOControl --
 *
 *	Free RaidIOControl.
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
Raid_FreeIOControl(IOControlPtr)
    RaidIOControl	*IOControlPtr;
{
    Sync_LockClear(&IOControlPtr->mutex);
    Free((char *) IOControlPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_MakeRequestControl --
 *
 *	Allocate and initialize RaidRequestControl.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

RaidRequestControl *
Raid_MakeRequestControl(raidPtr)
    Raid	*raidPtr;
{
    RaidRequestControl	*reqControlPtr;

    reqControlPtr = (RaidRequestControl *) Malloc(sizeof(RaidRequestControl));
    reqControlPtr->reqPtr = (RaidBlockRequest *)
    	    Malloc((unsigned) (raidPtr->numCol+4) * sizeof(RaidBlockRequest));
    reqControlPtr->numReq = 0;
    reqControlPtr->numFailed = 0;
    reqControlPtr->failedReqPtr = (RaidBlockRequest *) NIL;

    return reqControlPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_FreeRequestControl --
 *
 *	Free RaidRequestControl.
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
Raid_FreeRequestControl(reqControlPtr)
    RaidRequestControl	*reqControlPtr;
{
    Free((char *) reqControlPtr->reqPtr);
    Free((char *) reqControlPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * MakeRaidStripeIOControl --
 *
 *	Allocate and initialize RaidStripeIOControl.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

RaidStripeIOControl *
Raid_MakeStripeIOControl(raidPtr, operation, firstSector, nthSector, buffer,
	doneProc, clientData, ctrlData)
    Raid                *raidPtr;
    int			 operation;
    unsigned             firstSector;
    unsigned             nthSector;
    Address              buffer;
    void               (*doneProc)();
    ClientData           clientData;
    int                  ctrlData;
{
    RaidStripeIOControl	*stripeIOControlPtr;
    stripeIOControlPtr = (RaidStripeIOControl *)
				 Malloc(sizeof(RaidStripeIOControl));

    stripeIOControlPtr->raidPtr       = raidPtr;
    stripeIOControlPtr->operation     = operation;
    stripeIOControlPtr->firstSector   = firstSector;
    stripeIOControlPtr->nthSector     = nthSector;
    stripeIOControlPtr->buffer        = buffer;
    stripeIOControlPtr->doneProc      = doneProc;
    stripeIOControlPtr->clientData    = clientData;
    stripeIOControlPtr->recoverProc   = (void (*)()) NIL;
    stripeIOControlPtr->ctrlData      = ctrlData;
    stripeIOControlPtr->reqControlPtr = Raid_MakeRequestControl(raidPtr);
    stripeIOControlPtr->parityBuf     =
#ifdef NODATA
	    (char *) NIL;
#else
            Malloc((unsigned) raidPtr->bytesPerStripeUnit);
#endif
    stripeIOControlPtr->readBuf       =
#ifdef NODATA
	    (char *) NIL;
#else
	    Malloc((unsigned) raidPtr->dataBytesPerStripe);
#endif
    stripeIOControlPtr->rangeOff      = 0;
    stripeIOControlPtr->rangeLen      = 0;

    return stripeIOControlPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_FreeStripeIOControl --
 *
 *	Free RaidStripeIOControl.
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
Raid_FreeStripeIOControl(stripeIOControlPtr)
    RaidStripeIOControl	*stripeIOControlPtr;
{
    Raid_FreeRequestControl(stripeIOControlPtr->reqControlPtr);
#ifndef NODATA
    Free((char *) stripeIOControlPtr->parityBuf);
    Free((char *) stripeIOControlPtr->readBuf);
#endif
    Free((char *) stripeIOControlPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_MakeReconstructionControl --
 *
 *	Allocate and initialize RaidReconstructionControl.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

RaidReconstructionControl *
Raid_MakeReconstructionControl(raidPtr, col, row, diskPtr, doneProc, clientData,
	ctrlData)
    Raid        *raidPtr;
    int		 col;
    int		 row;
    RaidDisk	*diskPtr;
    void       (*doneProc)();
    ClientData   clientData;
    int		 ctrlData;
{
    RaidReconstructionControl *reconstructionControlPtr;
    reconstructionControlPtr = (RaidReconstructionControl *)
	    Malloc(sizeof(RaidReconstructionControl));

    reconstructionControlPtr->raidPtr       = raidPtr;
    reconstructionControlPtr->col           = col;
    reconstructionControlPtr->row           = row;
    reconstructionControlPtr->diskPtr       = diskPtr;
    reconstructionControlPtr->stripeID      = 0;
    reconstructionControlPtr->numStripe     = 0;
    reconstructionControlPtr->doneProc      = doneProc;
    reconstructionControlPtr->clientData    = clientData;
    reconstructionControlPtr->ctrlData      = ctrlData;
    reconstructionControlPtr->reqControlPtr = Raid_MakeRequestControl(raidPtr);
    reconstructionControlPtr->status        = SUCCESS;
    reconstructionControlPtr->parityBuf     =
#ifdef NODATA
	    (char *) NIL;
#else
            Malloc((unsigned) raidPtr->bytesPerStripeUnit);
#endif
    reconstructionControlPtr->readBuf       =
#ifdef NODATA
	    (char *) NIL;
#else
	    Malloc((unsigned) raidPtr->dataBytesPerStripe);
#endif
    return reconstructionControlPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_FreeReconstructionControl --
 *
 *	Free RaidReconstructionControl.
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
Raid_FreeReconstructionControl(reconstructionControlPtr)
    RaidReconstructionControl *reconstructionControlPtr;
{
    Raid_FreeRequestControl(reconstructionControlPtr->reqControlPtr);
#ifndef NODATA
    Free((char *) reconstructionControlPtr->parityBuf);
    Free((char *) reconstructionControlPtr->readBuf);
#endif
    Free((char *) reconstructionControlPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_RangeRestrict --
 *
 *	Restricts start and len so that they lie within rangeOffset and
 *	rangeLen.  Note that start is restricted modulo the fieldLen.
 *
 * Results:
 *	The restricted values of start and len (newStart, newLen).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Raid_RangeRestrict(start, len, rangeOffset, rangeLen, fieldLen, newStart, newLen)
    int		 start, len;
    int		 rangeOffset, rangeLen;
    int		 fieldLen;
    int		*newStart, *newLen;
{
    int		 startBase, startOffset;
    int		 newStartOffset;

    startOffset = start % fieldLen;
    startBase   = (start / fieldLen) * fieldLen;
    newStartOffset = MAX(startOffset, rangeOffset);
    *newStart = startBase + newStartOffset;
    *newLen = MIN(startOffset + len, rangeOffset + rangeLen) - newStartOffset;
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_XorRangeRequests --
 *
 *	Xor's the contents of the buffers of the requests in *reqControlPtr 
 *	restricted by rangeOffset and rangeLen and place the result in 
 *	*destBuf.
 *
 * Results:
 *      *destBuf.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Raid_XorRangeRequests(reqControlPtr, raidPtr, destBuf, rangeOffset, rangeLen)
    RaidRequestControl	*reqControlPtr;
    Raid		*raidPtr;
    char		*destBuf;   
    int			 rangeOffset;
    int			 rangeLen;
{
    RaidBlockRequest	*reqPtr;
    int			 rangeStartAddress;
    int			 newRangeLen;
    int			 i;

    rangeOffset = StripeUnitOffset(raidPtr, rangeOffset);
    for (i = 0; i < reqControlPtr->numReq; i++) {
	reqPtr = &reqControlPtr->reqPtr[i];
	if (reqPtr->state == REQ_READY || reqPtr->state == REQ_COMPLETED ) {
	    Raid_RangeRestrict((int) reqPtr->devReq.startAddress,
		    reqPtr->devReq.bufferLen,
		    rangeOffset, rangeLen, raidPtr->bytesPerStripeUnit,
		    &rangeStartAddress, &newRangeLen);
            Xor2(newRangeLen, reqPtr->devReq.buffer +
		    	    (rangeStartAddress - reqPtr->devReq.startAddress),
		    destBuf + StripeUnitOffset(raidPtr,
			    rangeStartAddress)-rangeOffset);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_AddParityRangeRequest --
 *
 *	Add a RaidBlockRequest for the indicated parity sectors to
 *	reqControlPtr.
 *
 * Results:
 *	Updates reqControlPtr.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Raid_AddParityRangeRequest(reqControlPtr, raidPtr, operation,
	sector, buffer, ctrlData, rangeOffset, rangeLen)
    RaidRequestControl	*reqControlPtr;
    Raid		*raidPtr;
    int			 operation;
    unsigned		 sector;
    Address		 buffer;
    int			 ctrlData;
    int			 rangeOffset;
    int			 rangeLen;
{
    RaidBlockRequest	*reqPtr;
    int			 col, row;
    int 		 numSectorsToTransfer;
    unsigned		 diskSector;

    rangeOffset = ByteToSector(raidPtr, StripeUnitOffset(raidPtr, rangeOffset));
    rangeLen    = ByteToSector(raidPtr, rangeLen);

    reqPtr = &reqControlPtr->reqPtr[reqControlPtr->numReq];
    sector = FirstSectorOfStripeUnit(raidPtr, sector) + rangeOffset;
    numSectorsToTransfer = rangeLen;

    /*
     * Map logical Raid sector address to (diskHandlePtr, diskSector).
     */
    Raid_MapParity(raidPtr, sector, &col, &row, &diskSector);

    if (numSectorsToTransfer > 0) {
	InitRaidBlockRequest(reqPtr, raidPtr, operation, col, row,
		diskSector, numSectorsToTransfer, buffer, ctrlData);
	if ( IsValid(reqPtr->diskPtr, diskSector, numSectorsToTransfer) ) {
	    reqPtr->state = REQ_READY;
	} else {
	    reqPtr->state = REQ_INVALID;
	    reqControlPtr->numFailed++;
	    reqControlPtr->failedReqPtr = reqPtr;
	}
        reqControlPtr->numReq++;
    }
    if (reqControlPtr->numReq >= raidPtr->numCol +4) {
	panic("Error: AddRaidParity: reqControl overrun.\n");
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Raid_AddDataRangeRequests --
 *
 *	Add RaidBlockRequest's for the indicated data sectors to
 *	reqControlPtr.
 *
 * Results:
 *	Updates reqControlPtr.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Raid_AddDataRangeRequests(reqControlPtr, raidPtr, operation,
		firstSector, nthSector, buffer, ctrlData, rangeOffset, rangeLen)
    RaidRequestControl	*reqControlPtr;
    Raid		*raidPtr;
    int			 operation;
    unsigned		 firstSector, nthSector;
    Address		 buffer;
    int			 ctrlData;
    int			 rangeOffset;
    int			 rangeLen;
{
    RaidBlockRequest	*reqPtr;
    int			 col, row;
    int 		 numSectorsToTransfer;
    int 		 rangeSectorsToTransfer;
    unsigned 		 currentSector;
    unsigned		 diskSector;

    rangeOffset = ByteToSector(raidPtr, StripeUnitOffset(raidPtr, rangeOffset));
    rangeLen    = ByteToSector(raidPtr, rangeLen);
    /*
     * Break up requests into stripe units.
     */
    currentSector = firstSector;
    while ( currentSector < nthSector ) {
	reqPtr = &reqControlPtr->reqPtr[reqControlPtr->numReq];
        numSectorsToTransfer = MIN( raidPtr->sectorsPerStripeUnit -
		currentSector%raidPtr->sectorsPerStripeUnit,
		nthSector - currentSector );
	/*
	 * Map logical Raid sector address to (diskHandlePtr, diskSector).
	 */
	Raid_MapSector(raidPtr, currentSector, &col, &row, &diskSector);

	Raid_RangeRestrict((int) diskSector, numSectorsToTransfer,
		rangeOffset, rangeLen,
		raidPtr->sectorsPerStripeUnit,
		(int *) &(diskSector), &rangeSectorsToTransfer);

	if (rangeSectorsToTransfer > 0) {
	    InitRaidBlockRequest(reqPtr, raidPtr, operation, col, row,
		    diskSector, rangeSectorsToTransfer, buffer, ctrlData);
	    if (IsValid(reqPtr->diskPtr, diskSector,rangeSectorsToTransfer)) {
		reqPtr->state = REQ_READY;
	    } else {
		reqPtr->state = REQ_INVALID;
		reqControlPtr->numFailed++;
		reqControlPtr->failedReqPtr = reqPtr;
	    }
	    reqControlPtr->numReq++;
	}

        currentSector += numSectorsToTransfer;
	buffer += SectorToByte(raidPtr, numSectorsToTransfer);
    }
    if (reqControlPtr->numReq >= raidPtr->numCol +4) {
	panic("Error: AddRaidData: reqControl overrun.\n");
    }
}
