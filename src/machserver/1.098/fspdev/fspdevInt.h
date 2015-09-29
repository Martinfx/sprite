/*
 * fspdevInt.h --
 *
 *	Declarations for pseudo-devices and pseudo-filesystems.
 *
 *	A pseudo-device is a file that acts as a communication channel
 *	between a user-level server process (hereafter called the "server"),
 *	and one or more client processes (hereafter called the "clients").
 *	Regular filesystem system calls (Fs_Read, Fs_Write, Fs_IOControl,
 *	Fs_Close) by a client process are forwarded to the server using
 *	a request-response procotol.  The server process can implement any
 *	sort of sementics for the file operations it wants to. The general
 *	format of Fs_IOControl, in particular, lets the server implement
 *	any remote procedure call it cares to define.
 *
 *	A pseudo-filesystem is a whole sub-tree of the filesystem that
 *	is controlled by a user-level server process.  The basic request
 *	response protocol is still used for communication.  In addition to
 *	file access operations, file naming operations are handled by
 *	a pseudo-filesystem server.  The pseudo-filesystem server can
 *	establish pseudo-device like connections for each pseudo-file
 *	that is opened, or it can open regular files and connect its
 *	clients to those files instead.
 *
 *	The user include file <dev/pdev.h> defines the request-response
 *	protocol as viewed by the user-level server process.
 *
 *      The handle definitions have been moved to fspdev.h   JMS
 *
 * Copyright 1987 Regents of the University of California
 * All rights reserved.
 *
 *
 * $Header: /sprite/src/kernel/fspdev/RCS/fspdevInt.h,v 9.4 91/06/26 01:06:44 mottsmth Exp $ SPRITE (Berkeley)
 */

#ifndef _FSPDEVINT
#define _FSPDEVINT

#include <trace.h>
#include <dev/pdev.h>
#include <dev/pfs.h>
#include <fsioLock.h>
#include <fspdev.h>

#include <stdio.h>

/*
 * Because there are corresponding control handles on the file server,
 * which records which host has the pdev server, and on the pdev server
 * itself, we need to be able to reopen the control handle on the
 * file server after it reboots.
 */
typedef struct FspdevControlReopenParams {
    Fs_FileID	fileID;		/* FileID of the control handle */
    int		serverID;	/* ServerID recorded in control handle.
				 * This may be NIL if the server closes
				 * while the file server is down. */
    int		seed;		/* Used to create unique pseudo-stream fileIDs*/
} FspdevControlReopenParams;

/*
 * The following control messages are passed internally from the
 * ServerStreamCreate routine to the FspdevControlRead routine.
 * They contain a streamPtr for a new server stream
 * that gets converted to a user-level streamID in FspdevControlRead.
 */

typedef struct FspdevNotify {
    List_Links links;
    Fs_Stream *streamPtr;
} FspdevNotify;

/*
 * The following types and macros are used to take pdev traces.
 */
typedef enum { PDEVT_NIL, PDEVT_SRV_OPEN, PDEVT_CLT_OPEN, PDEVT_READ_AHEAD,
	       PDEVT_SRV_CLOSE, PDEVT_CLT_CLOSE, PDEVT_READ_WAIT, 
	       PDEVT_WAKEUP, PDEVT_REQUEST, PDEVT_REPLY,
	       PDEVT_SRV_READ, PDEVT_SRV_READ_WAIT, PDEVT_SRV_WRITE,
	       PDEVT_SRV_SELECT, PDEVT_CNTL_READ, PDEVT_WAIT_LIST,
	       PDEVT_SELECT} FspdevTraceRecType ;

typedef struct FspdevTraceRecord {
    int index;
    Fs_FileID fileID;
    union {
	Pdev_RequestHdr	requestHdr;
	Pdev_Reply	reply;
	int		selectBits;
	struct WaitTrace {
	    int		selectBits;
	    Proc_PID	procID;
	} wait;
	Fsio_UseCounts	use;
    } un;
} FspdevTraceRecord;

