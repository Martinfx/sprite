/*
 * rpcDebug.c --
 *
 *	Debugging routines for the Rpc system.  These routines are used
 *	to profile and trace the whole system.
 *
 * Copyright 1985 Regents of the University of California
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/kernel/rpc/RCS/rpcDebug.c,v 9.6 91/02/12 14:15:43 jhh Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include <sprite.h>
#include <stdio.h>
#include <status.h>
#include <rpc.h>
#include <rpcInt.h>
#include <fs.h>
#include <timer.h>
#include <vm.h>
#include <sync.h>
#include <sched.h>
#include <rpcClient.h>
#include <rpcServer.h>
#include <rpcTrace.h>
#include <dev.h>
#include <user/sysStats.h>

#include <fsio.h>

Boolean		rpc_SanityCheck = TRUE;



/*
 *----------------------------------------------------------------------
 *
 * Test_RpcStub --
 *
 *	System call stub for the Rpc testing hook.  This is used
 *	to do ECHO and SEND rpcs.
 *
 * Results:
 *	An error code from the RPC.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Test_RpcStub(command, argPtr)
    int command;
    Address argPtr;
{
    int inSize;
    int outSize;
    int argSize;
    ReturnStatus status = SUCCESS;
    
    switch(command) {
	case TEST_RPC_ECHO: {
	    Rpc_EchoArgs *echoArgsPtr = (Rpc_EchoArgs *)argPtr;
	    Time deltaTime;

	    /*
	     * Make Accessible the struct containing the Echo arguments,
	     * then the in and out buffers.
	     */
	    Vm_MakeAccessible(VM_READONLY_ACCESS, sizeof(Rpc_EchoArgs),
			   (Address) echoArgsPtr, &argSize,
			   (Address *) (&echoArgsPtr));
	    if (echoArgsPtr == (Rpc_EchoArgs *)NIL) {
		return(RPC_INVALID_ARG);
	    }
	    Vm_MakeAccessible(VM_READONLY_ACCESS,
			      echoArgsPtr->size, echoArgsPtr->inDataPtr,
			      &inSize, &echoArgsPtr->inDataPtr);
	    if (echoArgsPtr->inDataPtr == (Address)NIL) {
		Vm_MakeUnaccessible((Address) echoArgsPtr, argSize);
		return(RPC_INVALID_ARG);
	    }
	    Vm_MakeAccessible(VM_OVERWRITE_ACCESS,
			      echoArgsPtr->size, echoArgsPtr->outDataPtr,
			      &outSize, &echoArgsPtr->outDataPtr);
	    if (echoArgsPtr->outDataPtr == (Address)NIL) {
		Vm_MakeUnaccessible(echoArgsPtr->inDataPtr, inSize);
		Vm_MakeUnaccessible((Address) echoArgsPtr, argSize);
		return(RPC_INVALID_ARG);
	    }
	    echoArgsPtr->size = (inSize > outSize) ? outSize : inSize ;

	    status = Rpc_EchoTest(echoArgsPtr->serverID, echoArgsPtr->n,
				echoArgsPtr->size, echoArgsPtr->inDataPtr,
				echoArgsPtr->outDataPtr, &deltaTime);

	    (void) Vm_CopyOut(sizeof(Time), (Address)&deltaTime,
				     (Address)echoArgsPtr->deltaTimePtr);
	    Vm_MakeUnaccessible(echoArgsPtr->inDataPtr, inSize);
	    Vm_MakeUnaccessible(echoArgsPtr->outDataPtr, outSize);
	    Vm_MakeUnaccessible((Address) echoArgsPtr, argSize);
	    break;
	}
	case TEST_RPC_SEND: {
	    Rpc_EchoArgs *echoArgsPtr = (Rpc_EchoArgs *)argPtr;
	    Time deltaTime;

	    Vm_MakeAccessible(VM_READONLY_ACCESS, sizeof(Rpc_EchoArgs),
			   (Address) echoArgsPtr, &argSize,
			   (Address *) (&echoArgsPtr));
	    if (argSize != sizeof(Rpc_EchoArgs)) {
		return(RPC_INVALID_ARG);
	    }
	    Vm_MakeAccessible(VM_READONLY_ACCESS,
			      echoArgsPtr->size, echoArgsPtr->inDataPtr,
			      &inSize, &echoArgsPtr->inDataPtr);
	    if (echoArgsPtr->inDataPtr == (Address)NIL) {
		Vm_MakeUnaccessible((Address) echoArgsPtr, argSize);
		return(RPC_INVALID_ARG);
	    }

	    status = Rpc_SendTest(echoArgsPtr->serverID, echoArgsPtr->n,
				inSize, echoArgsPtr->inDataPtr, &deltaTime);

	    (void) Vm_CopyOut(sizeof(Time), (Address)&deltaTime,
				     (Address)echoArgsPtr->deltaTimePtr);
	    Vm_MakeUnaccessible(echoArgsPtr->inDataPtr, inSize);
	    Vm_MakeUnaccessible((Address) echoArgsPtr, argSize);
	    break;
	}
	default:
	    status = RPC_INVALID_ARG;
	    break;
    }
    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * Rpc_GetStats --
 *
 *	Copy out rpc system stats.
 *
 * Results:
 *	SUCCESS, unless an argument could not be made accessible.
 *
 * Side effects:
 *	Fill in the requested statistics.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Rpc_GetStats(command, option, argPtr)
    int command;		/* Specifies what to do */
    int option;			/* Modifier for command */
    Address argPtr;		/* Argument for command */
{
    ReturnStatus status = SUCCESS;
    
    switch(command) {
	case SYS_RPC_ENABLE_SERVICE: {
	    /*
	     * A basic On/Off switch for the RPC system.  Servers in
	     * particular want to get everything ready before responding
	     * to clients.
	     */
	    if (option) {
		if (rpcSendNegAcks) {
		    RpcSetNackBufs();
		}
		printf("Starting RPC service\n");
		rpcServiceEnabled = TRUE;
	    } else {
		printf("Warning: Disabling RPC service\n");
		rpcServiceEnabled = FALSE;
	    }
	    break;
	}
	case SYS_RPC_NUM_NACK_BUFS: {
	    /*
	     * Set the number of negative ack buffers.
	     */
	    if (rpcServiceEnabled) {
		printf(
		    "Rpc service already enabled, cannot change nack bufs.\n");
		break;
	    }
	    if (option > 0) {
		printf("Setting number of nack bufs to %d\n", option);
		rpc_NumNackBuffers = option;
		RpcSetNackBufs();
	    }
	    break;
	}
	case SYS_RPC_SET_MAX: {
	    /*
	     * Set the maximum number of server procs allowed.  This can't
	     * go higher than the absolute maximum.
	     */
	    if (option <= 0) {
		printf("Warning: asked to set max number of rpc server ");
		printf("procs to %d.  I won't do this.\n", option);
	    } if (option < rpcNumServers) {
		printf("Warning: asked to set max number of rpc server ");
		printf("procs to %d which is less than the current ", option);
		printf("number of procs: %d.\n", rpcNumServers);
		printf("I can't do this.\n");
	    } else if (option > rpcAbsoluteMaxServers) {
		printf("Warning: asked to set max number of rpc server ");
		printf("procs to %d which is above the absolute ", option);
		printf("maximum of %d.\n", rpcAbsoluteMaxServers);
		printf("Setting this to maximum instead.\n");
		rpcMaxServers = rpcAbsoluteMaxServers;
	    } else {
		printf("Setting max number of server procs to %d.\n", option);
		rpcMaxServers = option;
	    }
	    break;
	}
	case SYS_RPC_SET_NUM: {
	    /*
	     * Set the number of rpc processes to the given number, if
	     * there aren't already that many.
	     */
	    int	pid;

	    if (option <= 0) {
		printf("Warning: asked to set number of rpc server ");
		printf("procs to %d.  I won't do this.\n", option);
	    } else if (option < rpcNumServers) {
		printf("There are already %d server procs, ", rpcNumServers);
		printf("and I won't kill them.\n");
	    } else if (option > rpcMaxServers) {
		printf("Warning: asked to set number of rpc server ");
		printf("procs to %d, which is above the maximum of %d\n",
			option, rpcMaxServers);
		printf("I'll create up to the maximum.\n");
		while (rpcNumServers < rpcMaxServers) {
		    if (Rpc_CreateServer(&pid) == SUCCESS) {
			printf("RPC srvr %x\n", pid);
			RpcResetNoServers(0);
		    } else {
			printf("Warning: no more RPC servers\n");
			RpcResetNoServers(-1);
		    }
		}
	    } else {
		/* create the correct number */
		while (rpcNumServers < option) {
		    if (Rpc_CreateServer(&pid) == SUCCESS) {
			printf("RPC srvr %x\n", pid);
			RpcResetNoServers(0);
		    } else {
			printf("Warning: no more RPC servers\n");
			RpcResetNoServers(-1);
		    }
		}
	    }
	    break;
	}
	case SYS_RPC_NEG_ACKS: {
	    printf("Turning negative acknowledgements %s.\n",
		    option ? "on" : "off");
	    rpcSendNegAcks = option;
	    break;
	}
	case SYS_RPC_CHANNEL_NEG_ACKS: {
	    printf("Turning client policy of ramping down channels %s.\n",
		    option ? "on" : "off");
	    rpcChannelNegAcks = option;
	    break;
	}
	case SYS_RPC_CLT_STATS: {
	    register Rpc_CltStat *cltStatPtr;

	    cltStatPtr = (Rpc_CltStat *)argPtr;
	    if (cltStatPtr == (Rpc_CltStat *)NIL ||
		cltStatPtr == (Rpc_CltStat *)0 ||
		cltStatPtr == (Rpc_CltStat *)USER_NIL) {
		
		Rpc_PrintCltStat();
	    } else {
		RpcResetCltStat();
		status = Vm_CopyOut(sizeof(Rpc_CltStat),
				  (Address)&rpcTotalCltStat,
				  (Address) cltStatPtr);
	    }
	    break;
	}
	case SYS_RPC_SRV_STATS: {
	    register Rpc_SrvStat *srvStatPtr;

	    srvStatPtr = (Rpc_SrvStat *)argPtr;
	    if (srvStatPtr == (Rpc_SrvStat *)NIL ||
		srvStatPtr == (Rpc_SrvStat *)0 ||
		srvStatPtr == (Rpc_SrvStat *)USER_NIL) {
		
		Rpc_PrintSrvStat();
	    } else {
		RpcResetSrvStat();
		status = Vm_CopyOut(sizeof(Rpc_SrvStat),
				  (Address)&rpcTotalSrvStat,
				  (Address) srvStatPtr);
	    }
	    break;
	}
	case SYS_RPC_TRACE_STATS: {
	    switch(option) {
		case SYS_RPC_TRACING_PRINT:
		    Rpc_PrintTrace((ClientData)32);
		    break;
		case SYS_RPC_TRACING_ON:
		    rpc_Tracing = TRUE;
		    break;
		case SYS_RPC_TRACING_OFF:
		    rpc_Tracing = FALSE;
		    break;
		default:
		    /*
		     * The option is the size of the users buffer to
		     * hold all the trace records.
		     */
		    status = Trace_Dump(rpcTraceHdrPtr, RPC_TRACE_LEN, argPtr);
		    break;
	    }
	    break;
	}
	case SYS_RPC_SERVER_HIST: {
	    /*
	     * Operate on the service-time histograms, depending on option.
	     */
	    if (option > 0 && option <= RPC_LAST_COMMAND) {
		/*
		 * Copy out the histogram for the service time of the RPC
		 * indicated by option.
		 */
		status = Rpc_HistDump(rpcServiceTime[option], argPtr);
	    } else if (option > RPC_LAST_COMMAND) {
		status = RPC_INVALID_ARG;
	    } else {
		/*
		 * Reset all the server side histograms
		 */
		status = SUCCESS;
		for (option = 1 ; option <= RPC_LAST_COMMAND ; option++) {
		    Rpc_HistReset(rpcServiceTime[option]);
		}
	    }
	    break;
	}
	case SYS_RPC_CLIENT_HIST: {
	    /*
	     * Operate on the client call-time histograms, depending on option.
	     */
	    if (option > 0 && option <= RPC_LAST_COMMAND) {
		/*
		 * Copy out the histogram for the client's view of the service
		 * time of the RPC indicated by option.
		 */
		status = Rpc_HistDump(rpcCallTime[option], argPtr);
	    } else if (option > RPC_LAST_COMMAND) {
		status = RPC_INVALID_ARG;
	    } else {
		/*
		 * Reset all the client side histograms
		 */
		status = SUCCESS;
		for (option = 1 ; option <= RPC_LAST_COMMAND ; option++) {
		    Rpc_HistReset(rpcCallTime[option]);
		}
	    }
	    break;
	}
	case SYS_RPC_SRV_STATE: {
	    /*
	     * Return the state of the server processes.
	     */
	    if (option >= 0 && option < rpcNumServers) {
		/*
		 * Copy out the state of the option'th server process.
		 */
		status = Vm_CopyOut(sizeof(RpcServerState),
				  (Address)rpcServerPtrPtr[option], argPtr);
	    } else {
		status = RPC_INVALID_ARG;
	    }
	    break;
	}
	case SYS_RPC_CLT_STATE: {
	    /*
	     * Return the state of the client channels.
	     */
	    if (option >= 0 && option < rpcNumChannels) {
		/*
		 * Copy out the state of the option'th client channel.
		 */
		status = Vm_CopyOut(sizeof(RpcClientChannel),
				  (Address)rpcChannelPtrPtr[option], argPtr);
	    } else {
		status = RPC_INVALID_ARG;
	    }
	    break;
	}
	case SYS_RPC_CALL_COUNTS: {
	    register int *callCountPtr;

	    callCountPtr = (int *)argPtr;
	    if (callCountPtr == (int *)NIL ||
		callCountPtr == (int *)0 ||
		callCountPtr == (int *)USER_NIL ||
		option <= 0) {
		
		Rpc_PrintCallCount();
	    } else {
		status = Vm_CopyOut(option,
				  (Address) rpcClientCalls,
				  (Address) callCountPtr);
	    }
	    break;
	}
	case SYS_RPC_SRV_COUNTS: {
	    register int *serviceCountPtr;

	    serviceCountPtr = (int *)argPtr;
	    if (serviceCountPtr == (int *)NIL ||
		serviceCountPtr == (int *)0 ||
		serviceCountPtr == (int *)USER_NIL ||
		option <= 0) {
		
		Rpc_PrintServiceCount();
	    } else {
		status = Vm_CopyOut(option,
				  (Address) rpcServiceCount,
				  (Address) serviceCountPtr);
	    }
	    break;
	}
	case SYS_RPC_SANITY_CHECK: {
	    printf("Turning RPC sanity checking %s, was %s.\n",
		    option ? "on" : "off",
		    rpc_SanityCheck ? "on" : "off");
	    rpc_SanityCheck = option;
	    break;
	}
	default:
	    status = RPC_INVALID_ARG;
	    break;
    }
    return(status);
}

