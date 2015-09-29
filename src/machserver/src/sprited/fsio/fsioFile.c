/* 
 * fsioFile.c --
 *
 *	Routines for operations on files.  A file handle is identified
 *	by using the <major> field of the Fs_FileID for the domain index,
 *	and the <minor> field for the file number.
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
 */

#ifndef lint
static char rcsid[] = "$Header: /user5/kupfer/spriteserver/src/sprited/fsio/RCS/fsioFile.c,v 1.3 92/06/04 14:20:43 kupfer Exp $ SPRITE (Berkeley)";
#endif not lint


#include <sprite.h>
#include <ckalloc.h>
#include <fs.h>
#include <fsMach.h>
#include <fsutil.h>
#include <fsconsist.h>
#include <fsio.h>
#include <fsioFile.h>
#include <fslcl.h>
#include <fsNameOps.h>
#include <fscache.h>
#include <fsprefix.h>
#include <fsioLock.h>
#include <fsdm.h>
#include <fsrmt.h>
#include <fsStat.h>
#include <vm.h>
#include <rpc.h>
#include <recov.h>

#ifdef SOSP91
#include <sospRecord.h>
#endif

#ifdef SPRITED_LOCALDISK
static void IncVersionNumber _ARGS_((Fsio_FileIOHandle	*handlePtr));
#endif


/*
 *----------------------------------------------------------------------
 *
 * Fsio_LocalFileHandleInit --
 *
 *	Initialize a handle for a local file from its descriptor on disk.
 *
 * Results:
 *	An error code from the read of the file descriptor off disk.
 *
 * Side effects:
 *	Create and install a handle for the file.  It is returned locked
 *	and with its reference count incremented if SUCCESS is returned.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Fsio_LocalFileHandleInit(fileIDPtr, name, descPtr, cantBlock, newHandlePtrPtr)
    Fs_FileID	*fileIDPtr;
    char	*name;
    Fsdm_FileDescriptor *descPtr;
    Boolean	cantBlock;
    Fsio_FileIOHandle	**newHandlePtrPtr;
{
#ifdef SPRITED_LOCALDISK
    register ReturnStatus status;
    register Fsio_FileIOHandle *handlePtr;
    register Fsdm_Domain *domainPtr;
    Boolean allocated = FALSE;
#endif
    register Boolean found;

    found = Fsutil_HandleInstall(fileIDPtr, sizeof(Fsio_FileIOHandle), name,
		    cantBlock, (Fs_HandleHeader **)newHandlePtrPtr);
    if (found) {
	if ((*newHandlePtrPtr) == (Fsio_FileIOHandle *) NIL) {
	    return FS_WOULD_BLOCK;
	}
	/*
	 * All set.
	 */
	if ((*newHandlePtrPtr)->descPtr == (Fsdm_FileDescriptor *)NIL) {
	    panic("Fsio_LocalFileHandleInit, found handle with no descPtr\n");
	}
	return(SUCCESS);
    }
#ifndef SPRITED_LOCALDISK
    panic("Fsio_LocalFileHandleInit: trying to access local domain.\n");
    descPtr = descPtr;		/* lint */
    return FAILURE;		/* lint */
#else
    status = SUCCESS;
    handlePtr = *newHandlePtrPtr;
    domainPtr = Fsdm_DomainFetch(fileIDPtr->major, FALSE);
    if (domainPtr == (Fsdm_Domain *)NIL) {
	Fsutil_HandleRelease(handlePtr, FALSE);
	Fsutil_HandleRemove(handlePtr);
	return(FS_DOMAIN_UNAVAILABLE);
    }
    if (descPtr == (Fsdm_FileDescriptor *) NIL) { 
	/*
	 * Get a hold of the disk file descriptor.
	 */
	allocated = TRUE;
	descPtr = (Fsdm_FileDescriptor *)ckalloc(sizeof(Fsdm_FileDescriptor));
	status = Fsdm_FileDescFetch(domainPtr, fileIDPtr->minor, descPtr);
	if (status == FS_FILE_NOT_FOUND) {
	    status = FS_FILE_REMOVED;
	}
	if ((status != SUCCESS) && (status != FS_FILE_REMOVED)) {
	    printf( 
	    "Fsio_LocalFileHandleInit: Fsdm_FileDescFetch of %d failed 0x%x\n",
				fileIDPtr->minor, status);
	}
    } 
    if ((status == SUCCESS) && !(descPtr->flags & FSDM_FD_ALLOC)) {
	status = FS_FILE_REMOVED;
    }
    if (status == SUCCESS) { 
	Fscache_Attributes attr;

	handlePtr->descPtr = descPtr;
	handlePtr->flags = 0;
	/*
	 * The use counts are updated when an I/O stream is opened on the file
	 */
	handlePtr->use.ref = 0;
	handlePtr->use.write = 0;
	handlePtr->use.exec = 0;
	handlePtr->segPtr = (Vm_Segment *)NIL;

	/*
	 * Copy attributes that are cached in the handle.
	 */
	attr.firstByte = descPtr->firstByte;
	attr.lastByte = descPtr->lastByte;
	attr.accessTime = descPtr->accessTime;
	attr.createTime = descPtr->createTime;
	attr.modifyTime = descPtr->dataModifyTime;
	attr.userType = descPtr->userType;
	attr.permissions = descPtr->permissions;
	attr.uid = descPtr->uid;
	attr.gid = descPtr->gid;

	Fscache_FileInfoInit(&handlePtr->cacheInfo, 
		(Fs_HandleHeader *)handlePtr,
		descPtr->version, TRUE, &attr, domainPtr->backendPtr);

	Fsconsist_Init(&handlePtr->consist, (Fs_HandleHeader *)handlePtr);
	Fsio_LockInit(&handlePtr->lock);
	Fscache_ReadAheadInit(&handlePtr->readAhead);

	handlePtr->segPtr = (Vm_Segment *)NIL;
    }
    if (status != SUCCESS) {
	Fsutil_HandleRelease(handlePtr, FALSE);
	Fsutil_HandleRemove(handlePtr);
	if (allocated) {
	    ckfree((Address)descPtr);
	 }
	*newHandlePtrPtr = (Fsio_FileIOHandle *)NIL;
    } else {
	if (descPtr->fileType == FS_DIRECTORY) {
	    fs_Stats.object.directory++;
	} else {
	    fs_Stats.object.files++;
	}
	*newHandlePtrPtr = handlePtr;
    }
    Fsdm_DomainRelease(fileIDPtr->major);
    return(status);
#endif /* SPRITED_LOCALDISK */
}

/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileSyncLockCleanup --
 *
 *	This takes care of the dynamically allocated Sync_Lock's that
 *	are embedded in a Fsio_FileIOHandle.  This routine is
 *	called when the file handle is being removed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The locking statistics for this handle are combined with the
 *	summary statistics for the lock types in the handle
 *
 *----------------------------------------------------------------------
 */
void
Fsio_FileSyncLockCleanup(handlePtr)
    Fsio_FileIOHandle *handlePtr;
{
    Fsconsist_SyncLockCleanup(&handlePtr->consist);
    Fscache_InfoSyncLockCleanup(&handlePtr->cacheInfo);
    Fscache_ReadAheadSyncLockCleanup(&handlePtr->readAhead);
}