#ifndef CLEAN

#define PDEV_TRACE(fileIDPtr, event) \
    if (fspdevTracing) { \
	FspdevTraceRecord rec; \
	rec.index = ++fspdevTraceIndex; \
	rec.fileID = *(fileIDPtr); \
	Trace_Insert(fspdevTraceHdrPtr, event, (ClientData)&rec);	\
    }

#define PDEV_REQUEST(fileIDPtr, requestHdrPtr) \
    if (fspdevTracing) {							\
	FspdevTraceRecord rec; \
	rec.index = ++fspdevTraceIndex; \
	rec.fileID = *(fileIDPtr); \
	rec.un.requestHdr = *(requestHdrPtr); \
	Trace_Insert(fspdevTraceHdrPtr, PDEVT_REQUEST, (ClientData)&rec);\
    }

#define PDEV_REPLY(fileIDPtr, replyPtr) \
    if (fspdevTracing) {							\
	FspdevTraceRecord rec; \
	rec.index = ++fspdevTraceIndex; \
	rec.fileID = *(fileIDPtr); \
	rec.un.reply = *(replyPtr); \
	Trace_Insert(fspdevTraceHdrPtr, PDEVT_REPLY, (ClientData)&rec);\
    }

#define PDEV_TSELECT(fileIDPtr, read, write, except) \
    if (fspdevTracing) {							\
	FspdevTraceRecord rec; \
	rec.index = ++fspdevTraceIndex; \
	rec.fileID = *(fileIDPtr); \
	rec.un.selectBits = 0; \
	if (read) { rec.un.selectBits |= FS_READABLE; } \
	if (write) { rec.un.selectBits |= FS_WRITABLE; } \
	if (except) { rec.un.selectBits |= FS_EXCEPTION; } \
	Trace_Insert(fspdevTraceHdrPtr, PDEVT_SELECT, (ClientData)&rec);\
    }

#define PDEV_WAKEUP(fileIDPtr, zprocID, zselectBits) \
    if (fspdevTracing) {							\
	FspdevTraceRecord rec; \
	rec.index = ++fspdevTraceIndex; \
	rec.fileID = *(fileIDPtr); \
	rec.un.wait.selectBits = zselectBits; \
	rec.un.wait.procID = zprocID; \
	Trace_Insert(fspdevTraceHdrPtr, PDEVT_WAKEUP, (ClientData)&rec);\
    }
#define DBG_PRINT(fmt)	if (fspdev_Debug) { printf fmt ; }

#define PDEV_REQUEST_PRINT(fileIDPtr, requestHdrPtr) \
    switch(requestHdrPtr->operation) {	\
	case PDEV_OPEN:	\
	    DBG_PRINT( ("Pdev %d,%d: Open  ", (fileIDPtr)->major, \
					     (fileIDPtr)->minor) ); \
	    break;	\
	case PDEV_READ:	\
	    DBG_PRINT( ("Pdev %d,%d: Read  ", (fileIDPtr)->major, \
					     (fileIDPtr)->minor) ); \
	    break;	\
	case PDEV_WRITE:	\
	    DBG_PRINT( ("Pdev %d,%d: Write ", (fileIDPtr)->major, \
					     (fileIDPtr)->minor) ); \
	    break;	\
	case PDEV_CLOSE:	\
	    DBG_PRINT( ("Pdev %d,%d: Close ", (fileIDPtr)->major, \
					     (fileIDPtr)->minor) ); \
	    break;	\
	case PDEV_IOCTL:	\
	    DBG_PRINT( ("Pdev %d,%d: Ioctl ", (fileIDPtr)->major, \
					     (fileIDPtr)->minor) ); \
	    break;	\
	default:	\
	    DBG_PRINT( ("Pdev %d,%d: ?!?   ", (fileIDPtr)->major, \
					     (fileIDPtr)->minor) ); \
	    break;	\
    }
