/* 
 * fsPdevSetup.c --
 *
 *	Open/Close/Migration routines for pseudo-devices.
 *	
 *	There are three kinds of streams involved in the implementation,
 *	a "control" stream that is returned to the server when it first
 *	opens the pseudo-device.  When a client opens the pseudo-device
 *	two streams are created and looked together.  The "client" stream
 *	is returned to the client process, and the "server" stream is
 *	passed to the server process via the control stream.
 *
 * Copyright 1987, 1988 Regents of the University of California
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

#include "sprite.h"
#include "fs.h"
#include "fsInt.h"
#include "fsOpTable.h"
#include "fsStream.h"
#include "fsClient.h"
#include "fsFile.h"
#include "fsDisk.h"
#include "fsMigrate.h"
#include "fsLock.h"
#include "proc.h"
#include "rpc.h"
#include "swapBuffer.h"
#include "fsPdev.h"

/*
 *----------------------------------------------------------------------------
 *
 * FsPseudoDevSrvOpen --
 *
 *	Early open time processing, this is called on a fileserver
 *	when setting up state for a call to the CltOpen routines on
 *	the client host.  For pseudo-device server processes, which
 *	are indicated by the FS_PDEV_MASTER flag, check that no other
 *	server exists.  For all other processes, which are referred to
 *	as "clients", make sure that a server process exists and
 *	generate a new ioFileID for the connection between the client
 *	and the server.
 *
 * Results:
 *	For server processes, SUCCESS if it is now the server,
 *	FS_FILE_BUSY if there already exists a server process.  For
 *	clients, SUCCESS if there is a server or the parameters
 *	indicate this is only for get/set attributes, DEV_OFFLINE if
 *	there is no server. 
 *
 * Side effects:
 *	Save the hostID of the calling process if
 *	it is to be the server for the pseudo-device.
 *
 *----------------------------------------------------------------------------
 *
 */
