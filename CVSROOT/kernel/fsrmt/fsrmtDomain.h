/*
 * fsSpriteDomain.h --
 *
 *	Definitions of the parameters required for Sprite Domain operations
 *
 * Copyright (C) 1985 Regents of the University of California
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
 * $Header$ SPRITE (Berkeley)
 */

#ifndef _FSSPRITEDOMAIN
#define _FSSPRITEDOMAIN

#include "fsNameOps.h"
#include "proc.h"
#include "fsrmt.h"

/*
 * These typedefs are replaced by stuff in fsrmtInt.h
 */
#ifdef notdef

/*
 * Parameters for the read and write RPCs.
 */

typedef struct FsrmtRemoteIOParam {
    Fs_FileID	fileID;			/* Identifies file to read from */
    Fs_FileID	streamID;		/* Identifies stream (for offset) */
    Sync_RemoteWaiter waiter;		/* Process info for remote waiting */
    Fs_IOParam	io;			/* I/O parameter block */
} FsrmtRemoteIOParam;

/*
 * Parameters for the iocontrol RPC
 */

typedef struct FsrmtSpriteIOCParams {
    Fs_FileID	fileID;		/* File to manipulate. */
    Fs_FileID	streamID;	/* Stream to the file, needed for locking */
    Proc_PID	procID;		/* ID of invoking process */
    Proc_PID	familyID;	/* Family of invoking process */
    int		command;	/* I/O Control to perform. */
    int		inBufSize;	/* Size of input params to ioc. */
    int		outBufSize;	/* Size of results from ioc. */
    int		byteOrder;	/* Defines client's byte ordering */
    int		uid;		/* Effective User ID */
} FsrmtSpriteIOCParams;

/*
 * Parameters for the I/O Control RPC.  (These aren't used, oops,
 * someday they should be used.)
 */

typedef struct FsrmtRemoteIOCParam {
    Fs_FileID	fileID;		/* File to manipulate. */
    Fs_FileID	streamID;	/* Stream to the file, needed for locking */
    Fs_IOCParam	ioc;		/* IOControl parameter block */
} FsrmtRemoteIOCParam;


/*
 * Parameters for the block copy RPC.
 */
typedef struct FsrmtRemoteBlockCopyParams {
    Fs_FileID	srcFileID;	/* File to copy from. */
    Fs_FileID	destFileID;	/* File to copy to. */
    int		blockNum;	/* Block to copy to. */
} FsrmtRemoteBlockCopyParams;

#endif

/*
 * RPC debugging.
 */
#ifndef CLEAN
#define FSRMT_RPC_DEBUG_PRINT(string) \
	if (fsrmt_RpcDebug) {\
	    printf(string);\
	}
#define FSRMT_RPC_DEBUG_PRINT1(string, arg1) \
	if (fsrmt_RpcDebug) {\
	    printf(string, arg1);\
	}
#define FSRMT_RPC_DEBUG_PRINT2(string, arg1, arg2) \
	if (fsrmt_RpcDebug) {\
	    printf(string, arg1, arg2);\
	}
#define FSRMT_RPC_DEBUG_PRINT3(string, arg1, arg2, arg3) \
	if (fsrmt_RpcDebug) {\
	    printf(string, arg1, arg2, arg3);\
	}
#define FSRMT_RPC_DEBUG_PRINT4(string, arg1, arg2, arg3, arg4) \
	if (fsrmt_RpcDebug) {\
	    printf(string, arg1, arg2, arg3, arg4);\
	}
#else
#define FSRMT_RPC_DEBUG_PRINT(string)
#define FSRMT_RPC_DEBUG_PRINT1(string, arg1)
#define FSRMT_RPC_DEBUG_PRINT2(string, arg1, arg2)
#define FSRMT_RPC_DEBUG_PRINT3(string, arg1, arg2, arg3)
#define FSRMT_RPC_DEBUG_PRINT4(string, arg1, arg2, arg3, arg4)
#endif not CLEAN


 /*
 * Sprite Domain functions called via Fsprefix_LookupOperation.
 * These are called with a pathname.
 */
extern	ReturnStatus	FsrmtSpriteImport();
extern	ReturnStatus	FsrmtSpriteOpen();
extern	ReturnStatus	FsrmtSpriteReopen();
extern	ReturnStatus	FsrmtSpriteDevOpen();
extern	ReturnStatus	FsrmtSpriteDevClose();
extern	ReturnStatus	FsrmtRemoteGetAttrPath();
extern	ReturnStatus	FsrmtRemoteSetAttrPath();
extern	ReturnStatus	FsrmtSpriteMakeDevice();
extern	ReturnStatus	FsrmtSpriteMakeDir();
extern	ReturnStatus	FsrmtSpriteRemove();
extern	ReturnStatus	FsrmtSpriteRemoveDir();
extern	ReturnStatus	FsrmtSpriteRename();
extern	ReturnStatus	FsrmtSpriteHardLink();


/*
 * Sprite Domain functions called via the fsAttrOpsTable switch.
 * These are called with a fileID.
 */
extern	ReturnStatus	FsrmtRemoteGetAttr();
extern	ReturnStatus	FsrmtRemoteSetAttr();

extern ReturnStatus FsrmtRmtDeviceCltOpen();
extern Fs_HandleHeader *FsrmtRmtDeviceVerify();
extern ReturnStatus FsrmtRmtDeviceMigrate();
extern ReturnStatus FsrmtRmtDeviceReopen();

extern Fs_HandleHeader *FsrmtRmtPipeVerify();
extern ReturnStatus FsrmtRmtPipeMigrate();
extern ReturnStatus FsrmtRmtPipeReopen();
extern ReturnStatus FsrmtRmtPipeClose();


extern ReturnStatus	FsrmtRmtFileCltOpen();
extern Fs_HandleHeader	*FsrmtRmtFileVerify();
extern ReturnStatus	FsrmtRmtFileRead();
extern ReturnStatus	FsrmtRmtFileWrite();
extern ReturnStatus	FsrmtRmtFileIOControl();
extern ReturnStatus	FsrmtRmtFileSelect();
extern ReturnStatus	FsrmtRmtFileGetIOAttr();
extern ReturnStatus	FsrmtRmtFileSetIOAttr();
extern ReturnStatus	FsrmtRmtFileRelease();
extern ReturnStatus	FsrmtRmtFileMigEnd();
extern ReturnStatus	FsrmtRmtFileMigrate();
extern ReturnStatus	FsrmtRmtFileReopen();
extern ReturnStatus     FsrmtRmtFileAllocate();
extern ReturnStatus     FsrmtRmtFileBlockRead();
extern ReturnStatus     FsrmtRmtFileBlockWrite();
extern ReturnStatus     FsrmtRmtFileBlockCopy();
extern Boolean		FsrmtRmtFileScavenge();
extern ReturnStatus	FsrmtRmtFileClose();


#endif _FSSPRITEDOMAIN
