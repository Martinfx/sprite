/*
 * fsioDevice.h --
 *
 *	Declarations for device access.  The DEVICE operation switch is
 *	defined here.  The I/O handle formas for devices is defined here.
 *
 * Copyright 1987 Regents of the University of California
 * All rights reserved.
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 *
 * $Header: /cdrom/src/kernel/Cvsroot/kernel/fsio/fsioDevice.h,v 9.6 92/08/10 17:26:48 mgbaker Exp $ SPRITE (Berkeley)
 */

#ifndef _FSIODEVICE
#define _FSIODEVICE

#include <fsio.h>
#include <fsioLock.h>
#include <fsNameOps.h>

/*
 * The I/O descriptor for a local device: FSIO_LCL_DEVICE_STREAM
 */

typedef struct Fsio_DeviceIOHandle {
    Fs_HandleHeader	hdr;		/* Standard handle header. The
					 * 'major' field of the fileID is
					 * the device type.  The 'minor'
					 * field is the unit number. */
    List_Links		clientList;	/* List of clients of the device. */
    Fsio_UseCounts		use;		/* Summary reference counts. */
    Fs_Device		device;		/* Device info passed to drivers.
					 * This includes a clientData field. */
    int			flags;		/* Flags returned by the device open.*/
    Fsio_LockState		lock;		/* User level lock state. */
    int			accessTime;	/* Cached version of access time */
    int			modifyTime;	/* Cached version of modify time */
    List_Links		readWaitList;	/* List of waiting reader processes. */
    List_Links		writeWaitList;	/* List of waiting writer processes. */
    List_Links		exceptWaitList;	/* List of process waiting for
					 * exceptions (is this needed?). */
    int			notifyFlags;	/* Bits set to optimize out notifies */
} Fsio_DeviceIOHandle;			/* 136 BYTES */

/*
 * Data transferred when a local device stream migrates.
 */
typedef struct Fsio_DeviceMigData {
    int foo;
} Fsio_DeviceMigData;

/*
 * The client data set up by the device pre-open routine on the server and
 * used by the device open routine on the client.
 */
typedef struct Fsio_DeviceState {
    int		accessTime;	/* Access time from disk descriptor */
    int		modifyTime;	/* Modify time from disk descriptor */
    Fs_FileID	streamID;	/* Used to set up client list */
} Fsio_DeviceState;

/*
 * Paramters for a device reopen RPC used to reestablish state on the
 * I/O server for a device.
 */
typedef struct Fsio_DeviceReopenParams {
    Fs_FileID		fileID;	/* File ID of file to reopen.  MUST BE FIRST! */
    Fsio_UseCounts	use;	/* Device usage information. */
} Fsio_DeviceReopenParams;

/*
 * Device support
 */
extern void Fsio_DevNotifyException _ARGS_((Fs_NotifyToken notifyToken));
extern void Fsio_DevNotifyWriter _ARGS_((Fs_NotifyToken notifyToken));
extern void Fsio_DevNotifyReader _ARGS_((Fs_NotifyToken notifyToken));
extern ReturnStatus Fsio_VanillaDevReopen _ARGS_((Fs_Device *devicePtr, 
			int refs, int writes, Fs_NotifyToken notifyToken));

/*
 * Open operations.
 */

extern ReturnStatus Fsio_DeviceClose _ARGS_((Fs_Stream *streamPtr, 
				int clientID, Proc_PID procID, int flags, 
				int size, ClientData data));
/*
 * Recovery testing operations.
 */
extern int Fsio_DeviceRecovTestUseCount _ARGS_((Fsio_DeviceIOHandle *handlePtr));
/*
 * Fast recov stuff.
 */
#include <fsrecovTypes.h>
extern ReturnStatus Fsio_DeviceSetupHandle _ARGS_((Fsrecov_HandleState *recovInfoPtr));


/*
 * Stream operations.
 */
extern ReturnStatus Fsio_DeviceIoOpen _ARGS_((Fs_FileID *ioFileIDPtr,
		int *flagsPtr, int clientID, ClientData streamData, char *name,
		Fs_HandleHeader **ioHandlePtrPtr));
extern ReturnStatus Fsio_DeviceReopen _ARGS_((Fs_HandleHeader *hdrPtr,
		int clientID, ClientData inData, int *outSizePtr,
		ClientData *outDataPtr));
extern ReturnStatus Fsio_DeviceRead _ARGS_((Fs_Stream *streamPtr, 
		Fs_IOParam *readPtr, Sync_RemoteWaiter *remoteWaitPtr, 
		Fs_IOReply *replyPtr));
extern ReturnStatus Fsio_DeviceWrite _ARGS_((Fs_Stream *streamPtr, 
		Fs_IOParam *writePtr, Sync_RemoteWaiter *remoteWaitPtr, 
		Fs_IOReply *replyPtr));
extern ReturnStatus Fsio_DeviceSelect _ARGS_((Fs_HandleHeader *hdrPtr, 
		Sync_RemoteWaiter *waitPtr, int *readPtr, int *writePtr, 
		int *exceptPtr));
extern ReturnStatus Fsio_DeviceIOControl _ARGS_((Fs_Stream *streamPtr, 
		Fs_IOCParam *ioctlPtr, Fs_IOReply *replyPtr));
extern ReturnStatus Fsio_DeviceGetIOAttr _ARGS_((Fs_FileID *fileIDPtr, 
		int clientID, register Fs_Attributes *attrPtr));
extern ReturnStatus Fsio_DeviceSetIOAttr _ARGS_((Fs_FileID *fileIDPtr, 
		Fs_Attributes *attrPtr, int flags));
extern ReturnStatus Fsio_DeviceMigClose _ARGS_((Fs_HandleHeader *hdrPtr, 
		int flags));
extern ReturnStatus Fsio_DeviceMigrate _ARGS_((Fsio_MigInfo *migInfoPtr, 
		int dstClientID, int *flagsPtr, int *offsetPtr, int *sizePtr,
		Address *dataPtr));
extern ReturnStatus Fsio_DeviceMigOpen _ARGS_((Fsio_MigInfo *migInfoPtr, int size,
		ClientData data, Fs_HandleHeader **hdrPtrPtr));
extern Boolean Fsio_DeviceScavenge _ARGS_((Fs_HandleHeader *hdrPtr));
extern void Fsio_DeviceClientKill _ARGS_((Fs_HandleHeader *hdrPtr, 
		int clientID));
extern Boolean FsioDeviceHandleInit _ARGS_((Fs_FileID *fileIDPtr, 
		char *name, Fsio_DeviceIOHandle **newHandlePtrPtr));
extern ReturnStatus FsioDeviceCloseInt _ARGS_((
		Fsio_DeviceIOHandle *devHandlePtr, int useFlags, int refs,
		int writes));

#endif /* _FSIODEVICE */