/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileNameOpen --
 *
 *	This is called in two cases after name lookup on the server.
 *	The first is when a client is opening the file from Fs_Open.
 *	The second is when a lookup is done when getting/setting the
 *	attributes of the files.  In the open case this routine has
 *	to set up Fsio_FileState that the client will use to complete
 *	the setup of its stream, and create a server-side stream.
 *	The handle should be locked upon entry, it remains locked upon return.
 *
 * Results:
 *	SUCCESS.
 *
 * Side effects:
 *	The major side effect of this routine is to invoke cache consistency
 *	actions by other clients.  This also does conflict checking, like
 *	preventing writing if a file is being executed.  Lastly, a shadow
 *	stream is created here on the server to support migration.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Fsio_FileNameOpen(handlePtr, openArgsPtr, openResultsPtr)
     register Fsio_FileIOHandle *handlePtr;	/* A handle from FslclLookup.
					 * Should be LOCKED upon entry,
					 * Returned UNLOCKED. */
      Fs_OpenArgs		*openArgsPtr;	/* Standard open arguments */
     Fs_OpenResults	*openResultsPtr;/* For returning ioFileID, streamID,
					 * and Fsio_FileState */
{
#ifndef SPRITED_LOCALDISK
    panic("Fsio_FileNameOpen called.\n");
#ifdef lint
    openResultsPtr = openResultsPtr;
    openArgsPtr = openArgsPtr;
    handlePtr = handlePtr;
#endif
    return FAILURE;
#else
    Fsio_FileState *fileStatePtr;
    ReturnStatus status;
    register useFlags = openArgsPtr->useFlags;
    register clientID = openArgsPtr->clientID;
    register Fs_Stream *streamPtr;
#ifdef SOSP91
    int realID = -1;
    if (openResultsPtr->dataSize == sizeof(Fs_OpenArgsSOSP)) {
	realID = ((Fs_OpenArgsSOSP *) openArgsPtr)->realID;
    }
#endif

    if ((useFlags & FS_WRITE) &&
	(handlePtr->descPtr->fileType == FS_DIRECTORY)) {
	status = FS_IS_DIRECTORY;
	goto exit;
    }
    if (handlePtr->descPtr->fileType == FS_DIRECTORY) {
	/*
	 * Strip off execute permission that was used to allow access to
	 * a directory.
	 */
	useFlags &= ~FS_EXECUTE;
    }
    /*
     * Check against writing and executing at the same time.  Fs_Open already
     * checks that useFlags doesn't contain both execute and write bits.
     */
    if (((useFlags & FS_EXECUTE) && (handlePtr->use.write > 0)) ||
	((useFlags & (FS_WRITE|FS_CREATE)) && (handlePtr->use.exec > 0))) {
	status = FS_FILE_BUSY;
	goto exit;
    }
    /*
     * Add in read permission when executing a file so Fs_Read doesn't
     * foil page-ins later.
     */
    if ((useFlags & FS_EXECUTE) && (handlePtr->descPtr->fileType == FS_FILE)) {
	useFlags |= FS_READ;
    }
    /*
     * Set up the ioFileIDPtr so our caller can set/get attributes.
     */
    openResultsPtr->ioFileID = handlePtr->hdr.fileID;
    if (clientID != rpc_SpriteID) {
	openResultsPtr->ioFileID.type = FSIO_RMT_FILE_STREAM;
    }
    if (useFlags == 0) { 
	/*
	 * Only being called from the get/set attributes code.
	 * Setting up the ioFileID is all that is needed.
	 */
	status = SUCCESS;
    } else {
	/*
	 * Called during an open.  Update the summary use counts while
	 * we still have the handle locked.  Then unlock the handle and
	 * do consistency call-backs.  The handle is unlocked to allow
	 * servicing of RPCs which are side effects
	 * of the consistency requests (i.e. write-backs).
	 */
	handlePtr->use.ref++;
	if (useFlags & FS_WRITE) {
	    handlePtr->use.write++;
	    IncVersionNumber(handlePtr);
	}
	if (useFlags & FS_EXECUTE) {
	    handlePtr->use.exec++;
	}
	Fsutil_HandleUnlock(handlePtr);
	fileStatePtr = mnew(Fsio_FileState);
	status = Fsconsist_FileConsistency(handlePtr, clientID, useFlags,
		    &fileStatePtr->cacheable, &fileStatePtr->openTimeStamp);
	if (status == SUCCESS) {
	    /*
	     * Copy cached attributes into the returned file state.
	     */
	    Fscache_GetCachedAttr(&handlePtr->cacheInfo, &fileStatePtr->version,
			&fileStatePtr->attr);
	    /*
	     * Return new usage flags to the client.  This lets us strip
	     * off the execute use flag (above, for directories) so
	     * the client doesn't have to worry about it.
	     */
	    fileStatePtr->newUseFlags = useFlags;
	    openResultsPtr->streamData = (ClientData)fileStatePtr;
	    openResultsPtr->dataSize = sizeof(Fsio_FileState);
#ifdef SOSP91
	if (handlePtr->descPtr->fileType == FS_DIRECTORY) {
	    fileStatePtr->newUseFlags |= FS_DIR;
	}
#endif SOSP91

	    /*
	     * Now set up a shadow stream on here on the server so we
	     * can support shared offset even after migration.
	     * Note: prefix handles get opened, but the stream is not used,
	     * could dispose stream in FslclExport.
	     */

	    streamPtr = Fsio_StreamCreate(rpc_SpriteID, clientID,
		(Fs_HandleHeader *)handlePtr, useFlags, handlePtr->hdr.name);
	    openResultsPtr->streamID = streamPtr->hdr.fileID;

#ifdef SOSP91
	    {
		int numWriters;
		int numReaders;
		(void) Fsconsist_NumClients(&handlePtr->consist, 
		    &numReaders, &numWriters);
		SOSP_ADD_OPEN_TRACE(openArgsPtr->clientID, 
			openArgsPtr->migClientID, openResultsPtr->ioFileID,
			openResultsPtr->streamID, openArgsPtr->id.user,
			realID, openArgsPtr->useFlags, numReaders, numWriters,
			fileStatePtr->attr.createTime,
			fileStatePtr->attr.lastByte + 1,
			fileStatePtr->attr.modifyTime,
			handlePtr->descPtr->fileType, fileStatePtr->cacheable);
	    }
#endif

	    Fsutil_HandleRelease(streamPtr, TRUE);
	    return(SUCCESS);
	} else {
	    /*
	     * Consistency call-backs failed because the last writer
	     * could not write back its copy of the file. We garbage
	     * collect the client to retreat to a known bookkeeping point.
	     */
	    int ref, write, exec;
	    printf("Consistency failed %x on <%d,%d>\n", status,
		handlePtr->hdr.fileID.major, handlePtr->hdr.fileID.minor);
	    Fsutil_HandleLock(handlePtr);
	    Fsconsist_Kill(&handlePtr->consist, clientID,
			  &ref, &write, &exec);
	    handlePtr->use.ref   -= ref;
	    handlePtr->use.write -= write;
	    handlePtr->use.exec  -= exec;
	    if ((handlePtr->use.ref < 0) || (handlePtr->use.write < 0) ||
		    (handlePtr->use.exec < 0)) {
		panic("Fsio_FileNameOpen: client %d ref %d write %d exec %d\n",
		    clientID, handlePtr->use.ref,
		    handlePtr->use.write, handlePtr->use.exec);
	    }
	    ckfree((Address)fileStatePtr);
	}
    }
exit:
    Fsutil_HandleUnlock(handlePtr);
    return(status);
#endif /* SPRITED_LOCALDISK */
}