ReturnStatus
FsPseudoDevSrvOpen(handlePtr, openArgsPtr, openResultsPtr)
     register FsLocalFileIOHandle *handlePtr;	/* A handle from FsLocalLookup.
					 * Should be LOCKED upon entry,
					 * unlocked upon exit. */
     FsOpenArgs		*openArgsPtr;	/* Standard open arguments */
     FsOpenResults	*openResultsPtr;/* For returning ioFileID, streamID,
					 * and FsDeviceState */
{
    register	ReturnStatus status = SUCCESS;
    Fs_FileID	ioFileID;
    register	PdevControlIOHandle *ctrlHandlePtr;
    register	Fs_Stream *streamPtr;
    register	FsPdevState *pdevStatePtr;

    /*
     * The control I/O handle is identified by the fileID of the pseudo-device
     * file with type CONTROL, and with the decriptor version number
     * xor'ed into the minor number to avoid conflict when you delete the
     * pdev file and recreate one with the same file number (minor field).
     */
    ioFileID = handlePtr->hdr.fileID;
    ioFileID.type = FS_CONTROL_STREAM;
    ioFileID.serverID = rpc_SpriteID;
    ioFileID.major = handlePtr->hdr.fileID.major;
    ioFileID.minor = handlePtr->hdr.fileID.minor ^
		    (handlePtr->descPtr->version << 16);
    ctrlHandlePtr = FsControlHandleInit(&ioFileID, handlePtr->hdr.name);

    if (openArgsPtr->useFlags & FS_PDEV_MASTER) {
	/*
	 * When a server opens we ensure there is only one.
	 */
	if (ctrlHandlePtr->serverID != NIL) {
	    status = FS_FILE_BUSY;
	} else {
	    /*
	     * Note which host is running the pseudo-device server.
	     */
	    ctrlHandlePtr->serverID = openArgsPtr->clientID;
	    /*
	     * Note our hostID is still in the hdr.serverID field of the
	     * control handle being returned to the opening process. This is
	     * used when closing the control stream to get back to us
	     * so we can clear the serverID field here.  We also set up
	     * a shadow stream here, which has us as the server so
	     * recovery and closing work right.
	     */
	    openResultsPtr->ioFileID = ioFileID;
	    openResultsPtr->streamData = (ClientData)NIL;
	    openResultsPtr->dataSize = 0;
	    streamPtr = FsStreamNewClient(rpc_SpriteID, openArgsPtr->clientID,
				    (FsHandleHeader *)ctrlHandlePtr,
				    openArgsPtr->useFlags, handlePtr->hdr.name);
	    openResultsPtr->streamID = streamPtr->hdr.fileID;
	    FsHandleRelease(streamPtr, TRUE);
	}
    } else {
	if (openArgsPtr->useFlags == 0) {
	    /*
	     * Set up for get/set attributes.  We point the client
	     * at the name of the pseudo-device if it is not active,
	     * otherwise we point it at the control stream handle that
	     * has the current access and modify times.
	     */
	    if (ctrlHandlePtr->serverID == NIL) {
		openResultsPtr->ioFileID = handlePtr->hdr.fileID;
	    } else {
		openResultsPtr->ioFileID = ctrlHandlePtr->rmt.hdr.fileID;
		if (openArgsPtr->clientID != ctrlHandlePtr->serverID) {
		    /*
		     * The requesting client is different than the pdev
		     * server host.  Unfortunately the serverID in the
		     * control handle is us, the file server.  We have
		     * to hack the fileID so the client makes the RPC to
		     * the pdev server.  This relies on a parallel hack
		     * in FsRemoteGetIOAttr to fix up the serverID by
		     * using the Fs_Attributes.serverID, which is us,
		     * so that the correct control handle is found.
		     */
		    openResultsPtr->ioFileID.type = FS_RMT_CONTROL_STREAM;
		    openResultsPtr->ioFileID.serverID = ctrlHandlePtr->serverID;
		}
	    }
	} else if (ctrlHandlePtr->serverID == NIL) {
	    /*
	     * No server process.
	     */
	    status = DEV_OFFLINE;
	} else {
	    /*
	     * The server exists.  Create a new I/O handle for the client.
	     * The major and minor numbers are generated from the fileID
	     * of the pseudo-device name (to avoid conflict with other
	     * pseudo-devices) and a clone seed (to avoid conflict with
	     * other clients of this pseudo-device).
	     */
	    if (ctrlHandlePtr->serverID == openArgsPtr->clientID) {
		openResultsPtr->ioFileID.type = FS_LCL_PSEUDO_STREAM;
	    } else {
		openResultsPtr->ioFileID.type = FS_RMT_PSEUDO_STREAM;
	    }
	    openResultsPtr->ioFileID.serverID = ctrlHandlePtr->serverID;
	    openResultsPtr->ioFileID.major =
				(handlePtr->hdr.fileID.serverID << 16) |
				 handlePtr->hdr.fileID.major;
	    ctrlHandlePtr->seed++;
	    openResultsPtr->ioFileID.minor =
				((handlePtr->descPtr->version << 24) ^
				 (handlePtr->hdr.fileID.minor << 12)) |
				 ctrlHandlePtr->seed;
	    /*
	     * Return the control stream file ID so it can be found again
	     * later when setting up the client's stream and the
	     * corresponding server stream.  The procID and uid fields are
	     * extra here, but will be used later if the client is remote.
	     */
	    pdevStatePtr = mnew(FsPdevState);
	    pdevStatePtr->ctrlFileID = ctrlHandlePtr->rmt.hdr.fileID;
	    pdevStatePtr->procID = (Proc_PID)NIL;
	    pdevStatePtr->uid = NIL;
	    openResultsPtr->streamData = (ClientData)pdevStatePtr ;
	    openResultsPtr->dataSize = sizeof(FsPdevState);
	    /*
	     * Create a streamID for the opening process.  No shadow
	     * stream is kept here.  Instead, the streamID is returned to
	     * the pdev server who sets up the shadow stream.
	     */
	    FsStreamNewID(ctrlHandlePtr->serverID, &openResultsPtr->streamID);
	    pdevStatePtr->streamID = openResultsPtr->streamID;
	}
    }
    FsHandleRelease(ctrlHandlePtr, TRUE);
    FsHandleUnlock(handlePtr);
    return(status);
}

