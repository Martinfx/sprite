/* 
 * devRaid.h --
 *
 *	Declarations for RAID device drivers.
 *
 * Copyright 1989 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * $Header: /cdrom/src/kernel/Cvsroot/kernel/raid/devRaid.h,v 1.11 92/06/25 17:20:55 eklee Exp $ SPRITE (Berkeley)
 */

#ifndef _DEVRAID
#define _DEVRAID

#include "sprite.h"
#include "sync.h"
#include "fs.h"
#include "devBlockDevice.h"
#include "devRaidDisk.h"
#include "devRaidLog.h"

#ifndef MIN
#define MIN(a,b) ( (a) < (b) ? (a) : (b) )
#endif  MIN

#ifndef MAX
#define MAX(a,b) ( (a) > (b) ? (a) : (b) )
#endif  MAX

#define BITS_PER_ADDR			32
#define RAID_MAX_XFER_SIZE		(1<<30)
#ifdef TESTING
#define RAID_ROOT_CONFIG_FILE_NAME	"RAID"
#else
#define RAID_ROOT_CONFIG_FILE_NAME	"/ra/raid/RAID"
#endif TESTING

/*
 * Data structure each RAID device.
 *
 * RAID_INVALID	==> Array has not been configured.
 * RAID_ATTACHED==> Array attached but not configured.
 * RAID_VALID	==> Array is configured and ready to service requests.
 */
typedef enum { RAID_INVALID, RAID_ATTACHED, RAID_VALID } RaidState;

typedef struct Raid {
    RaidState		 state;		/* must be first field */
    Sync_Semaphore	 mutex;		/* must be second field */
    Sync_Condition	 waitExclusive;
    Sync_Condition	 waitNonExclusive;
    int			 numReqInSys; /* -1 => exclusive access */
    int			 numWaitExclusive; /* number waiting for */
					  /* exclusive access. */

    Fs_Device		*devicePtr; /* Device corresponding to this raid. */
    int			 numCol;
    int			 numRow;
    RaidDisk	      ***disk;	    /* 2D array of disks (column major) */

    RaidLog		 log;

    unsigned		 numSector;
    int		 	 numStripe;
    int			 dataSectorsPerStripe;
    int			 dataStripeUnitsPerDisk;
    int		 	 sectorsPerDisk;
    int		 	 bytesPerStripeUnit;
    int		 	 dataBytesPerStripe;

    int		 	 numDataCol;
    int		 	 logBytesPerSector;
    int		 	 bytesPerSector;
    int		 	 sectorsPerStripeUnit;
    int		 	 rowsPerGroup;
    int		 	 stripeUnitsPerDisk;
    int		 	 groupsPerArray;
    char		 parityConfig;
} Raid;

/*
 * RaidHandle.
 */
typedef struct RaidHandle {		/* Subclass of DevBlockDeviceHandle. */
    DevBlockDeviceHandle blockHandle;	/* Must be FIRST field. */
    Fs_Device		*devPtr;	/* Device corresponding to handle */
    Raid		*raidPtr;
} RaidHandle;

/*
 * RaidBlockRequest
 *
 * REQ_INVALID	==> the request is to a failed device
 * REQ_FAILED	==> an error code was returned by the device
 * REQ_READY	==> the request is ready to be issued
 * REQ_COMPLETED==> the request has successfully completed
 * REQ_PENDING 	==> the request has been issued and is waiting for completion
 */
typedef enum RaidBlockRequestState {	/* Subclass of DevBlockDeviceRequest */
    REQ_INVALID, REQ_FAILED, REQ_READY, REQ_COMPLETED, REQ_PENDING
} RaidBlockRequestState;

typedef struct RaidBlockRequest {
    DevBlockDeviceRequest devReq;
    RaidBlockRequestState state;
    ReturnStatus	  status;
    Raid		 *raidPtr;
    int			  col;
    int			  row;
    RaidDisk		 *diskPtr;
    int			  version;
} RaidBlockRequest;

/*
 * Raid Control structures for syncronizing/communicating with
 * interrupt routines.
 */
typedef struct RaidIOControl {
    Sync_Semaphore	 mutex;
    Raid		*raidPtr;
    int			 numIO;
    void	       (*doneProc)();
    ClientData		 clientData;
    ReturnStatus	 status;
    int			 amountTransferred;
    int			 numFailed;
    RaidBlockRequest	*failedReqPtr;
} RaidIOControl;

typedef struct RaidRequestControl {
    RaidBlockRequest	*reqPtr;
    int			 numReq;
    int			 numFailed;
    RaidBlockRequest	*failedReqPtr;
} RaidRequestControl;

typedef struct RaidStripeIOControl {
    Raid		*raidPtr;
    int			 operation;
    unsigned		 firstSector;
    unsigned		 nthSector;
    Address		 buffer;
    void	       (*doneProc)();
    ClientData		 clientData;
    void	       (*recoverProc)();
    int			 ctrlData;
    RaidRequestControl	*reqControlPtr;
    char		*parityBuf;
    char		*readBuf;
    int			 rangeOff;
    int			 rangeLen;
} RaidStripeIOControl;

typedef struct RaidReconstructionControl {
    Raid		*raidPtr;
    int			 col;
    int			 row;
    RaidDisk		*diskPtr;
    int			 stripeID;
    int			 numStripe;
    void	       (*doneProc)();
    ClientData		 clientData;
    int			 ctrlData;
    RaidRequestControl	*reqControlPtr;
    ReturnStatus	 status;
    char		*parityBuf;
    char		*readBuf;
} RaidReconstructionControl;

extern DevBlockDeviceHandle *DevRaidAttach _ARGS_((Fs_Device *devicePtr));

#endif _DEVRAID