/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileReopen --
 *
 *      Reopen a file for use by a remote client. State is maintained in the
 *      handle's client list about this open and whether or not the client
 *      is caching.
 *
 * Results:
 *	A failure code if the client was caching dirty blocks but lost
 *	the race to re-open its file.  (i.e. another client already opened
 *	for writing.)
 *
 * Side effects:
 *	The client use state for the client is brought into agreement
 *	with what the client tells us.  We do cache consistency too.
 *	
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
Fsio_FileReopen(hdrPtr, clientID, inData, outSizePtr, outDataPtr)
    Fs_HandleHeader	*hdrPtr;	/* IGNORED here on the server */
    int			clientID;	/* Client doing the reopen */
    ClientData		inData;		/* Fsio_FileReopenParams */
    int			*outSizePtr;	/* Size of returned data */
    ClientData		*outDataPtr;	/* Returned data */
{
#ifndef SPRITED_LOCALDISK
    panic("Fsio_FileReopen called.\n");
    return FAILURE;		/* lint */
#else
    register Fsio_FileReopenParams *reopenParamsPtr; /* Parameters from RPC */
    register Fsio_FileState	*fileStatePtr;	/* Results for RPC */
    Fsio_FileIOHandle	    	*handlePtr;	/* Local handle for file */
    register ReturnStatus	status = SUCCESS; /* General return code */
    Fsdm_Domain			*domainPtr;

    *outDataPtr = (ClientData) NIL;
    *outSizePtr = 0;
    /*
     * Do initial setup for the reopen.  We make sure that the disk
     * for the file is still around first, mark the client
     * as doing recovery, and fetch a local handle for the file.
     * NAME note: we have no name for the file after a re-open.
     */
    reopenParamsPtr = (Fsio_FileReopenParams *) inData;
    domainPtr = Fsdm_DomainFetch(reopenParamsPtr->fileID.major, FALSE);
    if (domainPtr == (Fsdm_Domain *)NIL) {
	return(FS_DOMAIN_UNAVAILABLE);
    }
    status = Fsio_LocalFileHandleInit(&reopenParamsPtr->fileID, (char *)NIL,
	(Fsdm_FileDescriptor *) NIL, FALSE, &handlePtr);
    if (status != SUCCESS) {
	goto reopenReturn;
    }
    /*
     * See if the client can still cache its dirty blocks.
     */
    if (reopenParamsPtr->flags & FSIO_HAVE_BLOCKS) {
	status = Fscache_CheckVersion(&handlePtr->cacheInfo,
				     reopenParamsPtr->version, clientID);
	if (status != SUCCESS) {
	    Fsutil_HandleRelease(handlePtr, TRUE);
	    goto reopenReturn;
	}
    }
    /*
     * Update global use counts and version number.
     */
    Fsconsist_ReopenClient(handlePtr, clientID, reopenParamsPtr->use,
			reopenParamsPtr->flags & FSIO_HAVE_BLOCKS);
    if (reopenParamsPtr->use.write > 0) {
	IncVersionNumber(handlePtr);
    }
    /*
     * Now unlock the handle and do cache consistency call-backs.
     */
    fileStatePtr = mnew(Fsio_FileState);
    fileStatePtr->cacheable = reopenParamsPtr->flags & FSIO_HAVE_BLOCKS;
    Fsutil_HandleUnlock(handlePtr);
    status = Fsconsist_ReopenConsistency(handlePtr, clientID, reopenParamsPtr->use,
		reopenParamsPtr->flags & FS_SWAP,
		&fileStatePtr->cacheable, &fileStatePtr->openTimeStamp);
    if (status != SUCCESS) {
	/*
	 * Consistency call-backs failed, probably due to disk-full.
	 * We kill the client here as it will invalidate its handle
	 * after this re-open fails.
	 */
	int ref, write, exec;
	Fsutil_HandleLock(handlePtr);
	Fsconsist_Kill(&handlePtr->consist, clientID, &ref, &write, &exec);
	handlePtr->use.ref   -= ref;
	handlePtr->use.write -= write;
	handlePtr->use.exec  -= exec;
	if ((handlePtr->use.ref < 0) || (handlePtr->use.write < 0) ||
		(handlePtr->use.exec < 0)) {
	    panic("Fsio_FileReopen: client %d ref %d write %d exec %d\n",
		clientID, handlePtr->use.ref,
		handlePtr->use.write, handlePtr->use.exec);
	}
	Fsutil_HandleUnlock(handlePtr);
	ckfree((Address)fileStatePtr);
    } else {
	/*
	 * Successful re-open here on the server. Copy cached attributes
	 * into the returned file state.
	 */
	Fscache_GetCachedAttr(&handlePtr->cacheInfo, &fileStatePtr->version,
		    &fileStatePtr->attr);
	fileStatePtr->newUseFlags = 0;		/* Not used in re-open */
	*outDataPtr = (ClientData) fileStatePtr;
	*outSizePtr = sizeof(Fsio_FileState);
    }
    Fsutil_HandleRelease(handlePtr, FALSE);
reopenReturn:
    Fsdm_DomainRelease(reopenParamsPtr->fileID.major);
    return(status);
#endif /* SPRITED_LOCALDISK */
}

/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileIoOpen --
 *
 *      Set up a stream for a local disk file.  This is called from Fs_Open to
 *	complete the opening of a stream.  By this time any cache consistency
 *	actions have already been taken, and local use counts have been
 *	incremented by Fsio_FileNameOpen.
 *
 * Results:
 *	SUCCESS, unless there was an error installing the handle.
 *
 * Side effects:
 *	Installs the handle for the file.  This increments its refererence
 *	count (different than the use count).
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
Fsio_FileIoOpen(ioFileIDPtr, flagsPtr, clientID, streamData, name, ioHandlePtrPtr)
    Fs_FileID		*ioFileIDPtr;	/* I/O fileID from the name server */
    int			*flagsPtr;	/* Return only.  The server returns
					 * a modified useFlags in Fsio_FileState */
    int			clientID;	/* IGNORED */
    ClientData		streamData;	/* Fsio_FileState. */
    char		*name;		/* File name for error msgs */
    Fs_HandleHeader	**ioHandlePtrPtr;/* Return - a handle set up for
					 * I/O to a file, NIL if failure. */
{
    register ReturnStatus	status;

    status = Fsio_LocalFileHandleInit(ioFileIDPtr, name, 
		(Fsdm_FileDescriptor *) NIL, FALSE, 
		(Fsio_FileIOHandle **)ioHandlePtrPtr);
    if (status == SUCCESS) {
	/*
	 * Return the new useFlags from the server.  It has stripped off
	 * execute permission for directories.
	 */
	*flagsPtr = ( (Fsio_FileState *)streamData )->newUseFlags;
	Fsutil_HandleUnlock(*ioHandlePtrPtr);
    }
    ckfree((Address)streamData);
    return(status);
}

/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileClose --
 *
 *	Close time processing for local files.  We need to remove ourselves
 *	from the list of clients of the file, decrement use counts, and
 *	handle pending deletes.  This returns either with one reference
 *	to the handle released, or with the handle removed entirely.
 *
 * Results:
 *	SUCCESS, FS_FILE_REMOVED, or an error code from the disk operation
 *	on the file descriptor.
 *
 * Side effects:
 *	Attributes cached on clients are propogated to the local handle.
 *	Use counts in the client list are decremented.  The handle's
 *	use counts are decremented.  If the file has been deleted, then
 *	the file descriptor is so marked, other clients are told of
 *	the delete, and the handle is removed entirely.  Otherwise,
 *	a reference on the handle is released.
 *
 *----------------------------------------------------------------------
 */

#ifndef SOSP91
ReturnStatus
Fsio_FileClose(streamPtr, clientID, procID, flags, dataSize, closeData)
#else
ReturnStatus
Fsio_FileClose(streamPtr, clientID, procID, flags, dataSize, closeData,
    offsetPtr, rwFlagsPtr)
#endif
    Fs_Stream		*streamPtr;	/* Stream to regular file */
    int			clientID;	/* Host ID of closer */
    Proc_PID		procID;		/* Process ID of closer */
    int			flags;		/* Flags from the stream being closed */
    int			dataSize;	/* Size of closeData */
    ClientData		closeData;	/* Ref. to Fscache_Attributes */
#ifdef SOSP91
    int			*offsetPtr;
    int			*rwFlagsPtr;