#else
/*
 * Compiling with -DCLEAN will zap the if statements and procedure
 * calls defined by the above macros
 */
#define DBG_PRINT(fmt)

#define PDEV_TRACE(fileIDPtr, event)
#define PDEV_REQUEST(fileIDPtr, requestHdrPtr)
#define PDEV_REPLY(fileIDPtr, replyPtr)
#define PDEV_TSELECT(fileIDPtr, read, write, except)
#define PDEV_WAKEUP(fileIDPtr, waitInfoPtr, selectBits)

#endif not CLEAN

/*
 * Internal Pdev routines
 */
extern ReturnStatus FspdevSignalOwner _ARGS_((
		Fspdev_ControlIOHandle *ctrlHandlePtr, Fs_IOCParam *ioctlPtr));
extern Fspdev_ClientIOHandle *FspdevConnect _ARGS_((
		Fspdev_ControlIOHandle *ctrlHandlePtr, Fs_FileID *ioFileIDPtr,
		int clientID, Boolean naming));

/*
 * Definitions for a trace of the request-response protocol.
 */
extern Trace_Header fspdevTraceHdr;
extern Trace_Header *fspdevTraceHdrPtr;
extern int fspdevTraceLength;
extern Boolean fspdevTracing;
extern int fspdevMaxTraceDataSize;
extern int fspdevTraceIndex;
/*
 * File server open-time routines.
 */
extern ReturnStatus FspdevNameOpen _ARGS_((Fsio_FileIOHandle *handlePtr,
		Fs_OpenArgs *openArgsPtr, Fs_OpenResults *openResultsPtr));
extern ReturnStatus FspdevRmtLinkNameOpen _ARGS_((Fsio_FileIOHandle *handlePtr,
		Fs_OpenArgs *openArgsPtr, Fs_OpenResults *openResultsPtr));
/*
 * Control Stream routines.
 */
extern Fspdev_ControlIOHandle *FspdevControlHandleInit _ARGS_((
		Fs_FileID *fileIDPtr, char *name));
extern ReturnStatus FspdevControlIoOpen _ARGS_((Fs_FileID *ioFileIDPtr, 
		int *flagsPtr, int clientID, ClientData streamData, char *name,
		Fs_HandleHeader **ioHandlePtrPtr));
extern ReturnStatus FspdevControlSelect _ARGS_((Fs_HandleHeader *hdrPtr, 
		Sync_RemoteWaiter *waitPtr, int *readPtr, int *writePtr, 
		int *exceptPtr));
extern ReturnStatus FspdevControlRead _ARGS_((Fs_Stream *streamPtr, 
		Fs_IOParam *readPtr, Sync_RemoteWaiter *waitPtr, 
		Fs_IOReply *replyPtr));
extern ReturnStatus FspdevControlIOControl _ARGS_((Fs_Stream *streamPtr, 
		Fs_IOCParam *ioctlPtr, Fs_IOReply *replyPtr));
extern ReturnStatus FspdevControlGetIOAttr _ARGS_((Fs_FileID *fileIDPtr, 
		int clientID, Fs_Attributes *attrPtr));
extern ReturnStatus FspdevControlSetIOAttr _ARGS_((Fs_FileID *fileIDPtr, 
		Fs_Attributes *attrPtr, int flags));
extern Fs_HandleHeader *FspdevControlVerify _ARGS_((Fs_FileID *fileIDPtr, 
		int pdevServerHostID, int *domainTypePtr));
extern ReturnStatus FspdevControlReopen _ARGS_((Fs_HandleHeader *hdrPtr,
		int clientID, ClientData inData, int *outSizePtr, 
		ClientData *outDataPtr));
#ifdef SOSP91
extern ReturnStatus FspdevControlClose _ARGS_((Fs_Stream *streamPtr, 
		int clientID, Proc_PID procID, int flags, int size,
		ClientData data, int *offsetPtr, int *rwFlagsPtr));
