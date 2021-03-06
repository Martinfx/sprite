/* 
 * miscutil.c --
 *
 * Copyright 1992 Regents of the University of California
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
static char rcsid[] = "$Header$ SPRITE (Berkeley)";
#endif /* not lint */

#include "sync.h"
#include "sprite.h"
#include "fs.h"
#include "dev.h"
#include "devBlockDevice.h"
#include "semaphore.h"
#include "stdlib.h"
#include "dbg.h"
#include "miscutil.h"
#include "varargs.h"


/*
 *----------------------------------------------------------------------
 *
 * Raid_InitSyncControl --
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
void
Raid_InitSyncControl(syncControlPtr)
    SyncControl	*syncControlPtr;
{
    Sync_SemInitDynamic(&syncControlPtr->mutex, "SyncControl mtuex");
#ifdef TESTING
    Sync_CondInit(&syncControlPtr->wait);
#endif TESTING
    syncControlPtr->numIO = 1;
    syncControlPtr->status = SUCCESS;
}

/*
 *----------------------------------------------------------------------
 *
 * Raid_StartIO --
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
void
Raid_StartSyncIO(syncControlPtr)
    SyncControl	*syncControlPtr;
{
    MASTER_LOCK(&syncControlPtr->mutex);
    syncControlPtr->numIO++;
    MASTER_UNLOCK(&syncControlPtr->mutex);
}

/*
 *----------------------------------------------------------------------
 *
 * Raid_WaitIO --
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Raid_WaitSyncIO(syncControlPtr)
    SyncControl	*syncControlPtr;
{
    MASTER_LOCK(&syncControlPtr->mutex);
    syncControlPtr->numIO--;
    if (syncControlPtr->numIO == 0) {
	MASTER_UNLOCK(&syncControlPtr->mutex);
    } else {
	Sync_MasterWait(&syncControlPtr->wait, &syncControlPtr->mutex, FALSE);
	MASTER_UNLOCK(&syncControlPtr->mutex);
    }
    /*
     * Setting numIO to 1 again allows syncControlPtr to be used again without
     * calling Raid_InitSyncControl.
     */
    syncControlPtr->numIO = 1;
    return syncControlPtr->status;
}

/*
 *----------------------------------------------------------------------
 *
 * Raid_SyncDoneProc --
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
int
Raid_SyncDoneProc(syncControlPtr, status)
    SyncControl		*syncControlPtr;
    ReturnStatus	status;
{
    if (status != SUCCESS) {
        syncControlPtr->status = status;
    }
    MASTER_LOCK(&syncControlPtr->mutex);
    syncControlPtr->numIO--;
    if (syncControlPtr->numIO == 0) {
        Sync_MasterBroadcast(&syncControlPtr->wait);
	MASTER_UNLOCK(&syncControlPtr->mutex);
	return 1;
    } else {
	MASTER_UNLOCK(&syncControlPtr->mutex);
	return 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DevIOSync --
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
static ReturnStatus
DevIOSync(devHandlePtr, devOffset, buffer, bufSize, op)
    DevBlockDeviceHandle	*devHandlePtr;
    int				devOffset;
    char			*buffer;
    int				bufSize;
    int				op;
{
    DevBlockDeviceRequest       req;
    int                         xferAmt;
    ReturnStatus                status;

    req.operation       = op;
    req.startAddress    = devOffset;
    req.startAddrHigh   = 0;
    req.buffer          = buffer;
    req.bufferLen       = bufSize;
    status = Dev_BlockDeviceIOSync(devHandlePtr, &req, &xferAmt);
    if (xferAmt != bufSize) {
	return FAILURE;
    }
    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * Raid_DevReadSync --
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Raid_DevReadSync(devHandlePtr, devOffset, buffer, bufSize)
    DevBlockDeviceHandle	*devHandlePtr;
    int				devOffset;
    char			*buffer;
    int				bufSize;
{
    return DevIOSync(devHandlePtr, devOffset, buffer, bufSize, FS_READ);
}

/*
 *----------------------------------------------------------------------
 *
 * Raid_DevWriteSync --
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Raid_DevWriteSync(devHandlePtr, devOffset, buffer, bufSize)
    DevBlockDeviceHandle	*devHandlePtr;
    int				devOffset;
    char			*buffer;
    int				bufSize;
{
    return DevIOSync(devHandlePtr, devOffset, buffer, bufSize, FS_WRITE);
}

/*
 *----------------------------------------------------------------------
 *
 * Raid_DevWriteInt --
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
/*VARARGS3*/
ReturnStatus
Raid_DevWriteInt(va_alist)
    va_dcl
{
    DevBlockDeviceHandle        *devHandlePtr;
    int				devOffset;
    int				numInt;

    int			bufSize;
    int			*buffer;
    ReturnStatus        status;
    int			i;
    va_list		argList;

    va_start(argList);
    devHandlePtr	= va_arg(argList, DevBlockDeviceHandle *);
    devOffset		= va_arg(argList, int);
    numInt		= va_arg(argList, int);

    bufSize = RoundUp(sizeof(int)*numInt, devHandlePtr->minTransferUnit);
    buffer  = (int *) malloc(bufSize);
    for (i = 0; i < numInt; i++) {
	buffer[i] = va_arg(argList, int);
    }
    status = Raid_DevWriteSync(devHandlePtr, devOffset, (char *)buffer,bufSize);
    free((char *) buffer);
    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * Raid_DevReadInt --
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
/*VARARGS3*/
ReturnStatus
Raid_DevReadInt(va_alist)
    va_dcl
{
    DevBlockDeviceHandle        *devHandlePtr;
    int				devOffset;
    int				numInt;

    int			bufSize;
    int			*buffer;
    ReturnStatus        status;
    int			i;
    va_list		argList;

    va_start(argList);
    devHandlePtr	= va_arg(argList, DevBlockDeviceHandle *);
    devOffset		= va_arg(argList, int);
    numInt		= va_arg(argList, int);

    bufSize = RoundUp(sizeof(int *)*numInt, devHandlePtr->minTransferUnit);
    buffer  = (int *) malloc(bufSize);
    status = Raid_DevReadSync(devHandlePtr, devOffset, (char *)buffer, bufSize);
    for (i = 0; i < numInt; i++) {
	*va_arg(argList, int *) = buffer[i];
    }
    va_end(argList);
    free((char *) buffer);
    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * Raid_WaitTime --
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
void
Raid_WaitTime(ms)
    int		ms;
{
    Time time;

    time.seconds = ms/1000;
    time.microseconds = (ms % 1000) * 1000;
    Sync_WaitTime(time);
}