#if 0
/*
 * List of the minimum allowable size for the parameter area of Rpc
 * requests. 
 */
static int rpcSanityParamSize[RPC_NUM_COMMANDS] = {
   {0,	 				/* RPC_BAD_COMMAND */
    0,	 				/* RPC_ECHO_1 */
    0,					/* RPC_ECHO_2 */
    0, 					/* RPC_SEND */
    0, 					/* RPC_RECEIVE */
    0,					/* RPC_GETTIME */
    0,	  				/* RPC_FS_PREFIX */
    sizeof(Fs_OpenArgs), 		/* RPC_FS_OPEN */
    sizeof(FsrmtIOParam), 		/* RPC_FS_READ */
    sizeof(FsrmtIOParam), 		/* RPC_FS_WRITE */
    sizeof(FsRemoteCloseParams),	/* RPC_FS_CLOSE */
    sizeof(Fs_LookupArgs), 		/* RPC_FS_UNLINK */
    sizeof(Fs_2PathParams),		/* RPC_FS_RENAME */
    sizeof(Fs_OpenArgs),		/* RPC_FS_MKDIR */
    sizeof(Fs_LookupArgs),		/* RPC_FS_RMDIR */
    sizeof(Fs_MakeDeviceArgs),		/* RPC_FS_MKDEV */
    sizeof(Fs_2PathParams),		/* RPC_FS_LINK */
    0,					/* RPC_FS_SYM_LINK */
    sizeof(Fs_FileID), 			/* RPC_FS_GET_ATTR */
    sizeof(FsRemoteSetAttrParams), 	/* RPC_FS_SET_ATTR */
    sizeof(Fs_OpenArgs),		/* RPC_FS_GET_ATTR_PATH */
    sizeof(Fs_SetAttrArgs),		/* RPC_FS_SET_ATTR_PATH */
    sizeof(Fs_GetAttrResultsParam), 	/* RPC_FS_GET_IO_ATTR */
    sizeof(FsRemoteSetAttrParams),	/* RPC_FS_SET_IO_ATTR */
    sizeof(FsDeviceRemoteOpenParam),	/* RPC_FS_DEV_OPEN */
    sizeof(FsRemoteSelectParams),	/* RPC_FS_SELECT */
    sizeof(FsrmtIOCParam),		/* RPC_FS_IO_CONTROL */
    sizeof(ConsistMsg),			/* RPC_FS_CONSIST */
    sizeof(ConsistReply),		/* RPC_FS_CONSIST_REPLY */
    sizeof(FsrmtBlockCopyParam),	/* RPC_FS_COPY_BLOCK */
    sizeof(FsMigInfo),			/* RPC_FS_MIGRATE */
    sizeof(FsStreamReleaseParam),	/* RPC_FS_RELEASE */
    sizeof(Fs_FileID),			/* RPC_FS_REOPEN */
    sizeof(int),			/* RPC_FS_RECOVERY */
    sizeof(Fs_FileID),			/* RPC_FS_DOMAIN_INFO */
    sizeof(ProcMigCmd),			/* RPC_PROC_MIG_COMMAND */
    0,					/* RPC_PROC_REMOTE_CALL */
    sizeof(ProcRemoteWaitCmd),		/* RPC_PROC_REMOTE_WAIT */
    sizeof(int),			/* RPC_PROC_GETPCB */
    sizeof(Sync_RemoteWaiter),		/* RPC_REMOTE_WAKEUP */
    sizeof(SigParms),			/* RPC_SIG_SEND */
    sizeof(FsStreamReleaseParam), 	/* RPC_FS_RELEASE_NEW */
};
#endif


/*
 *----------------------------------------------------------------------
 *
 * Rpc_SanityCheck --
 *
 *	Do some sanity checks on the packet.  This is not meant to be
 *	an exhaustive test of the validity of a packet; rather it
 *	is intended to be a debugging aid for weeding out bad packets
 *	from clients.
 *
 * Results:
 *	SUCCESS if the packet looks ok, FAILURE otherwise
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Rpc_SanityCheck(length, scatterPtr, packetLength)
    int			length;		/* Length of the scatter/gather 
					 * array. */	
    Net_ScatterGather	*scatterPtr; 	/* The scatter/gather array. */
    int			packetLength;	/* Length of the network packet. */

{
    static int		buffer[(RPC_MAX_SIZE + sizeof(RpcHdr)) / sizeof(int)];
    RpcHdr		*rpcHdrPtr = NULL;
    int			rpcLength;
    ReturnStatus	status = SUCCESS;
    char		*packetPtr;
    int			paramSize;
    int			dataSize;

    if (packetLength < sizeof(RpcHdr)) {
	printf("RpcSanityCheck: packet smaller than an RpcHdr (%d)\n",
	    packetLength);
	return FAILURE;
    }
    if (length == 1) {
	rpcHdrPtr = (RpcHdr *) scatterPtr->bufAddr;
	packetPtr = (char *) scatterPtr->bufAddr;
    } else {
	Net_GatherCopy(scatterPtr, length, 
	    (Address) buffer);
	rpcHdrPtr = (RpcHdr *) buffer;
	packetPtr = (char *) buffer;
    }
    if ((int) rpcHdrPtr & 0x3) {
	printf("rpcHdrPtr = 0x%x\n", rpcHdrPtr);
	rpc_SanityCheck = FALSE;
	panic("Bye\n");
    }
    rpcLength = sizeof(RpcHdr) + rpcHdrPtr->paramSize + rpcHdrPtr->dataSize;
    if (rpcLength > packetLength) {
	printf("RpcSanityCheck: packet too short, %d < %d\n", 
	    packetLength, rpcLength);
	status = FAILURE;
	goto done;
    }
    /*
     * We only deal with the first fragment of an RPC.
     */

    if ((rpcHdrPtr->numFrags != 0) && (rpcHdrPtr->fragMask != 0x1)) {
	return SUCCESS;
    }
    paramSize = rpcHdrPtr->paramSize;
    dataSize = rpcHdrPtr->dataSize;
    if (rpcHdrPtr->flags & RPC_REQUEST) {
	switch(rpcHdrPtr->command) {
	    case RPC_FS_OPEN:
	    case RPC_FS_READ:
	    case RPC_FS_WRITE:
	    case RPC_FS_CLOSE:
	    case RPC_FS_UNLINK:
	    case RPC_FS_MKDIR:
	    case RPC_FS_RMDIR:
	    case RPC_FS_MKDEV:
	    case RPC_FS_GET_ATTR:
	    case RPC_FS_SET_ATTR:
	    case RPC_FS_GET_ATTR_PATH:
	    case RPC_FS_SET_ATTR_PATH:
	    case RPC_FS_SET_IO_ATTR:
	    case RPC_FS_DEV_OPEN:
	    case RPC_FS_SELECT: 
	    case RPC_FS_IO_CONTROL:
	    case RPC_FS_CONSIST:
	    case RPC_FS_CONSIST_REPLY:
	    case RPC_FS_COPY_BLOCK:
	    case RPC_FS_MIGRATE:
	    case RPC_FS_RELEASE:
	    case RPC_FS_REOPEN:
	    case RPC_FS_DOMAIN_INFO: {
		Fs_FileID	*fileIDPtr;
		if (paramSize < sizeof(Fs_FileID)) {
		    printf("Rpc_SanityCheck: request %d param too small (%d)\n",
			paramSize);
		    status = FAILURE;
		    break;
		}
		fileIDPtr = (Fs_FileID *) (packetPtr + sizeof(RpcHdr));
		if ((int) packetPtr & 0x3) {
		    printf("packetPtr = 0x%x\n", packetPtr);
		    panic("Bye\n");
		}
		if ((fileIDPtr->type < 0) || 
		    (fileIDPtr->type >= FSIO_NUM_STREAM_TYPES)) {
		    printf("Rpc_SanityCheck: request %d file id type = %d\n", 
			rpcHdrPtr->command, fileIDPtr->type);
		    status = FAILURE;
		}
		break;
	    }
	}
    }
done:
    if (status != SUCCESS) {
	printf("Rpc_SanityCheck: client %d, server %d:\n", 
	    rpcHdrPtr->clientID, rpcHdrPtr->serverID);
    }
    return status;
}