#else
extern ReturnStatus FspdevControlClose _ARGS_((Fs_Stream *streamPtr, 
		int clientID, Proc_PID procID, int flags, int size,
		ClientData data));
#endif
extern void FspdevControlClientKill _ARGS_((Fs_HandleHeader *hdrPtr,
		int clientID));
extern Boolean FspdevControlScavenge _ARGS_((Fs_HandleHeader *hdrPtr));
/*
 * Pfs Control Stream routines.
 */
extern ReturnStatus FspdevPfsIoOpen _ARGS_((Fs_FileID *ioFileIDPtr,
		int *flagsPtr, int clientID, ClientData streamData,
		char *name, Fs_HandleHeader **ioHandlePtrPtr));
/*
 * Server stream routines.
 */
extern ReturnStatus FspdevServerStreamSelect _ARGS_((Fs_HandleHeader *hdrPtr,
		Sync_RemoteWaiter *waitPtr, int *readPtr, int *writePtr, 
		int *exceptPtr));
extern ReturnStatus FspdevServerStreamRead _ARGS_((Fs_Stream *streamPtr, 
		Fs_IOParam *readPtr, Sync_RemoteWaiter *waitPtr,
		Fs_IOReply *replyPtr));
extern ReturnStatus FspdevServerStreamIOControl _ARGS_((Fs_Stream *streamPtr, 
		Fs_IOCParam *ioctlPtr, Fs_IOReply *replyPtr));
#ifdef SOSP91
extern ReturnStatus FspdevServerStreamClose _ARGS_((Fs_Stream *streamPtr, 
		int clientID, Proc_PID procID, int flags, int size, 
		ClientData data, int *offsetPtr, int *rwFlagsPtr));
#else
extern ReturnStatus FspdevServerStreamClose _ARGS_((Fs_Stream *streamPtr, 
		int clientID, Proc_PID procID, int flags, int size, 
		ClientData data));
#endif
/*
 * Pseudo-device (client-side) streams
 */
extern ReturnStatus FspdevPseudoStreamIoOpen _ARGS_(( Fs_FileID *ioFileIDPtr,
		int *flagsPtr, int clientID, ClientData streamData, char *name,
		Fs_HandleHeader **ioHandlePtrPtr));
extern ReturnStatus FspdevPseudoStreamOpen _ARGS_((
		Fspdev_ServerIOHandle *pdevHandlePtr, int flags, int clientID, 
		Proc_PID procID, int userID));
extern ReturnStatus FspdevPseudoStreamRead _ARGS_((Fs_Stream *streamPtr, 
		Fs_IOParam *readPtr, Sync_RemoteWaiter *waitPtr, 
		Fs_IOReply *replyPtr));
extern ReturnStatus FspdevPseudoStreamWrite _ARGS_((Fs_Stream *streamPtr, 
		Fs_IOParam *writePtr, Sync_RemoteWaiter *waitPtr, 
		Fs_IOReply *replyPtr));
extern ReturnStatus FspdevPseudoStreamIOControl _ARGS_((Fs_Stream *streamPtr, 
		Fs_IOCParam *ioctlPtr, Fs_IOReply *replyPtr));
extern ReturnStatus FspdevPseudoStreamSelect _ARGS_((Fs_HandleHeader *hdrPtr,
		Sync_RemoteWaiter *waitPtr, int *readPtr, int *writePtr,
		int *exceptPtr));
extern ReturnStatus FspdevPseudoStreamGetIOAttr _ARGS_((Fs_FileID *fileIDPtr, 
		int clientID,   Fs_Attributes *attrPtr));
extern ReturnStatus FspdevPseudoStreamSetIOAttr _ARGS_((Fs_FileID *fileIDPtr,
		Fs_Attributes *attrPtr, int flags));
extern ReturnStatus FspdevPseudoStreamMigClose _ARGS_((Fs_HandleHeader *hdrPtr,
		int flags));