/*
 *----------------------------------------------------------------------
 *
 * FsPseudoStreamCltOpen --
 *
 *	This is called from Fs_Open, or from the RPC stub if the client
 *	is remote, to complete setup of a client's
 *	stream to the pseudo-device.  The server is running on this
 *	host.  This routine creates a trivial client I/O handle
 *	that references the server's I/O handle that has the main
 *	state for the connection to the server.  FsServerStreamCreate
 *	is then called to set up the server's I/O handle, and the control
 *	stream is used to pass a server stream to the server.  Finally
 *	an open transaction is made with the server process
 *	to see if it will accept the client.
 * 
 * Results:
 *	SUCCESS, unless the server process has died recently, or the
 *	server rejects the open.
 *
 * Side effects:
 *	Creates the client's I/O handle.  Calls FsServerStreamCreate
 *	which sets up the servers corresponding I/O handle.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
FsPseudoStreamCltOpen(ioFileIDPtr, flagsPtr, clientID, streamData, name,
	ioHandlePtrPtr)
    register Fs_FileID	*ioFileIDPtr;	/* I/O fileID */
    int			*flagsPtr;	/* FS_READ | FS_WRITE ... */
    int			clientID;	/* Host doing the open */
    ClientData		streamData;	/* Pointer to FsPdevState. */
    char		*name;		/* File name for error msgs */
    FsHandleHeader	**ioHandlePtrPtr;/* Return - a locked handle set up for
					 * I/O to a pseudo device, or NIL */
{
    ReturnStatus		status;
    Boolean			foundStream;
    register PdevClientIOHandle	*cltHandlePtr;
    register PdevControlIOHandle *ctrlHandlePtr;
    register FsPdevState	*pdevStatePtr;
    Fs_Stream			*cltStreamPtr;
    Fs_Stream			*srvStreamPtr;
    PdevNotify			*notifyPtr;
    Proc_ControlBlock		*procPtr;
    Proc_PID 			procID;
    int				uid;

    /*
     * PATCH - should allow execution via pseudo-devices some day.
     *	Need to fix Fs_CheckSetID, and the sticky segment stuff.
     */
    if (*flagsPtr & FS_EXECUTE) {
	return(FS_WRONG_TYPE);
    }

    pdevStatePtr = (FsPdevState *)streamData;
    ctrlHandlePtr = FsHandleFetchType(PdevControlIOHandle,
				    &pdevStatePtr->ctrlFileID);
    /*
     * If there is no server present the creation of the stream
     * can't succeed.  This case arises when the pseudo-device
     * master goes away between FsPseudoDevSrvOpen and this call.
     */
    if ((ctrlHandlePtr == (PdevControlIOHandle *)NIL) ||
	(ctrlHandlePtr->serverID == NIL)) {
	status = DEV_OFFLINE;
	goto exit;
    }

    if (ctrlHandlePtr->rmt.hdr.fileID.serverID != rpc_SpriteID) {
	/*
	 * Extract the seed from the minor field (see the SrvOpen routine).
	 * This done in case of recovery when we'll need to reset the
	 * seed kept on the file server.
	 */
	ctrlHandlePtr->seed = ioFileIDPtr->minor & 0x0FFF;
    }

    cltHandlePtr = FsPdevConnect(ctrlHandlePtr, ioFileIDPtr, clientID, 0);
    if (cltHandlePtr == (PdevClientIOHandle *)NIL) {
	goto exit;
    }
    /*
     * Put the client on its own stream list.
     */
    cltStreamPtr = FsStreamAddClient(&pdevStatePtr->streamID, clientID,
		(FsHandleHeader *)cltHandlePtr, *flagsPtr, name,
		(Boolean *)NIL, &foundStream);
    FsHandleRelease(cltStreamPtr, TRUE);
    FsHandleUnlock(cltHandlePtr);
    /*
     * Set up a stream for the server process.  This will be picked
     * up by FsControlRead and converted to a user-level streamID.
     */
    srvStreamPtr = FsStreamNewClient(rpc_SpriteID, rpc_SpriteID,
			    (FsHandleHeader *)cltHandlePtr->pdevHandlePtr,
			    FS_READ|FS_USER, name);
    notifyPtr = mnew(PdevNotify);
    notifyPtr->streamPtr = srvStreamPtr;
    List_InitElement((List_Links *)notifyPtr);
    List_Insert((List_Links *)notifyPtr,
		LIST_ATREAR(&ctrlHandlePtr->queueHdr));
    FsHandleUnlock(srvStreamPtr);

    FsFastWaitListNotify(&ctrlHandlePtr->readWaitList);
    FsHandleRelease(ctrlHandlePtr, TRUE);
    ctrlHandlePtr = (PdevControlIOHandle *)NIL;
    /*
     * Now that the request response stream is set up we do
     * our first transaction with the server process to see if it
     * will accept the open.  We unlock the handle and rely on the
     * per-connection monitor lock instead.  This is important because a
     * buggy pseudo-device server could be ignoring this connection
     * request indefinitely, and leaving handles locked for long periods
     * clogs up handle scavenging, and potentially recovery callbacks too.
     */
    if (clientID == rpc_SpriteID) {
	procPtr = Proc_GetEffectiveProc();
	procID = procPtr->processID;
	uid = procPtr->effectiveUserID;
    } else {
	procID = pdevStatePtr->procID;
	uid = pdevStatePtr->uid;
    }
    status = FsPseudoStreamOpen(cltHandlePtr->pdevHandlePtr, *flagsPtr,
				clientID, procID, uid);
    if (status == SUCCESS) {
	*ioHandlePtrPtr = (FsHandleHeader *)cltHandlePtr;
    } else {
	/*
	 * Clean up client side, we assume server closes its half.
	 */
	FsHandleInvalidate((FsHandleHeader *) cltHandlePtr);
	FsHandleRemove(cltHandlePtr);
	(void)FsStreamClientClose(&cltStreamPtr->clientList, clientID);
	if (!foundStream) {
	    /*
	     * The client's stream wasn't already around from being installed
	     * in Fs_Open, so we nuke the shadow stream we've created.
	     */
	    cltStreamPtr = FsHandleFetchType(Fs_Stream,
					     &cltStreamPtr->hdr.fileID);
	    FsStreamDispose(cltStreamPtr);
	}
    }
exit:
    if (ctrlHandlePtr != (PdevControlIOHandle *)NIL) {
	FsHandleRelease(ctrlHandlePtr, TRUE);
    }
    free((Address)streamData);
    return(status);
}