#endif
{
#ifndef SPRITED_LOCALDISK
    panic("Fsio_FileClose called.\n");
#ifdef lint
    closeData = closeData;
    dataSize = dataSize;
    flags = flags;
    procID = procID;
    clientID = clientID;
    streamPtr = streamPtr;
#endif /* lint */
    return FAILURE;		/* lint */
#else /* SPRITED_LOCALDISK */
    register Fsio_FileIOHandle *handlePtr =
	    (Fsio_FileIOHandle *)streamPtr->ioHandlePtr;
    ReturnStatus		status;
    Boolean			wasCached = TRUE;
#ifdef SOSP91
    Boolean			didCloseConsist;
#endif /* SOSP91 */

    /*
     * Update the client state to reflect the close by the client.
     */

    /*
     * This code is to track down a problem with clients sending bad
     * data.
     */
    if (flags & FS_EXECUTE) {
	List_Links *clientList = &(handlePtr->consist.clientList);
	Fsconsist_ClientInfo *clientPtr;
	LIST_FORALL(clientList, (List_Links *)clientPtr) {
	    if (clientPtr->clientID == clientID) {
	       if (clientPtr->use.exec==0) {
		   printf("***ERROR***:Client %d: bad close on %s\n",
			   clientID, Fsutil_HandleName(handlePtr));
		  return(FAILURE);
	       }
	    }
        }
    }

#ifdef SOSP91
    didCloseConsist = Fsconsist_Close(&handlePtr->consist, clientID, flags,
	    &wasCached);
    if (!didCloseConsist) {
#else /* SOSP91 */
    if (!Fsconsist_Close(&handlePtr->consist, clientID, flags, &wasCached)) {
#endif /* SOSP91 */
	printf("Fsio_FileClose, client %d pid %x unknown for file <%d,%d>\n",
		  clientID, procID, handlePtr->hdr.fileID.major,
		  handlePtr->hdr.fileID.minor);
	Fsutil_HandleUnlock(handlePtr);
	return(FS_STALE_HANDLE);
    }
    if (wasCached && dataSize != 0) {
	/*
	 * Update the server's attributes from ones cached on the client.
	 */
	Fscache_UpdateAttrFromClient(clientID, &handlePtr->cacheInfo,
				(Fscache_Attributes *)closeData);
	(void)Fsdm_UpdateDescAttr(handlePtr, &handlePtr->cacheInfo.attr, -1);
    }

    Fsio_LockClose(&handlePtr->lock, &streamPtr->hdr.fileID);

    /*
     * Update use counts and handle pending deletions.
     */
    status = Fsio_FileCloseInt(handlePtr, 1, (flags & FS_WRITE) != 0,
				     (flags & FS_EXECUTE) != 0,
				     clientID, TRUE);
#ifdef SOSP91
    {
	int offset;
	int rwFlags;
	if ((offsetPtr != (int *) NIL) && !(streamPtr->flags & FS_RMT_SHARED)) {
	    offset = *offsetPtr;
	    rwFlags = *rwFlagsPtr;
	} else {
	    offset = streamPtr->offset;
	    rwFlags = (streamPtr->hdr.flags & FSUTIL_RW_FLAGS) >> 8;
	}
	SOSP_ADD_CLOSE_TRACE(streamPtr->hdr.fileID, offset, 
	    handlePtr->cacheInfo.attr.lastByte + 1, streamPtr->flags,
	    rwFlags, handlePtr->use.ref, didCloseConsist);
    }
#endif
    if (status == FS_FILE_REMOVED) {
	if (clientID == rpc_SpriteID) {
	    status = SUCCESS;
	}
    } else {
	status = SUCCESS;
	Fsutil_HandleRelease(handlePtr, TRUE);
    }

    return(status);
#endif /* SPRITED_LOCALDISK */
}

/*
 * ----------------------------------------------------------------------------
 *
 * Fsio_FileCloseInt --
 *
 *	Close a file, handling pending deletions.
 *	This is called from the regular close routine, from
 *	the file client-kill cleanup routine, and from the
 *	lookup routine that deletes file names.
 *
 * Results:
 *	SUCCESS or FS_FILE_REMOVED.
 *
 * Side effects:
 *	Adjusts use counts and does pending deletions.  If the file is
 *	deleted the handle can not be used anymore.  Otherwise it
 *	is left locked.
 *
 * ----------------------------------------------------------------------------
 *
 */
ReturnStatus
Fsio_FileCloseInt(handlePtr, ref, write, exec, clientID, callback)
    Fsio_FileIOHandle *handlePtr;	/* File to clean up */
    int ref;				/* Number of uses to remove */
    int write;				/* Number of writers to remove */
    int exec;				/* Number of executers to remove */
    int clientID;			/* Closing, or crashed, client */
    Boolean callback;			/* TRUE if we should call back to
					 * the client and tell it about
					 * the deletion. */
{
#ifndef SPRITED_LOCALDISK
    panic("Fsio_FileCloseInt called.\n");
#ifdef lint
    callback = callback;
    clientID = clientID;
    exec = exec;
    write = write;
    ref = ref;
    handlePtr = handlePtr;
#endif /* lint */
    return FAILURE;		/* lint */
#else /* SPRITED_LOCALDISK */
    register ReturnStatus status;
    /*
     * Update the global/summary use counts for the file.
     */
    handlePtr->use.ref -= ref;
    handlePtr->use.write -= write;
    handlePtr->use.exec -= exec;
    if (handlePtr->use.ref < 0 || handlePtr->use.write < 0 ||
	handlePtr->use.exec < 0) {
	panic("Fsio_FileCloseInt <%d,%d> use %d, write %d, exec %d\n",
	    handlePtr->hdr.fileID.major, handlePtr->hdr.fileID.minor,
	    handlePtr->use.ref, handlePtr->use.write, handlePtr->use.exec);
    }

    /*
     * Handle pending deletes
     *	0. Make sure it isn't being deleted already.
     *	1. Scan the client list and call-back to the last writer if
     *		it is not the client doing the close.  The handle gets
     *		temporarily unlocked during the callback, so we are
     *		are careful to ensure only one process does the delete.
     *	2. Mark the disk descriptor as deleted,
     *	3. Remove the file handle.
     *	4. Return FS_FILE_REMOVED so clients know to nuke their cache.
     */
    if ((handlePtr->use.ref == 0) &&
	(handlePtr->flags & FSIO_FILE_NAME_DELETED)) {
	if ((handlePtr->flags & FSIO_FILE_DESC_DELETED) == 0) {
	    handlePtr->flags |= FSIO_FILE_DESC_DELETED;
	    if (handlePtr->descPtr->fileType == FS_DIRECTORY) {
		fs_Stats.object.directory--;
	    } else {
		fs_Stats.object.files--;
	    }
	    if (callback) {
		Fsconsist_ClientRemoveCallback(&handlePtr->consist, clientID);
	    }
#ifdef SOSP91
	    SOSP_ADD_DELETE_DESC_TRACE(handlePtr->hdr.fileID,
		    handlePtr->cacheInfo.attr.modifyTime,
		    handlePtr->cacheInfo.attr.createTime,
		    handlePtr->cacheInfo.attr.lastByte + 1);
#endif

	    (void)Fslcl_DeleteFileDesc(handlePtr);
	    Fsio_FileSyncLockCleanup(handlePtr);
	    if (callback) {
		Fsutil_HandleRelease(handlePtr, FALSE);
	    }
	    Fsutil_HandleRemove(handlePtr);
	} else {
	    /*
	     * The following printf is a mousetrap to verify that the 
	     * bug where Proc_ServerProcs leave files locked was fixed.
	     * Remove it once we are positive the bug is fixed. JHH 11/5/90
	     */
	    printf(
    "Fsio_FileCloseInt: almost returned FS_FILE_REMOVED w/ handle locked\n");
	    Fsutil_HandleUnlock(handlePtr);
	}
	status = FS_FILE_REMOVED;
    } else {
	status = SUCCESS;
    }
    return(status);
#endif /* SPRITED_LOCALDISK */
}

/*
 * ----------------------------------------------------------------------------
 *
 * Fsio_FileClientKill --
 *
 *	Called when a client is assumed down.  This cleans up the
 *	cache consistency state associated with the client, and reflects
 *	these changes in uses (i.e. less writers) in the handle's global
 *	use counts.
 *	
 *
 * Results:
 *	SUCCESS.
 *
 * Side effects:
 *	Removes the client list entry for the client and adjusts the
 *	use counts on the file.  This has to remove or unlock the handle.
 *
 * ----------------------------------------------------------------------------
 *
 */
void
Fsio_FileClientKill(hdrPtr, clientID)
    Fs_HandleHeader	*hdrPtr;	/* File to clean up */
    int			clientID;	/* Host assumed down */
{
    Fsio_FileIOHandle *handlePtr = (Fsio_FileIOHandle *)hdrPtr;
    int refs, writes, execs;
    register ReturnStatus status;

    Fsconsist_IOClientKill(&handlePtr->consist.clientList, clientID,
		    &refs, &writes, &execs);
    Fsio_LockClientKill(&handlePtr->lock, clientID);

    status = Fsio_FileCloseInt(handlePtr, refs, writes, execs, clientID, FALSE);
    if (status != FS_FILE_REMOVED) {
	Fsutil_HandleUnlock(handlePtr);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * Fsio_FileScavenge --
 *
 *	Called periodically to see if this handle is still needed.
 *	
 *
 * Results:
 *	TRUE if it removed the handle.
 *
 * Side effects:
 *	Removes the handle if their are no references to it and no
 *	blocks in the cache for it.  Otherwise it unlocks the handle
 *	before returning.
 *
 * ----------------------------------------------------------------------------
 *
 */
/*ARGSUSED*/
Boolean
Fsio_FileScavenge(hdrPtr)
    Fs_HandleHeader	*hdrPtr;	/* File to clean up */
{
    register Fsio_FileIOHandle *handlePtr = (Fsio_FileIOHandle *)hdrPtr;
    register Boolean noUsers;

    /*
     * We can reclaim the handle if the following holds.
     *  0. The descriptor is not dirty.
     *	1. There are no active users of the file.
     *  2. The file is not undergoing deletion
     *		(The deletion will remove the handle soon)
     *  3. There are no remote clients of the file.  In particular,
     *		the last writer might not be active, but we can't
     *		nuke the handle until after it writes back.
     */
    noUsers = ((handlePtr->descPtr->flags & FSDM_FD_DIRTY) == 0) &&
               (handlePtr->use.ref == 0) &&
	     ((handlePtr->flags & (FSIO_FILE_DESC_DELETED|
				   FSIO_FILE_NAME_DELETED)) == 0) &&
#ifdef SOSP91
	      (Fsconsist_NumClients(&handlePtr->consist, (int *) NIL,
		  (int *) NIL) == 0);
#else
	      (Fsconsist_NumClients(&handlePtr->consist) == 0);
#endif
    if (noUsers && Fscache_OkToScavenge(&handlePtr->cacheInfo)) {
	register Boolean isDir;
#ifdef CONSIST_DEBUG
	extern int fsTraceConsistMinor;
	if (fsTraceConsistMinor == handlePtr->hdr.fileID.minor) {
	    printf("Fsio_FileScavenge <%d,%d> nuked, lastwriter %d\n",
		handlePtr->hdr.fileID.major, handlePtr->hdr.fileID.minor,
		handlePtr->consist.lastWriter);
	}
#endif	CONSIST_DEBUG
	/*
	 * Remove handles for files with no users and no blocks in cache.
	 * We tell VM not to cache the segment associated with the file.
	 * The "attempt remove" call unlocks the handle and then frees its
	 * memory if there are no references to it lingering from the name
	 * hash table.
	 */
	Vm_FileChanged(&handlePtr->segPtr);
	isDir = (handlePtr->descPtr->fileType == FS_DIRECTORY);
	if (Fsutil_HandleAttemptRemove(hdrPtr)) {
	    if (isDir) {
		fs_Stats.object.directory--;
		fs_Stats.object.dirFlushed++;
	    } else {
		fs_Stats.object.files--;
	    }
	    return(TRUE);
	} else {
	    return(FALSE);
	}
    } else {
	Fsutil_HandleUnlock(hdrPtr);
	return(FALSE);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * Fsio_FileMigClose --
 *
 *	Initiate migration of a FSIO_LCL_FILE_STREAM.  There is no extra
 *	state needed than already put together by Fsio_EncapStream.  However,
 *	we do release a low-level reference on the handle which is
 *	re-obtained by FsFileDeencap.  Other than that, we leave the
 *	book-keeping alone, waiting to atomically switch references from
 *	one client to the other at de-encapsulation time.
 *	
 *
 * Results:
 *	SUCCESS.
 *
 * Side effects:
 *	Release a reference on the handle header.
 *
 * ----------------------------------------------------------------------------
 *
 */
/*ARGSUSED*/
ReturnStatus
Fsio_FileMigClose(hdrPtr, flags)
    Fs_HandleHeader *hdrPtr;	/* File being encapsulated */
    int flags;			/* Use flags from the stream */
{
    panic( "Fsio_FileMigClose called\n");
    Fsutil_HandleRelease(hdrPtr, FALSE);
    return(SUCCESS);
}


/*
 * ----------------------------------------------------------------------------
 *
 * Fsio_FileMigOpen --
 *
 *	Complete setup of a stream to a local file after migration to the
 *	file server.  Fsio_FileMigrate has done the work of shifting use
 *	counts at the stream and I/O handle level.  This routine has to
 *	increment the low level I/O handle reference count to reflect
 *	the existence of a new stream to the I/O handle.
 *
 * Results:
 *	SUCCESS or FS_FILE_NOT_FOUND if the I/O handle can't be set up.
 *
 * Side effects:
 *	Gains one reference to the I/O handle.  Frees the client data.
 *
 * ----------------------------------------------------------------------------
 *
 */
/*ARGSUSED*/
ReturnStatus
Fsio_FileMigOpen(migInfoPtr, size, data, hdrPtrPtr)
    Fsio_MigInfo	*migInfoPtr;	/* Migration state */
    int		size;		/* sizeof(Fsio_FileState), IGNORED */
    ClientData	data;		/* referenced to Fsio_FileState */
    Fs_HandleHeader **hdrPtrPtr;	/* Return - I/O handle for the file */
{
    register ReturnStatus status;
    register Fsio_FileIOHandle *handlePtr;

    handlePtr = Fsutil_HandleFetchType(Fsio_FileIOHandle,
		&migInfoPtr->ioFileID);
    if (handlePtr == (Fsio_FileIOHandle *)NIL) {
	printf("Fsio_FileMigOpen, file <%d,%d> from client %d not found\n",
	    migInfoPtr->ioFileID.major, migInfoPtr->ioFileID.minor,
	    migInfoPtr->srcClientID);
	status = FS_FILE_NOT_FOUND;
    } else {
	Fsutil_HandleUnlock(handlePtr);
	*hdrPtrPtr = (Fs_HandleHeader *)handlePtr;
	status = SUCCESS;
    }
    ckfree((Address)data);
    return(status);
}

/*
 * ----------------------------------------------------------------------------
 *
 * Fsio_FileMigrate --
 *
 *	This takes care of transfering references from one client to the other.
 *	Three things are done:  cache consistency actions are taken to
 *	reflect the movement of the client, file state is set up for use
 *	on the client in the MigEnd procedure, and cross-network stream
 *	sharing is detected.  A useful side-effect of this routine is
 *	to properly set the type in the ioFileID, either FSIO_LCL_FILE_STREAM
 *	or FSIO_RMT_FILE_STREAM.  In the latter case FsrmtFileMigrate
 *	is called to do all the work.
 *
 * Results:
 *	An error status if the I/O handle can't be set-up or if there
 *	is a cache consistency failure.  Otherwise SUCCESS is returned,
 *	*flagsPtr may have the FS_RMT_SHARED bit set, and *sizePtr
 *	and *dataPtr are set to reference Fsio_FileState.
 *
 * Side effects:
 *	Sets the correct stream type on the ioFileID.
 *	Shifts client references from the srcClient to the destClient.
 *	Set up and return Fsio_FileState for use by the MigEnd routine.
 *
 * ----------------------------------------------------------------------------
 *
 */
/*ARGSUSED*/
ReturnStatus
Fsio_FileMigrate(migInfoPtr, dstClientID, flagsPtr, offsetPtr, sizePtr, dataPtr)
    Fsio_MigInfo	*migInfoPtr;	/* Migration state */
    int		dstClientID;	/* ID of target client */
    int		*flagsPtr;	/* In/Out Stream usage flags */
    int		*offsetPtr;	/* Return - correct stream offset */
    int		*sizePtr;	/* Return - sizeof(Fsio_FileState) */
    Address	*dataPtr;	/* Return - pointer to Fsio_FileState */
{
#ifndef SPRITED_MIGRATION
    panic("Fsio_FileMigrate called.\n");
    return FAILURE;		/* lint */
#else
    register Fsio_FileIOHandle	*handlePtr;
    register Fsio_FileState		*fileStatePtr;
    register ReturnStatus		status;
    Boolean				closeSrcClient;

    if (migInfoPtr->ioFileID.serverID != rpc_SpriteID) {
	/*
	 * The file was local, which is why we were called, but is now remote.
	 */
	migInfoPtr->ioFileID.type = FSIO_RMT_FILE_STREAM;
	return(FsrmtFileMigrate(migInfoPtr, dstClientID, flagsPtr, offsetPtr,
		sizePtr, dataPtr));
    }
    migInfoPtr->ioFileID.type = FSIO_LCL_FILE_STREAM;
    handlePtr = Fsutil_HandleFetchType(Fsio_FileIOHandle, &migInfoPtr->ioFileID);
    if (handlePtr == (Fsio_FileIOHandle *)NIL) {
	panic("Fsio_FileMigrate, no I/O handle");
	status = FS_STALE_HANDLE;
    } else {

	/*
	 * At the stream level, add the new client to the set of clients
	 * for the stream, and check for any cross-network stream sharing.
	 * We only close the orignial client if the stream is unshared,
	 * i.e. there are no references left there.
	 */
	Fsio_StreamMigClient(migInfoPtr, dstClientID,
		(Fs_HandleHeader *)handlePtr, &closeSrcClient);

	/*
	 * Adjust use counts on the I/O handle to reflect any new sharing.
	 */
	Fsio_MigrateUseCounts(migInfoPtr->flags, closeSrcClient,
			      &handlePtr->use);

	/*
	 * Update the client list, and take any required cache consistency
	 * actions. The handle returns unlocked from the consistency routine.
	 */
	fileStatePtr = mnew(Fsio_FileState);
	Fsutil_HandleUnlock(handlePtr);
	status = Fsconsist_MigrateConsistency(handlePtr,
		migInfoPtr->srcClientID,
		dstClientID, migInfoPtr->flags, closeSrcClient,
		&fileStatePtr->cacheable, &fileStatePtr->openTimeStamp);
	if (status == SUCCESS) {
	    Fscache_GetCachedAttr(&handlePtr->cacheInfo, &fileStatePtr->version,
				&fileStatePtr->attr);
	    *sizePtr = sizeof(Fsio_FileState);
	    *dataPtr = (Address)fileStatePtr;
	    *flagsPtr = fileStatePtr->newUseFlags = migInfoPtr->flags;
	    *offsetPtr = migInfoPtr->offset;
	} else {
	    ckfree((Address)fileStatePtr);
	}
	/*
	 * We don't need this reference on the I/O handle, there is no change.
	 */
	Fsutil_HandleRelease(handlePtr, FALSE);
    }
    return(status);
#endif /* SPRITED_MIGRATION */
}

/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileRead --
 *
 *	Read from a file.  This is a thin layer on top of the cache
 *	read routine.
 *
 * Results:
 *	The results of Fscache_Read.
 *
 * Side effects:
 *	None, because Fscache_Read does most everything.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Fsio_FileRead(streamPtr, readPtr, remoteWaitPtr, replyPtr)
    Fs_Stream		*streamPtr;	/* Open stream to the file. */
    Fs_IOParam		*readPtr;	/* Read parameter block. */
    Sync_RemoteWaiter	*remoteWaitPtr;	/* Process info for remote waiting */
    Fs_IOReply		*replyPtr;	/* Signal to return, if any,
					 * plus the amount read. */
{
#ifndef SPRITED_LOCALDISK
    panic("Fsio_FileRead called.\n");
#ifdef lint
    replyPtr = replyPtr;
    remoteWaitPtr = remoteWaitPtr;
    readPtr = readPtr;
    streamPtr = streamPtr;
#endif
    return FAILURE;		/* lint */
#else /* SPRITED_LOCALDISK */
    register Fsio_FileIOHandle *handlePtr =
	    (Fsio_FileIOHandle *)streamPtr->ioHandlePtr;
    register ReturnStatus status;
    int savedOffset = readPtr->offset;
    int savedLength = readPtr->length;
#ifdef SOSP91
    Fsconsist_Info *consistPtr = &handlePtr->consist;
    Fsconsist_ClientInfo	*clientPtr;
    Boolean			maybeShared = FALSE;
    int				numReading, numWriting;

    if ((handlePtr->descPtr != (Fsdm_FileDescriptor *) NIL) &&
	    (handlePtr->descPtr->fileType == FS_FILE)) {
	/*
	 * If this file is marked uncacheable or if we are the server doing
	 * a read and it's marked uncacheable on another client, then record
	 * this read.
	 */
	LIST_FORALL(&consistPtr->clientList, (List_Links *) clientPtr) {
	    if ((clientPtr->clientID == readPtr->reserved) &&
		    (!clientPtr->cached)) {
		maybeShared = TRUE;
		break;
	    } else if ((readPtr->reserved == rpc_SpriteID) &&
		    (!clientPtr->cached)) {
		maybeShared = TRUE;
		break;
	    }
	}
    }
    if (maybeShared) {
	(void) Fsconsist_NumClients(consistPtr, &numReading, &numWriting);
	SOSP_ADD_READ_TRACE(readPtr->reserved, 
		handlePtr->hdr.fileID, streamPtr->hdr.fileID, TRUE,
		readPtr->offset, readPtr->length, numReading, numWriting);

    }
#endif SOSP91

    status = Fscache_Read(&handlePtr->cacheInfo, readPtr->flags,
	    readPtr->buffer, readPtr->offset, &readPtr->length, remoteWaitPtr);
    
    if ((status == SUCCESS) || (readPtr->length > 0)) { 
	(void)Fsdm_UpdateDescAttr(handlePtr, &handlePtr->cacheInfo.attr,
				FSDM_FD_ACCESSTIME_DIRTY);
	if (readPtr->flags & FS_SWAP) {
	    int hostID = Proc_GetHostID(readPtr->procID);
	    if (hostID == rpc_SpriteID)  {
		/*
		 * While page-ins on the file server come from its cache, we
		 * inform the cache that these pages are good canidicates
		 * for replacement.
		 */
		Fscache_BlocksUnneeded(streamPtr, savedOffset, savedLength, 
				FALSE);
	    }
	}

    }
    replyPtr->length = readPtr->length;
    return(status);
#endif /* SPRITED_LOCALDISK */
}

/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileWrite --
 *
 *	Write to a disk file.  This is a thin layer on top of the cache
 *	write routine.  Besides doing the write, this routine synchronizes
 *	with read ahead on the file.
 *
 * Results:
 *	The results of Fscache_Write.
 *
 * Side effects:
 *	The handle is locked during the I/O.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Fsio_FileWrite(streamPtr, writePtr, remoteWaitPtr, replyPtr)
    Fs_Stream		*streamPtr;	/* Open stream to the file. */
    Fs_IOParam		*writePtr;	/* Read parameter block */
    Sync_RemoteWaiter	*remoteWaitPtr;	/* Process info for remote waiting */
    Fs_IOReply		*replyPtr;	/* Signal to return, if any */
{
#ifndef SPRITED_LOCALDISK
    panic("Fsio_FileWrite called.\n");
#ifdef lint
    replyPtr = replyPtr;
    streamPtr = streamPtr;
    remoteWaitPtr = remoteWaitPtr;
    writePtr = writePtr;
#endif
    return FAILURE;		/* lint */
#else /* SPRITED_LOCALDISK */
    register Fsio_FileIOHandle *handlePtr =
	    (Fsio_FileIOHandle *)streamPtr->ioHandlePtr;
    register ReturnStatus status;
    int savedOffset = writePtr->offset;
    int savedLength = writePtr->length;
#ifdef SOSP91
    Fsconsist_Info *consistPtr = &handlePtr->consist;
    Fsconsist_ClientInfo	*clientPtr;
    Boolean			maybeShared = FALSE;
    int				numReading, numWriting;

    if ((handlePtr->descPtr != (Fsdm_FileDescriptor *) NIL) &&
	    (handlePtr->descPtr->fileType == FS_FILE)) {
	/*
	 * If this file is marked uncacheable or if we are the server doing
	 * a read and it's marked uncacheable on another client, then record
	 * this read.
	 */
	LIST_FORALL(&consistPtr->clientList, (List_Links *) clientPtr) {
	    if ((clientPtr->clientID == writePtr->reserved) &&
		    (!clientPtr->cached)) {
		maybeShared = TRUE;
		break;
	    } else if ((writePtr->reserved == rpc_SpriteID) &&
		    (!clientPtr->cached)) {
		maybeShared = TRUE;
		break;
	    }
	}
    }
    if (maybeShared) {
	(void) Fsconsist_NumClients(consistPtr, &numReading, &numWriting);
	SOSP_ADD_READ_TRACE(writePtr->reserved, handlePtr->hdr.fileID,
		streamPtr->hdr.fileID, FALSE, writePtr->offset, 
		writePtr->length, numReading, numWriting);
    }

#endif SOSP91

    /*
     * Get a reference to the domain so it can't be dismounted during the I/O.
     * Then synchronize with read ahead before actually doing the write.
     */
    if (Fsdm_DomainFetch(handlePtr->hdr.fileID.major, FALSE) ==
	    (Fsdm_Domain *)NIL) {
	return(FS_DOMAIN_UNAVAILABLE);
    }
    Fscache_WaitForReadAhead(&handlePtr->readAhead);
    status = Fscache_Write(&handlePtr->cacheInfo, writePtr->flags,
			  writePtr->buffer, writePtr->offset,
			  &writePtr->length, remoteWaitPtr);
    replyPtr->length = writePtr->length;
    if (status == SUCCESS) {
	if (replyPtr->length > 0) {
	    (void)Fsdm_UpdateDescAttr(handlePtr, &handlePtr->cacheInfo.attr, 
			FSDM_FD_MODTIME_DIRTY);
	}
	if (writePtr->flags & FS_SWAP) {
	    int hostID = Proc_GetHostID(writePtr->procID);
	    if (hostID == rpc_SpriteID)  {
		/*
		 * While page-outs on the file server go to its cache, we
		 * inform the cache that these pages are good canidicates
		 * for replacement.
		 */
		 Fscache_BlocksUnneeded(streamPtr, savedOffset, savedLength, 
				FALSE);
	    }
	}
    }

    Fscache_AllowReadAhead(&handlePtr->readAhead);
    Fsdm_DomainRelease(handlePtr->hdr.fileID.major);
    return(status);
#endif /* SPRITED_LOCALDISK */
}

/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileBlockCopy --
 *
 *	Copy the block from the source swap file to the destination swap file.
 *
 *	NOTE: This routine does not call the routine that puts swap file blocks
 *	      on the front of the free list.  This is because the general
 *	      mode of doing things is to fork which copies the swap file and
 *	      then exec which removes it.  Thus we want the swap file to be
 *	      in the cache for the copy and we don't have to put the 
 *	      destination files blocks on front of the lru list because it
 *	      is going to get removed real soon anyway.
 *
 * Results:
 *	Error code if couldn't allocate disk space or didn't read a full
 *	block.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
Fsio_FileBlockCopy(srcHdrPtr, dstHdrPtr, blockNum)
    Fs_HandleHeader	*srcHdrPtr;	/* File to copy block from.  */
    Fs_HandleHeader	*dstHdrPtr;	/* File to copy block to.  */
    int			blockNum;	/* Block to copy. */
{
#ifndef SPRITED_LOCALDISK
    panic("Fsio_FileBlockCopy called.\n");
    return FAILURE;		/* lint */
#else
    int			offset;
    ReturnStatus	status;
    Fscache_Block	*cacheBlockPtr;
    int			numBytes;
    register Fsio_FileIOHandle *srcHandlePtr =
	    (Fsio_FileIOHandle *)srcHdrPtr;
    register Fsio_FileIOHandle *dstHandlePtr =
	    (Fsio_FileIOHandle *)dstHdrPtr;

    /*
     * Look in the cache for the source block.
     */
    status = Fscache_BlockRead(&srcHandlePtr->cacheInfo, blockNum,
		    &cacheBlockPtr, &numBytes, FSCACHE_DATA_BLOCK, FALSE);
    if (status != SUCCESS) {
	return(status);
    }
    if (numBytes != FS_BLOCK_SIZE) {
	if (numBytes != 0) {
	    Fscache_UnlockBlock(cacheBlockPtr, 0, -1, 0,
			FSCACHE_CLEAR_READ_AHEAD);
	}
	return(VM_SHORT_READ);
    }
    /*
     * Write to the destination block.
     */
    numBytes = FS_BLOCK_SIZE;
    offset = blockNum * FS_BLOCK_SIZE;
    status = Fscache_Write(&dstHandlePtr->cacheInfo, FALSE,
		    cacheBlockPtr->blockAddr, offset, &numBytes,
		    (Sync_RemoteWaiter *) NIL);
    if (status == SUCCESS && numBytes != FS_BLOCK_SIZE) {

	status = VM_SHORT_WRITE;
    }

    Fscache_UnlockBlock(cacheBlockPtr, 0, -1, 0, FSCACHE_CLEAR_READ_AHEAD);

    srcHandlePtr->cacheInfo.attr.accessTime = Fsutil_TimeInSeconds();
    dstHandlePtr->cacheInfo.attr.modifyTime = Fsutil_TimeInSeconds();
    (void)Fsdm_UpdateDescAttr(srcHandlePtr, &srcHandlePtr->cacheInfo.attr, 
			FSDM_FD_ACCESSTIME_DIRTY);
    (void)Fsdm_UpdateDescAttr(dstHandlePtr, &dstHandlePtr->cacheInfo.attr, 
			FSDM_FD_MODTIME_DIRTY);

    return(status);
#endif /* SPRITED_LOCALDISK */
}

/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileIOControl --
 *
 *	IOControls for regular files.  The handle should be locked up entry.
 *	This handles byte swapping of its input and output buffers if
 *	the clients byte ordering/padding is different.
 *
 * Results:
 *	An error code from the command.
 *
 * Side effects:
 *	Command dependent.
 *
 *----------------------------------------------------------------------
 */

/*ARGSUSED*/
ReturnStatus
Fsio_FileIOControl(streamPtr, ioctlPtr, replyPtr)
    Fs_Stream *streamPtr;		/* Stream to local file */
    Fs_IOCParam *ioctlPtr;		/* I/O Control parameter block */
    Fs_IOReply *replyPtr;		/* Return length and signal */
{
    register Fsio_FileIOHandle *handlePtr =
	    (Fsio_FileIOHandle *)streamPtr->ioHandlePtr;
    register ReturnStatus status = SUCCESS;
    Boolean	unLock;

    Fsutil_HandleLock(handlePtr);
    unLock = TRUE;
    switch(ioctlPtr->command) {
	case IOC_REPOSITION:
	    break;
	case IOC_GET_FLAGS:
	    if ((ioctlPtr->outBufSize >= sizeof(int)) &&
		(ioctlPtr->outBuffer != (Address)NIL)) {
		*(int *)ioctlPtr->outBuffer = 0;
	    }
	    break;
	case IOC_SET_FLAGS:
	case IOC_SET_BITS:
	case IOC_CLEAR_BITS:
	    break;
#ifdef 0
	case IOC_MAP:
#endif
	case IOC_TRUNCATE: {
	    int arg;		/* The truncation length for IOC_TRUNCATE,
				 * The mapping flag for IOC_MAP. */

	    if (ioctlPtr->inBufSize < sizeof(int)) {
		status = GEN_INVALID_ARG;
	    } else if (ioctlPtr->format != fsMach_Format) {
		int outSize = sizeof(int);
		int inSize = sizeof(int);
		int fmtStatus;
		fmtStatus = Fmt_Convert("w", ioctlPtr->format, &inSize, 
				ioctlPtr->inBuffer, fsMach_Format, &outSize,
				(Address) &arg);
		if (fmtStatus != 0) {
		    printf("Format of ioctl failed <0x%x>\n", fmtStatus);
		    status = GEN_INVALID_ARG;
		}
		if (outSize != sizeof(int)) {
		    status = GEN_INVALID_ARG;
		}
	    } else {
		arg = *(int *)ioctlPtr->inBuffer;
	    }
	    if (status == SUCCESS) {
		if (ioctlPtr->command == IOC_TRUNCATE) {
		    if (arg < 0) {
			status = GEN_INVALID_ARG;
		    } else {
#ifdef SOSP91
			{
			    int oldSize;
			    int modifyTime = 
				handlePtr->cacheInfo.attr.modifyTime;
			    oldSize = handlePtr->cacheInfo.attr.lastByte;
			    status = Fsio_FileTrunc(handlePtr, arg, 0);
			    SOSP_ADD_TRUNCATE_TRACE(streamPtr->hdr.fileID,
				oldSize + 1, 
				handlePtr->cacheInfo.attr.lastByte + 1,
				modifyTime,
				handlePtr->cacheInfo.attr.createTime);
			}
#else
			status = Fsio_FileTrunc(handlePtr, arg, 0);
#endif
		    }
		} else {
		    Fsutil_HandleUnlock(handlePtr);
		    unLock = FALSE;
		    status = Fsconsist_MappedConsistency(handlePtr,
			    ioctlPtr->uid, arg);
		}
	    }
	    break;
	}
	case IOC_LOCK:
	case IOC_UNLOCK:
	    status = Fsio_IocLock(&handlePtr->lock, ioctlPtr,
				&streamPtr->hdr.fileID);
	    break;
	case IOC_NUM_READABLE: {
	    /*
	     * Return the number of bytes available to read.  The top-level
	     * IOControl routine has put the current stream offset in inBuffer.
	     */
	    int bytesAvailable;
	    int streamOffset;
	    int size;

	    if (ioctlPtr->inBufSize != sizeof(int)) {
		status = GEN_INVALID_ARG;
	    } else if (ioctlPtr->format != fsMach_Format) {
		int fmtStatus;
		int inSize;
		inSize = ioctlPtr->inBufSize;
		fmtStatus = Fmt_Convert("w", ioctlPtr->format, &inSize, 
				ioctlPtr->inBuffer, fsMach_Format, &size,
				(Address) &streamOffset);
		if (fmtStatus != 0) {
		    printf("Format of ioctl failed <0x%x>\n", fmtStatus);
		    status = GEN_INVALID_ARG;
		}
		if (size != sizeof(int)) {
		    status = GEN_INVALID_ARG;
		}
	    } else {
		streamOffset = *(int *)ioctlPtr->inBuffer;
	    }
	    if (status == SUCCESS) {
		bytesAvailable = handlePtr->cacheInfo.attr.lastByte + 1 -
				streamOffset;
		if (ioctlPtr->outBufSize != sizeof(int)) {
		    status = GEN_INVALID_ARG;
		} else if (ioctlPtr->format != fsMach_Format) {
		    int fmtStatus;
		    int	inSize;
		    inSize = sizeof(int);
		    fmtStatus = Fmt_Convert("w", fsMach_Format, &inSize, 
				    (Address) &bytesAvailable, ioctlPtr->format,
				    &size, ioctlPtr->outBuffer);
		    if (fmtStatus != 0) {
			printf("Format of ioctl failed <0x%x>\n", fmtStatus);
			status = GEN_INVALID_ARG;
		    }
		    if (size != sizeof(int)) {
			status = GEN_INVALID_ARG;
		    }
		} else {
		    *(int *)ioctlPtr->outBuffer = bytesAvailable;
		}
	    }
	    break;
	}
	case IOC_SET_OWNER:
	case IOC_GET_OWNER:
	    status = GEN_NOT_IMPLEMENTED;
	    break;
	case IOC_PREFIX:
	    break;
	case IOC_WRITE_BACK: {
	    /*
	     * Write out the cached data for the file.
	     */
	    Ioc_WriteBackArgs *argPtr = (Ioc_WriteBackArgs *)ioctlPtr->inBuffer;
	    Fscache_FileInfo *cacheInfoPtr = &handlePtr->cacheInfo;

	    if (ioctlPtr->inBufSize < sizeof(Ioc_WriteBackArgs)) {
		status = GEN_INVALID_ARG;
	    } else {
		int firstBlock, lastBlock;
		int blocksSkipped;
		int flags = 0;
		Ioc_WriteBackArgs writeBack;

		if (ioctlPtr->format != fsMach_Format) {
		    int fmtStatus;
		    int size;
		    size = ioctlPtr->inBufSize;
		    fmtStatus = Fmt_Convert("w3", ioctlPtr->format, &size, 
				    ioctlPtr->inBuffer, fsMach_Format, &size,
				    (Address) &writeBack);
		    if (fmtStatus != 0) {
			printf("Format of ioctl failed <0x%x>\n", fmtStatus);
			status = GEN_INVALID_ARG;
		    }
		    if (size != sizeof(Ioc_WriteBackArgs)) {
			status = GEN_INVALID_ARG;
		    }
		    if (status != SUCCESS) {
			break;
		    }
		    argPtr = &writeBack;
		}
		if (argPtr->shouldBlock) {
		    flags |= FSCACHE_FILE_WB_WAIT;
		}
		if (argPtr->firstByte > 0) {
		    firstBlock = argPtr->firstByte / FS_BLOCK_SIZE;
		} else {
		    firstBlock = 0;
		}
		if (argPtr->lastByte > 0) {
		    lastBlock = argPtr->lastByte / FS_BLOCK_SIZE;
		} else {
		    lastBlock = FSCACHE_LAST_BLOCK;
		}
#ifdef SOSP91
		cacheInfoPtr->flags |= FSCACHE_SYNC;
#endif SOSP91
		/*
		 * Release the handle lock during the FileWriteBack to 
		 * avoid hanging up everyone who stumbles over the handle
		 * during the writeback.
		 */
		Fsutil_HandleUnlock(handlePtr);
		unLock = FALSE;
		status = Fscache_FileWriteBack(cacheInfoPtr, firstBlock,
			lastBlock, flags, &blocksSkipped);
	    }
	    break;
	}
	default:
	    status = GEN_INVALID_ARG;
	    break;
    }
    if (unLock) { 
	Fsutil_HandleUnlock(handlePtr);
    }
    return(status);
}

/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileTrunc --
 *
 *	Shorten a file to length bytes.  This calls routines to update
 *	the cacheInfo and the fileDescriptor.
 *
 * Results:
 *	Error status from Fsdm_FileTrunc.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Fsio_FileTrunc(handlePtr, size, flags)
    Fsio_FileIOHandle	*handlePtr;	/* File to truncate. */
    int			size;		/* Size to truncate the file to. */
    int			flags;		/* FSCACHE_TRUNC_DELETE */
{
#ifndef SPRITED_LOCALDISK
    panic("Fsio_FileTrunc called.\n");
#ifdef lint
    flags = flags;
    size = size;
    handlePtr = handlePtr;
#endif
    return FAILURE;		/* lint */
#else /* SPRITED_LOCALDISK */
    ReturnStatus status;

    status = Fscache_Trunc(&handlePtr->cacheInfo, size, flags);
    if ((flags & FSCACHE_TRUNC_DELETE) == 0) {
	(void)Fsdm_UpdateDescAttr(handlePtr, &handlePtr->cacheInfo.attr, 
			FSDM_FD_MODTIME_DIRTY);
    }
    return(status);
#endif /* SPRITED_LOCALDISK */
}

/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileSelect --
 *
 *	Always returns that file is readable and writable.
 *
 * Results:
 *	SUCCESS		- always returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
Fsio_FileSelect(hdrPtr, waitPtr, readPtr, writePtr, exceptPtr)
    Fs_HandleHeader *hdrPtr;	/* The handle of the file */
    Sync_RemoteWaiter *waitPtr;	/* Process info for waiting */
    int		*readPtr;	/* Read bit */
    int		*writePtr;	/* Write bit */
    int		*exceptPtr;	/* Exception bit */
{
    *exceptPtr = 0;
    return(SUCCESS);
}

#ifdef SPRITED_LOCALDISK
/*
 *----------------------------------------------------------------------------
 *
 * IncVersionNumber --
 *
 *	Increment the version number on file.  This is done when a file
 *	is opened for writing, and the version number is used by clients
 *	to verify their caches.  This must be called with the handle locked.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Version number incremented and the descriptor is pushed to disk.
 *
 *----------------------------------------------------------------------------
 *
 */
static void
IncVersionNumber(handlePtr)
    Fsio_FileIOHandle	*handlePtr;
{
    Fsdm_FileDescriptor	*descPtr;

    descPtr = handlePtr->descPtr;
    descPtr->version++;
    handlePtr->cacheInfo.version = descPtr->version;
    descPtr->flags |= FSDM_FD_VERSION_DIRTY;
    (void)Fsdm_FileDescStore(handlePtr, FALSE);
    Vm_FileChanged(&handlePtr->segPtr);
}
#endif /* SPRITED_LOCALDISK */


/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileRecovTestUseCount --
 *
 *	For recovery testing, return the use count on the file's io handle.
 *
 * Results:
 *	Use count.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
Fsio_FileRecovTestUseCount(handlePtr)
    Fsio_FileIOHandle	*handlePtr;
{
    return handlePtr->use.ref;
}


/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileRecovTestNumCacheBlocks --
 *
 *	For recovery testing, return the number of blocks in the cache
 *	for this file.
 *
 * Results:
 *	Number of blocks.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
Fsio_FileRecovTestNumCacheBlocks(handlePtr)
    Fsio_FileIOHandle	*handlePtr;
{
    return handlePtr->cacheInfo.blocksInCache;
}


/*
 *----------------------------------------------------------------------
 *
 * Fsio_FileRecovTestNumDirtyCacheBlocks --
 *
 *	For recovery testing, return the number of dirty blocks in the cache
 *	for this file.
 *
 * Results:
 *	Number of dirty blocks.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
Fsio_FileRecovTestNumDirtyCacheBlocks(handlePtr)
    Fsio_FileIOHandle	*handlePtr;
{
    return handlePtr->cacheInfo.numDirtyBlocks;
}