extern ReturnStatus FspdevPseudoStreamMigrate _ARGS_((Fsio_MigInfo *migInfoPtr,
		int dstClientID, int *flagsPtr, int *offsetPtr, int *sizePtr,
		Address *dataPtr));
extern ReturnStatus FspdevPseudoStreamMigOpen _ARGS_((Fsio_MigInfo *migInfoPtr,
		int size, ClientData data, Fs_HandleHeader **hdrPtrPtr));
#ifdef SOSP91
extern ReturnStatus FspdevPseudoStreamClose _ARGS_((Fs_Stream *streamPtr, 
		int clientID, Proc_PID procID, int flags, int size,
		ClientData data, int *offsetPtr, int *rwFlagsPtr));
#else
extern ReturnStatus FspdevPseudoStreamClose _ARGS_((Fs_Stream *streamPtr, 
		int clientID, Proc_PID procID, int flags, int size,
		ClientData data));
#endif
extern void FspdevPseudoStreamCloseInt _ARGS_((	
		Fspdev_ServerIOHandle *pdevHandlePtr));

extern Fspdev_ServerIOHandle *FspdevServerStreamCreate _ARGS_((
		Fs_FileID *ioFileIDPtr, char *name, Boolean naming));

/*
 * Remote pseudo-device streams
 */
extern ReturnStatus FspdevRmtPseudoStreamIoOpen _ARGS_((Fs_FileID *ioFileIDPtr,
		int *flagsPtr, int clientID, ClientData streamData, char *name,
		Fs_HandleHeader **ioHandlePtrPtr));
extern Fs_HandleHeader *FspdevRmtPseudoStreamVerify _ARGS_((
		Fs_FileID *fileIDPtr, int clientID, int *domainTypePtr));
extern ReturnStatus FspdevRmtPseudoStreamMigrate _ARGS_((
		Fsio_MigInfo *migInfoPtr, int dstClientID, int *flagsPtr,
		int *offsetPtr, int *sizePtr, Address *dataPtr));
/*
 * Local and remote pseudo-device streams to pseudo-file-systems
 */
extern ReturnStatus FspdevPfsStreamIoOpen _ARGS_((Fs_FileID *ioFileIDPtr, 
		int *flagsPtr, int clientID, ClientData streamData, char *name,
		Fs_HandleHeader **ioHandlePtrPtr));
extern ReturnStatus FspdevRmtPfsStreamIoOpen _ARGS_((Fs_FileID *ioFileIDPtr, 
		int *flagsPtr, int clientID, ClientData streamData, char *name,
		Fs_HandleHeader **ioHandlePtrPtr));
/*
 * Naming Stream routines.
 */
extern ReturnStatus FspdevPfsExport _ARGS_((Fs_HandleHeader *hdrPtr,
		int clientID, register Fs_FileID *ioFileIDPtr, 
		int *dataSizePtr, ClientData *clientDataPtr));
extern ReturnStatus FspdevPfsNamingIoOpen _ARGS_((Fs_FileID *ioFileIDPtr,
		int *flagsPtr, int clientID, ClientData streamData, char *name,
		Fs_HandleHeader **ioHandlePtrPtr));
/*
 * Pseudo-file-system naming routines.
 */
extern ReturnStatus FspdevPfsOpen _ARGS_((Fs_HandleHeader *prefixHandle,
		char *relativeName, Address argsPtr, Address resultsPtr, 
		Fs_RedirectInfo **newNameInfoPtrPtr));
extern ReturnStatus FspdevPfsGetAttrPath _ARGS_((Fs_HandleHeader *prefixHandle,
		char *relativeName, Address argsPtr, Address resultsPtr, 
		Fs_RedirectInfo **newNameInfoPtrPtr));
extern ReturnStatus FspdevPfsSetAttrPath _ARGS_((Fs_HandleHeader *prefixHandle,
		char *relativeName, Address argsPtr, Address resultsPtr,
		Fs_RedirectInfo **newNameInfoPtrPtr));