/*
 *----------------------------------------------------------------------
 *
 * FsPdevConnect --
 *
 *	This sets up a pseduo-device connection.  This is called from
 *	the PseudoStreamCltOpen routine with ordinary pseudo-devices,
 *	and from FsPfsCltOpen to set up the naming connection to a
 *	pseudo-filesystem server, and during an IOC_PFS_OPEN by a
 *	pseudo-filesystem server to set up a connection to a client.
 *	The case of the naming stream is distinguished by the last
 *	parameter.  The state of this needs to be marked specially
 *	so proper clean up can be made later.
 * 
 * Results:
 *	A pointer to a PdevClientIOHandle that references a PdevServerIOHandle.
 *	The client handle is returned locked, but the server handle it
 *	references is not locked.
 *
 * Side effects:
 *	Creates the client's I/O handle.  Calls FsServerStreamCreate
 *	which sets up the servers corresponding I/O handle.
 *	This changes the ioFileIDPtr->type from FS_LCL_PSEUDO_STREAM to
 *	FS_SERVER_STREAM.
 *
 *----------------------------------------------------------------------
 */

PdevClientIOHandle *
FsPdevConnect(ctrlHandlePtr, ioFileIDPtr, clientID, naming)
    PdevControlIOHandle *ctrlHandlePtr;	/* Control stream handle */
    register Fs_FileID	*ioFileIDPtr;	/* I/O fileID */
    int			clientID;	/* Host ID of client-side */
    Boolean		naming;		/* TRUE if called from FsPfsCltOpen
					 * to set up the naming stream */
{
    Boolean			found;
    FsHandleHeader		*hdrPtr;
    register PdevClientIOHandle	*cltHandlePtr;

    found = FsHandleInstall(ioFileIDPtr, sizeof(PdevClientIOHandle),
		ctrlHandlePtr->rmt.hdr.name, &hdrPtr);
    cltHandlePtr = (PdevClientIOHandle *)hdrPtr;
    if (found) {
	if ((cltHandlePtr->pdevHandlePtr != (PdevServerIOHandle *)NIL) &&
	    (cltHandlePtr->pdevHandlePtr->clientPID != (unsigned int)NIL)) {
	    printf(
		"FsPdevConnect found client handle\n");
	    printf("Check (and kill) client process %x\n",
		cltHandlePtr->pdevHandlePtr->clientPID);
	}
	/*
	 * Invalidate this lingering handle.  The client process is hung
	 * or suspended and hasn't closed its end of the pdev connection.
	 */
	FsHandleInvalidate((FsHandleHeader *)cltHandlePtr);
	FsHandleRelease(cltHandlePtr, TRUE);

	found = FsHandleInstall(ioFileIDPtr, sizeof(PdevClientIOHandle),
			ctrlHandlePtr->rmt.hdr.name, &hdrPtr);
	cltHandlePtr = (PdevClientIOHandle *)hdrPtr;
	if (found) {
	    panic( "FsPdevConnect handle still there\n");
	}
    }
    /*
     * Set up the connection state and hook the client handle to it.
     */
    cltHandlePtr->pdevHandlePtr = FsServerStreamCreate(ioFileIDPtr,
				    ctrlHandlePtr->rmt.hdr.name, naming);
    if (cltHandlePtr->pdevHandlePtr == (PdevServerIOHandle *)NIL) {
	FsHandleRemove(cltHandlePtr);
	return((PdevClientIOHandle *)NIL);
    }
    cltHandlePtr->pdevHandlePtr->ctrlHandlePtr = ctrlHandlePtr;
    /*
     * Set up the client list in case the client is remote.
     */
    List_Init(&cltHandlePtr->clientList);
    (void)FsIOClientOpen(&cltHandlePtr->clientList, clientID, 0, FALSE);
    /*
     * Grab an extra reference to the server's handle so the
     * server close routine can remove the handle and it won't
     * go away until the client also closes.
     */
    FsHandleUnlock(cltHandlePtr->pdevHandlePtr);
    (void)FsHandleDup((FsHandleHeader *)cltHandlePtr->pdevHandlePtr);
    FsHandleUnlock(cltHandlePtr->pdevHandlePtr);

    return(cltHandlePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * FsRmtPseudoStreamCltOpen --
 *
 *	Complete a remote client's stream to a pseudo-device.
 *	The client is on a different host than the server process.  This
 *	makes an RPC to the pseudo-device server's host to invoke
 *	FsPseudoStreamCltOpen, which sets up the pdev connection.
 *	This host only keeps a FsRemoteIOHandle that implicitly references
 *	the pdev connection on the pdev server's host.
 * 
 * Results:
 *	SUCCESS unless the server process has died recently, then DEV_OFFLINE.
 *
 * Side effects:
 *	RPC to the server's host to invoke the regular setup routines.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
FsRmtPseudoStreamCltOpen(ioFileIDPtr, flagsPtr, clientID, streamData, name,
	ioHandlePtrPtr)
    register Fs_FileID	*ioFileIDPtr;	/* I/O fileID */
    int			*flagsPtr;	/* FS_READ | FS_WRITE ... */
    int			clientID;	/* IGNORED (== rpc_SpriteID) */
    ClientData		streamData;	/* NIL for us. */
    char		*name;		/* File name for error msgs */
    FsHandleHeader	**ioHandlePtrPtr;/* Return - a locked handle set up for
					 * I/O to a pseudo device, or NIL */
{
    register ReturnStatus status;
    register Proc_ControlBlock *procPtr;
    register FsPdevState *pdevStatePtr = (FsPdevState *)streamData;

    /*
     * Use RPC to invoke FsPseudoStreamCltOpen which sets up the connection.
     */
    procPtr = Proc_GetEffectiveProc();
    pdevStatePtr->procID = procPtr->processID;
    pdevStatePtr->uid = procPtr->effectiveUserID;
    ioFileIDPtr->type = FS_LCL_PSEUDO_STREAM;
    status = FsDeviceRemoteOpen(ioFileIDPtr, *flagsPtr,	sizeof(FsPdevState),
				(ClientData)pdevStatePtr);
    if (status == SUCCESS) {
	ioFileIDPtr->type = FS_RMT_PSEUDO_STREAM;
	FsRemoteIOHandleInit(ioFileIDPtr, *flagsPtr, name, ioHandlePtrPtr);
    }
    free((Address)pdevStatePtr);
    return(status);
}

/*
 *----------------------------------------------------------------------
 *
 * FsPseudoStreamClose --
 *
 *	Close a pseudo stream that's been used by a client to talk to a server.
 *	This issues a close message to the server and then tears down the
 *	state used to implement the pseudo stream connection.
 *
 * Results:
 *	SUCCESS.
 *
 * Side effects:
 *	Other than the request-response to the server, this releases the
 *	pseudo stream's reference to the handle.  This may also have
 *	to contact a remote host to clean up references there, too.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
FsPseudoStreamClose(streamPtr, clientID, procID, flags, size, data)
    Fs_Stream		*streamPtr;	/* Client pseudo-stream to close */
    int			clientID;	/* HostID of client closing */
    Proc_PID		procID;		/* ID of closing process, IGNORED */
    int			flags;		/* IGNORED */
    int			size;		/* Should be zero */
    ClientData		data;		/* IGNORED */
{
    register PdevClientIOHandle *cltHandlePtr =
	    (PdevClientIOHandle *)streamPtr->ioHandlePtr;
    Boolean cache = FALSE;

    DBG_PRINT( ("Client closing pdev %x,%x\n", 
		cltHandlePtr->hdr.fileID.major,
		cltHandlePtr->hdr.fileID.minor) );

    if (!FsIOClientClose(&cltHandlePtr->clientList, clientID, 0, &cache)) {
	/*
	 * Invalid client trying to close.
	 */
	printf( "FsPseudoStreamClose: client %d not found\n",
	    clientID);
	FsHandleUnlock(cltHandlePtr);
	return(GEN_INVALID_ARG);
    } else if (!List_IsEmpty(&cltHandlePtr->clientList)) {
	/*
	 * Still clients out there.
	 */
	FsHandleUnlock(cltHandlePtr);
    } else {
	/*
	 * No clients remaining so we can close down the connection.
	 * Notify the server that a client has gone away.  Then we get rid
	 * of our reference to the server's handle and nuke our own.
	 * Note we unlock the client handle before the request response
	 * in case the server process is buggy and hangs us.
	 */
	FsHandleUnlock(cltHandlePtr);
	FsPseudoStreamCloseInt(cltHandlePtr->pdevHandlePtr);
	FsHandleRelease(cltHandlePtr->pdevHandlePtr, FALSE);
	FsHandleRelease(cltHandlePtr, FALSE);
	FsHandleRemove(cltHandlePtr);
    }
    return(SUCCESS);
}

/*
 *----------------------------------------------------------------------
 *
 * FsPseudoStreamRelease --
 *
 *	Called to release a reference on a pseudo stream.  However, there
 *	is always only one refernece on the handle so we do nothing.
 *
 * Results:
 *	SUCCESS.
 *
 * Side effects:
 *	Release the I/O handle.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
FsPseudoStreamRelease(hdrPtr, flags)
    FsHandleHeader *hdrPtr;	/* File being encapsulated */
    int flags;			/* Use flags from the stream */
{
    panic( "FsPseudoStreamRelease called\n");

    return(SUCCESS);
}

/*
 *----------------------------------------------------------------------
 *
 * FsPseudoStreamMigrate --
 *
 *	Migrate a pseudo-stream client.
 *
 *	This takes care of transfering references from one client to the other.
 *	A useful side-effect of this routine is	to properly set the type in
 *	the ioFileID, either FS_LCL_PSEUDO_STREAM or FS_RMT_PSEUDO_STREAM.
 *	In the latter case FsRmtPseudoStreamMigrate is called to do all
 *	the work.
 *
 * Results:
 *	An error status if the I/O handle can't be set-up.
 *	Otherwise SUCCESS is returned, *flagsPtr may have the FS_RMT_SHARED
 *	bit set, and *sizePtr and *dataPtr are set to reference FsPdevState.
 *
 * Side effects:
 *	Sets the correct stream type on the ioFileID.
 *	Shifts client references from the srcClient to the destClient.
 *	Set up and return FsPdevState for use by the MigEnd routine.
 
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
FsPseudoStreamMigrate(migInfoPtr, dstClientID, flagsPtr, offsetPtr, sizePtr,
		      dataPtr)
    FsMigInfo	*migInfoPtr;	/* Migration state */
    int		dstClientID;	/* ID of target client */
    int		*flagsPtr;	/* In/Out Stream usage flags */
    int		*offsetPtr;	/* Return - new stream offset */
    int		*sizePtr;	/* Return - sizeof(FsPdevState) */
    Address	*dataPtr;	/* Return - pointer to FsPdevState */
{
    PdevClientIOHandle			*cltHandlePtr;
    Boolean				closeSrcClient;

    if (migInfoPtr->ioFileID.serverID != rpc_SpriteID) {
	/*
	 * The device was local, which is why we were called, but is
	 * now remote.
	 */
	migInfoPtr->ioFileID.type = FS_RMT_PSEUDO_STREAM;
	return(FsRmtPseudoStreamMigrate(migInfoPtr, dstClientID, flagsPtr,
					offsetPtr, sizePtr, dataPtr));
    }
    migInfoPtr->ioFileID.type = FS_LCL_PSEUDO_STREAM;
    cltHandlePtr = FsHandleFetchType(PdevClientIOHandle, &migInfoPtr->ioFileID);
    if (cltHandlePtr == (PdevClientIOHandle *)NIL) {
	panic( "FsPseudoStreamMigrate, no client handle <%d,%x,%x>\n",
		migInfoPtr->ioFileID.serverID,
		migInfoPtr->ioFileID.major, migInfoPtr->ioFileID.minor);
	return(FAILURE);
    }
    DBG_PRINT( ("Migrating pdev %x,%x, ref %d.\n", 
		cltHandlePtr->hdr.fileID.major,
		cltHandlePtr->hdr.fileID.minor,
		cltHandlePtr->hdr.refCount) );
    /*
     * At the stream level, add the new client to the set of clients
     * for the stream, and check for any cross-network stream sharing.
     */
    FsStreamMigClient(migInfoPtr, dstClientID, (FsHandleHeader *)cltHandlePtr,
			&closeSrcClient);

    /*
     * Move the client at the I/O handle level.  The flags are used
     * by FsIOClient{Open,Close} and are different for pdevs than
     * other files -- namely, the flags are set to 0 before calls to these
     * routines.  The only flag we have to make sure to pass is
     * whether it's a new stream, since this is used by FsIOClientMigrate
     * itself.
     */
    FsIOClientMigrate(&cltHandlePtr->clientList, migInfoPtr->srcClientID,
		      dstClientID, migInfoPtr->flags & FS_NEW_STREAM,
		      closeSrcClient);

    *sizePtr = 0;
    *dataPtr = (Address)NIL;
    *flagsPtr = migInfoPtr->flags;
    *offsetPtr = migInfoPtr->offset;
    /*
     * We don't need this reference on the I/O handle; there is no change.
     */
    FsHandleRelease(cltHandlePtr, TRUE);
    return(SUCCESS);
}

/*
 *----------------------------------------------------------------------
 *
 * FsRmtPseudoStreamMigrate --
 *
 *	Migrate a pseudo-stream client.
 *	This takes care of transfering references from one client to the other.
 *	A useful side-effect of this routine is	to properly set the type in
 *	the ioFileID, either FS_LCL_PSEUDO_STREAM or FS_RMT_PSEUDO_STREAM.
 *	In the former case FsPseudoStreamMigrate is called to do all the work.
 *
 * Results:
 *	An error status if the I/O handle can't be set-up.
 *	Otherwise SUCCESS is returned, *flagsPtr may have the FS_RMT_SHARED
 *	bit set, and *sizePtr and *dataPtr are set to reference FsPdevState.
 *
 * Side effects:
 *	Sets the correct stream type on the ioFileID.
 *	Shifts client references from the srcClient to the destClient.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
FsRmtPseudoStreamMigrate(migInfoPtr, dstClientID, flagsPtr, offsetPtr,
			 sizePtr, dataPtr)
    FsMigInfo	*migInfoPtr;	/* Migration state */
    int		dstClientID;	/* ID of target client */
    int		*flagsPtr;	/* In/Out Stream usage flags */
    int		*offsetPtr;	/* Return - the new stream offset */
    int		*sizePtr;	/* Return - 0 */
    Address	*dataPtr;	/* Return - NIL */
{
    register ReturnStatus		status;

    if (migInfoPtr->ioFileID.serverID == rpc_SpriteID) {
	/*
	 * The device was remote, which is why we were called, but is now local.
	 */
	migInfoPtr->ioFileID.type = FS_LCL_PSEUDO_STREAM;
	return(FsPseudoStreamMigrate(migInfoPtr, dstClientID, flagsPtr,
				     offsetPtr, sizePtr, dataPtr));
    }
    migInfoPtr->ioFileID.type = FS_RMT_PSEUDO_STREAM;
    status = FsNotifyOfMigration(migInfoPtr, flagsPtr, offsetPtr,
				0, (Address)NIL);
    DBG_PRINT( ("Migrating remote pdev %x,%x.\n", 
		migInfoPtr->ioFileID.major,
		migInfoPtr->ioFileID.minor) );
    if (status != SUCCESS) {
	printf( "FsRmtDeviceMigrate, server error <%x>\n",
	    status);
    } else {
	*dataPtr = (Address)NIL;
	*sizePtr = 0;
    }
    return(status);
}

/*
 *----------------------------------------------------------------------
 *
 * FsPseudoStreamMigEnd --
 *
 *	Complete setup of a pdev client I/O handle after migrating a stream
 *	to the I/O server of the pseudo-device connection (the host running
 *	the user-level server process).  FsPseudoStreamMigrate has done
 *	the work of shifting use counts at the stream and I/O handle level.
 *	This routine fills in the stream's ioHandlePtr, but doens't adjust
 *	the low-level reference count on the I/O handle (like other MigEnd
 *	procedures) because the reference count isn't used the same way.
 *	With pseudo-device connections, there is always only one refCount
 *	on the client handle, but there may be entries in the clientList
 *	to reflect remote clients.
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
FsPseudoStreamMigEnd(migInfoPtr, size, data, hdrPtrPtr)
    FsMigInfo	*migInfoPtr;	/* Migration state */
    int		size;		/* Zero */
    ClientData	data;		/* NIL */
    FsHandleHeader **hdrPtrPtr;	/* Return - handle for the file */
{
    register PdevClientIOHandle *cltHandlePtr;

    cltHandlePtr = FsHandleFetchType(PdevClientIOHandle,
				     &migInfoPtr->ioFileID);
    if (cltHandlePtr == (PdevClientIOHandle *)NIL) {
	panic( "FsPseudoStreamMigEnd, no handle.\n");
	return(FAILURE);
    } else {
	/*
	 * Release this reference so the client handle always has
	 * just one reference.  Instead of refcounts, an empty
	 * client list indicates there are no more clients.
	 */
	FsHandleRelease(cltHandlePtr, TRUE);
	*hdrPtrPtr = (FsHandleHeader *)cltHandlePtr;
	return(SUCCESS);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FsRmtPseudoStreamVerify --
 *
 *	Verify that the remote client is known for the pdev, and return
 *	a locked pointer to the client I/O handle.
 *
 * Results:
 *	A pointer to the client I/O handle, or NIL if
 *	the client is bad.
 *
 * Side effects:
 *	The handle is returned locked and with its refCount incremented.
 *	It should be released with FsHandleRelease.
 *
 *----------------------------------------------------------------------
 */

FsHandleHeader *
FsRmtPseudoStreamVerify(fileIDPtr, clientID, domainTypePtr)
    Fs_FileID	*fileIDPtr;	/* Client's I/O file ID */
    int		clientID;	/* Host ID of the client */
    int		*domainTypePtr;	/* Return - FS_PSEUDO_DOMAIN */
{
    register PdevClientIOHandle	*cltHandlePtr;
    register FsClientInfo	*clientPtr;
    Boolean			found = FALSE;

    if (fileIDPtr->type > 0 && fileIDPtr->type < FS_NUM_STREAM_TYPES) {
	fileIDPtr->type = fsRmtToLclType[fileIDPtr->type];
    }
    if (fileIDPtr->type != FS_LCL_PSEUDO_STREAM &&
	fileIDPtr->type != FS_LCL_PFS_STREAM) {
	printf( "FsRmtPseudoStreamVerify, bad type <%d>\n",
	    fileIDPtr->type);
	return((FsHandleHeader *)NIL);
    }
    cltHandlePtr = FsHandleFetchType(PdevClientIOHandle, fileIDPtr);
    if (cltHandlePtr != (PdevClientIOHandle *)NIL) {
	LIST_FORALL(&cltHandlePtr->clientList, (List_Links *) clientPtr) {
	    if (clientPtr->clientID == clientID) {
		found = TRUE;
		break;
	    }
	}
	if (!found) {
	    FsHandleRelease(cltHandlePtr, TRUE);
	    cltHandlePtr = (PdevClientIOHandle *)NIL;
	}
    }
    if (!found) {
	printf(
	    "FsRmtPseudoDeviceVerify, client %d not known for %s <%x,%x>\n",
	    clientID, FsFileTypeToString(fileIDPtr->type),
	    fileIDPtr->major, fileIDPtr->minor);
    }
    if (domainTypePtr != (int *)NIL) {
	*domainTypePtr = FS_PSEUDO_DOMAIN;
    }
    return((FsHandleHeader *)cltHandlePtr);
}