extern ReturnStatus FspdevPfsMakeDir _ARGS_((Fs_HandleHeader *prefixHandle,
		char *relativeName, Address argsPtr, Address resultsPtr,
		Fs_RedirectInfo **newNameInfoPtrPtr));
extern ReturnStatus FspdevPfsMakeDevice _ARGS_((Fs_HandleHeader *prefixHandle, 
		char *relativeName, Address argsPtr, Address resultsPtr,
		Fs_RedirectInfo **newNameInfoPtrPtr));
extern ReturnStatus FspdevPfsRemove _ARGS_((Fs_HandleHeader *prefixHandle,
		char *relativeName, Address argsPtr, Address resultsPtr,
		Fs_RedirectInfo **newNameInfoPtrPtr));
extern ReturnStatus FspdevPfsRemoveDir _ARGS_((Fs_HandleHeader *prefixHandle,
		char *relativeName, Address argsPtr, Address resultsPtr, 
		Fs_RedirectInfo **newNameInfoPtrPtr));
extern ReturnStatus FspdevPfsRename _ARGS_((Fs_HandleHeader *prefixHandle1,
		char *relativeName1, Fs_HandleHeader *prefixHandle2,
		char *relativeName2, Fs_LookupArgs *lookupArgsPtr, 
		Fs_RedirectInfo **newNameInfoPtrPtr, Boolean *name1ErrorPtr));
extern ReturnStatus FspdevPfsHardLink _ARGS_((Fs_HandleHeader *prefixHandle1,
		char *relativeName1, Fs_HandleHeader *prefixHandle2,
		char *relativeName2, Fs_LookupArgs *lookupArgsPtr, 
		Fs_RedirectInfo **newNameInfoPtrPtr, Boolean *name1ErrorPtr));
extern ReturnStatus FspdevPfs2Path _ARGS_((Pdev_Op operation, 
		Fs_HandleHeader *prefixHandle1, char *relativeName1, 
		Fs_HandleHeader *prefixHandle2, char *relativeName2,
		Fs_LookupArgs *lookupArgsPtr, 
		Fs_RedirectInfo **newNameInfoPtrPtr, Boolean *name1ErrorPtr));
extern ReturnStatus FspdevPseudoStream2Path _ARGS_((
		Fspdev_ServerIOHandle *pdevHandlePtr, Pfs_Request *requestPtr,
		Fs_2PathData *dataPtr, Boolean *name1ErrorPtr, 
		Fs_RedirectInfo **newNameInfoPtrPtr));

extern ReturnStatus FspdevPseudoStreamLookup _ARGS_((
		Fspdev_ServerIOHandle *pdevHandlePtr, Pfs_Request *requestPtr,
		int argSize, Address argsPtr, int *resultsSizePtr, 
		Address resultsPtr, Fs_RedirectInfo **newNameInfoPtrPtr));

/*
 * Pseudo-file-system routines given an open file.
 */
extern ReturnStatus FspdevPseudoGetAttr _ARGS_((Fs_FileID *fileIDPtr, 
		int clientID, Fs_Attributes *attrPtr));
extern ReturnStatus FspdevPseudoSetAttr _ARGS_((Fs_FileID *fileIDPtr, 
		Fs_Attributes *attrPtr, Fs_UserIDs *idPtr, int flags));

extern Boolean FspdevPdevServerOK _ARGS_((Fspdev_ServerIOHandle *pdevHandlePtr));

extern ReturnStatus FspdevPassStream _ARGS_((Fs_FileID *ioFileIDPtr,
		int *flagsPtr, int clientID, ClientData streamData, 
		char *name, Fs_HandleHeader **ioHandlePtrPtr));


extern int FspdevPfsOpenConnection _ARGS_((
		Fspdev_ServerIOHandle *namingPdevHandlePtr, 
		Fs_FileID *srvrFileIDPtr, Fs_OpenResults *openResultsPtr));

#endif _FSPDEVINT
