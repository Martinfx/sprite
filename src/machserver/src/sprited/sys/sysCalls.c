/* 
 * sysCalls.c --
 *
 *	Miscellaneous system calls that are lumped under the Sys_ prefix.
 *
 * Copyright 1986, 1991 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that this copyright
 * notice appears in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /user5/kupfer/spriteserver/src/sprited/sys/RCS/sysCalls.c,v 1.14 92/07/16 18:11:13 kupfer Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include <sprite.h>
#include <ckalloc.h>
#include <mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <mach_error.h>
#include <string.h>

#include <dbg.h>
#include <dev.h>
#include <fsprefix.h>
#include <fsutil.h>
#include <machCalls.h>
#include <net.h>
#include <proc.h>
#include <rpc.h>
#include <user/sched.h>
#include <sig.h>
#include <spriteSrvServer.h>
#include <sync.h>
#include <sys.h>
#include <sysCallNums.h>
#include <user/sys.h>
#include <user/sysStats.h>
#include <timer.h>
#include <utils.h>
#include <vm.h>

/* 
 * System shutdown happens in three stages (after preliminaries like
 * flushing the disks).  First, all the user processes are killed.  Second,
 * the VM system flushes dirty segments and cleans up.  Third, all
 * remaining processes are killed.
 */
    
#define KILL_USER_PROCS		0
#define CLEAN_VM		1
#define KILL_SERVER_PROCS	2

/* 
 * Define an array of kill procedures and names, so that we can do the 
 * 3-stage shutdown in a loop.
 */

typedef int (*SysKillProc)_ARGS_((void)); /* function to kill things */

static int KillUserProcesses _ARGS_((void));
static int KillServerProcesses _ARGS_((void));

static SysKillProc killProc[] = {KillUserProcesses, Vm_Shutdown,
				     KillServerProcesses};
static char *killObject[] = {"user processes", "VM segments",
				 "server processes"};

/* 
 * When shutting down, this is the number of times to loop before 
 * giving up.
 */
#define	MAX_WAIT_INTERVALS	3


Boolean	sys_ErrorShutdown = FALSE;
Boolean	sys_ShouldSyncDisks = TRUE;
Boolean	sys_ShuttingDown = FALSE;

/*
 * Exported so we can deny RPC requests as we die.
 * Otherwise we can hang RPCs if we hang trying to sync our disks.
 */
Boolean	sys_ErrorSync = FALSE;


static Boolean	shutdownDebug = FALSE;

/* More forward references */

static void GetSchedStats _ARGS_((Sched_Instrument *statsPtr));
static ReturnStatus Sys_OutputNumCalls _ARGS_((int numToCopy,
					       Address buffer));


/*
 *----------------------------------------------------------------------
 *
 * Sys_GetTimeOfDayStub --
 *
 *	Returns the current system time to a local user process.
 *
 *	The "real" time of day is returned, rather than the software
 *	time.
 *
 * Results:
 * 	Returns KERN_SUCCESS.  Sprite status should always be SUCCESS.
 * 	Fills in the "pending signals" flag.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

kern_return_t
Sys_GetTimeOfDayStub(serverPort, timePtr, localOffsetPtr, DSTPtr,
		     statusPtr, pendingSigPtr)
    mach_port_t	serverPort;		/* server request port */
    Time	*timePtr;		/* OUT: Buffer to store the UT time. */
    int		*localOffsetPtr;	/* OUT: Buffer to store the number 
					 * of minutes from UT (a negative
					 * value means you are west of the
					 * Prime Meridian. */
    Boolean	*DSTPtr;		/* OUT: Buffer to store a flag
					 * that's TRUE if DST is followed. */
    ReturnStatus *statusPtr;		/* OUT: Sprite status */
    boolean_t	*pendingSigPtr;		/* OUT: there is a pending signal */
{
#ifdef lint
    serverPort = serverPort;
#endif

    Timer_GetTimeOfDay(timePtr, localOffsetPtr, DSTPtr);
    *statusPtr = SUCCESS;
    *pendingSigPtr = Sig_Pending(Proc_GetCurrentProc());

    return KERN_SUCCESS;
}


/*
 *----------------------------------------------------------------------
 *
 * Sys_SetTimeOfDayStub --
 *
 *	Set the system clock.  Currently disabled.
 *
 * Results:
 *	Returns KERN_SUCCESS.  Fills in a Sprite status code and "pending 
 *	signals" flag.
 *
 * Side effects:
 *	None, currently.
 *
 *----------------------------------------------------------------------
 */

kern_return_t
Sys_SetTimeOfDayStub(serverPort, time, localOffset, dstOk, statusPtr,
		     sigPendingPtr)
    mach_port_t serverPort;	/* server request port */
    Time time;			/* New value for the UT (GMT) time.  */
    int localOffset;		/* new value for the offset in minutes 
				 * from UT. */
    boolean_t dstOk;		/* If TRUE, DST is used at this site. */
    ReturnStatus *statusPtr;	/* OUT: Sprite status code */
    boolean_t *sigPendingPtr;	/* OUT: pending signals flag */
{
#ifdef lint
    serverPort = serverPort;
    time = time;
    localOffset = localOffset;
    dstOk = dstOk;
#endif

    *statusPtr = GEN_NO_PERMISSION;
    *sigPendingPtr = Sig_Pending(Proc_GetCurrentProc());

    return KERN_SUCCESS;
}


/*
 *----------------------------------------------------------------------
 *
 * Sys_ShutdownStub --
 *
 *	MIG interface to Sys_Shutdown.
 *
 * Results:
 *	Returns KERN_SUCCESS.  Fills in the "pending signals" flag.
 *
 * Side effects:
 *	See Sys_Shutdown.
 *
 *----------------------------------------------------------------------
 */

kern_return_t
Sys_ShutdownStub(port, flags, pendingSigPtr)
    mach_port_t	port;		/* request port */
    int flags;
    boolean_t *pendingSigPtr;	/* OUT: the process has pending signals */
{
#ifdef lint
    port = port;
#endif

    Sys_Shutdown(flags);
    *pendingSigPtr = Sig_Pending(Proc_GetCurrentProc());
    return KERN_SUCCESS;
}


/*
 *----------------------------------------------------------------------
 *
 * Sys_Shutdown --
 *
 *	Shut down the system and exit.
 *	XXX Should check the permissions of the calling process?
 *
 * Results:
 *	Returns SUCCESS if none of SYS_HALT, SYS_REBOOT, or SYS_DEBUG was 
 *	specified in "flags".  Otherwise doesn't return at all.
 *
 * Side effects:
 *	Writes back dirty FS and VM blocks if SYS_WRITE_BACK was specified.
 *
 *----------------------------------------------------------------------
 */

void
Sys_Shutdown(flags)
    int flags;
{
    Time		waitTime;
    int			alive;
    int			timesWaited;
    int			stage;

    if (flags & SYS_DEBUG) {
	sys_ErrorShutdown = TRUE;
    }

    if (flags & SYS_KILL_PROCESSES) {
	if (flags & SYS_WRITE_BACK) { 
	    /*
	     * Do a few initial syncs.
	     * These are necessary because the cache isn't getting written
	     * out properly with the new block cleaner.
	     */
	    printf("Doing initial syncs\n");
	    Fsutil_Sync(-1, 0);
	    Fsutil_Sync(-1, 0);
	    Fsutil_Sync(-1, 0);
	    printf("Done initial syncs\n");
	}

	/*
	 * Get rid of any migrated processes.
	 */
#ifdef SPRITED_MIGRATION
	(void) Proc_EvictForeignProcs();
#endif
	
	waitTime.seconds = 5;
	waitTime.microseconds = 0;

	for (stage = KILL_USER_PROCS; stage <= KILL_SERVER_PROCS; stage++) {
	    if (stage == KILL_SERVER_PROCS) {
		sys_ShuttingDown = TRUE;
	    }
		
	    timesWaited = 0;
	    while (TRUE) {
		alive = (*killProc[stage])();
		if (alive == 0) {
		    break;
		}
		if (timesWaited >= MAX_WAIT_INTERVALS) {
		    printf("%d %s still alive.\n", alive,
			   killObject[stage]);
		    break;
		}
		timesWaited++;
		printf("Waiting with %d %s still alive\n", alive,
		       killObject[stage]);
		if (shutdownDebug) {
		    DBG_CALL;
		}
		(void) Sync_WaitTime(waitTime);
	    }
	}
    }

    /*
     * Sync the disks.
     */
    if (flags & SYS_WRITE_BACK) {
	printf("Syncing disks\n");
	Fsutil_Sync(-1, flags & SYS_KILL_PROCESSES);
    }

    /* 
     * Shutdown if requested, or return.
     */
    if (flags & SYS_REBOOT) {
	printf("Warning: sprited can't restart itself.\n");
    }
    if (flags & (SYS_HALT | SYS_REBOOT)) {
	exit(0);
    } else if (flags & SYS_DEBUG) {
	sys_ShuttingDown = FALSE;
	sys_ErrorShutdown = FALSE;
	DBG_CALL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Sys_StatsStub --
 *
 *	MIG server routine for the Statistics hook.
 *
 * Results:
 * 	Returns KERN_SUCCESS.  Fills in the Sprite status code from 
 * 	Sys_Stats and the "pending signals" flag.
 *
 * Side effects:
 *	Writes the requested statistics to the user process.
 *
 *----------------------------------------------------------------------
 */
kern_return_t
Sys_StatsStub(serverPort, command, option, argPtr, statusPtr, sigPendingPtr)
    mach_port_t serverPort;
    int command;		/* Specifies what to do */
    int option;			/* Modifier for command */
    vm_address_t argPtr;	/* Argument for command (user address) */
    ReturnStatus *statusPtr;	/* OUT: Sprite return code */
    boolean_t *sigPendingPtr;	/* OUT: is there a pending signal */
{
#ifdef lint
    serverPort = serverPort;
#endif

    *statusPtr = Sys_Stats(command, option, (Address)argPtr);
    *sigPendingPtr = Sig_Pending(Proc_GetCurrentProc());
    return KERN_SUCCESS;
}


/*
 *----------------------------------------------------------------------
 *
 * Sys_Stats --
 *
 *	The Statistics hook.
 *
 * Results:
 *	SUCCESS			- the data were returned.
 *	GEN_INVALID_ARG		- if a bad argument was passed in.
 *	?			- result from Vm_CopyOut.
 *	
 *
 * Side effects:
 *	Fill in the requested statistics.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Sys_Stats(command, option, argPtr)
    int command;		/* Specifies what to do */
    int option;			/* Modifier for command */
    Address argPtr;		/* Argument for command (user address) */
{
    /*
     * These extern decl's are temporary and should be removed when the
     * START_STATS and END_STATS are removed.
     */
    extern void	Dev_StartIOStats();
    extern void	Dev_StopIOStats();

    ReturnStatus status = SUCCESS;
    
    switch(command) {
	case SYS_GET_VERSION_STRING: {
	    /*
	     * option is the length of the storage referenced by argPtr.
	     */
	    register int length;
	    register char *version;
	    extern char *Version();

	    version = Version();
	    length = strlen(version) + 1;
	    if (option <= 0) {
		status = GEN_INVALID_ARG;
		break;
	    }
	    if (option < length) {
		length = option;
	    }
	    status = Vm_CopyOut(length, version, argPtr);
	    break;
	}
#if 0
	case SYS_SYNC_STATS: {
	    register Sync_Instrument *syncStatPtr;

	    syncStatPtr = (Sync_Instrument *)argPtr;
	    if (syncStatPtr == (Sync_Instrument *)NIL ||
		syncStatPtr == (Sync_Instrument *)0 ||
		syncStatPtr == (Sync_Instrument *)USER_NIL) {
		
		Sync_PrintStat();
	    } else if (option <= 0) {
		status = GEN_INVALID_ARG;
		break;
	    } else {
		if (option > sizeof(sync_Instrument)) {
		    option = sizeof(sync_Instrument);
		}
		status = Vm_CopyOut(option,
				  (Address) sync_Instrument,
				  (Address) syncStatPtr);
	    }
	    break;
	}
	case SYS_VM_STATS: {
	    if (argPtr == (Address)NIL ||
		argPtr == (Address)0 ||
		argPtr == (Address)USER_NIL) {
		return(GEN_INVALID_ARG);
	    } else {
		status = Vm_CopyOut(sizeof(Vm_Stat), (Address)&vmStat, argPtr);
	    }
	    break;
	}
#endif /* 0 */
	case SYS_SCHED_STATS: {
	    Sched_Instrument schedStats;

	    /* 
	     * Make sure we don't pass in the wrong amount of bytes (e.g.,
	     * if the client is using a new definition of Sched_Instrumet).
	     */
	    if (option > sizeof(Sched_Instrument)) {
		option = sizeof(Sched_Instrument);
	    }

	    GetSchedStats(&schedStats);
	    status = Vm_CopyOut(option, (Address)&schedStats, argPtr);
	    break;
	}

	case SYS_RPC_CLT_STATS:
	case SYS_RPC_SRV_STATS:
	case SYS_RPC_TRACE_STATS:
	case SYS_RPC_SERVER_HIST:
	case SYS_RPC_CLIENT_HIST:
	case SYS_RPC_SRV_STATE:
	case SYS_RPC_CLT_STATE:
	case SYS_RPC_ENABLE_SERVICE:
	case SYS_RPC_SRV_COUNTS:
	case SYS_RPC_CALL_COUNTS:
	case SYS_RPC_SET_MAX:
	case SYS_RPC_SET_NUM:
	case SYS_RPC_NEG_ACKS:
	case SYS_RPC_CHANNEL_NEG_ACKS:
	case SYS_RPC_NUM_NACK_BUFS:
	case SYS_RPC_SANITY_CHECK:
	    status = Rpc_GetStats(command, option, argPtr);
	    break;
	case SYS_PROC_MIGRATION: {
	    switch(option) {
		/*
		 * The first two are for backward compatibility.
		 */
		case SYS_PROC_MIG_ALLOW: 
		case SYS_PROC_MIG_REFUSE: {
		    register Proc_ControlBlock *procPtr;
		    procPtr = Proc_GetEffectiveProc();
		    if (procPtr->effectiveUserID != 0) {
			status = GEN_NO_PERMISSION;
		    } else {
			/*
			 * This part is simplified for now.
			 */
			if (option == SYS_PROC_MIG_REFUSE) {
			    proc_AllowMigrationState &= ~PROC_MIG_IMPORT_ALL;
			} else {
#ifdef SPRITED_MIGRATION
			    proc_AllowMigrationState |= PROC_MIG_IMPORT_ALL;
#else
			    status = GEN_NOT_IMPLEMENTED;
#endif
			}
		    }
		}
		break;

		case SYS_PROC_MIG_SET_STATE: {
#ifndef SPRITED_MIGRATION
		    status = GEN_NOT_IMPLEMENTED;
#else
		    register Proc_ControlBlock *procPtr;
		    int arg;

		    procPtr = Proc_GetEffectiveProc();
		    if (procPtr->effectiveUserID != 0) {
			status = GEN_NO_PERMISSION;
		    } else {
			status = Vm_CopyIn(sizeof(int), argPtr, (Address)&arg);
			if (status == SUCCESS) {
			    proc_AllowMigrationState = arg;
			}
		    }
#endif /* SPRITED_MIGRATION */
		}
		break;

		/*
		 * Also obsolete, here for backward compatibility for a while.
		 */
	        case SYS_PROC_MIG_GET_STATUS: {
		    if (argPtr != (Address) NIL) {
			/*
			 * This part is simplified for now.
			 */
			int refuse;
			if (proc_AllowMigrationState & PROC_MIG_IMPORT_ALL) {
			    refuse = 0;
			} else {
			    refuse = 1;
			}
			status = Vm_CopyOut(sizeof(Boolean),
					    (Address)&refuse,
					    argPtr);
		    } else {
			status = GEN_INVALID_ARG;
		    }
		}
		break;
	        case SYS_PROC_MIG_GET_STATE: {
		    if (argPtr != (Address) NIL) {
			status = Vm_CopyOut(sizeof(Boolean),
					    (Address)&proc_AllowMigrationState,
					    argPtr);
		    } else {
			status = GEN_INVALID_ARG;
		    }
		}
		break;
	        case SYS_PROC_MIG_GET_VERSION: {
		    if (argPtr != (Address) NIL) {
			status = Vm_CopyOut(sizeof(int),
					    (Address)&proc_MigrationVersion,
					    argPtr);
		    } else {
			status = GEN_INVALID_ARG;
		    }
		}
		break;
	        case SYS_PROC_MIG_SET_VERSION: {
#ifndef SPRITED_MIGRATION
		    status = GEN_NOT_IMPLEMENTED;
#else
		    register Proc_ControlBlock *procPtr;
		    int arg;

		    procPtr = Proc_GetEffectiveProc();
		    if (procPtr->effectiveUserID != 0) {
			status = GEN_NO_PERMISSION;
		    } else {
			status = Vm_CopyIn(sizeof(int), argPtr, (Address)&arg);
			if (status == SUCCESS && arg >= 0) {
			    proc_MigrationVersion = arg;
			} else if (status == SUCCESS) {
			    status = GEN_INVALID_ARG;
			}
		    }
#endif /* SPRITED_MIGRATION */
		}
		break;
		case SYS_PROC_MIG_SET_DEBUG: {
		    int arg;
		    status = Vm_CopyIn(sizeof(int), argPtr, (Address)&arg);
		    if (status == SUCCESS && arg >= 0) {
			proc_MigDebugLevel = arg;
		    } else if (status == SUCCESS) {
			status = GEN_INVALID_ARG;
		    }
		}
		break;

		case SYS_PROC_MIG_GET_STATS: {
		    status = Proc_MigGetStats(argPtr);
		    break;
		}
		case SYS_PROC_MIG_RESET_STATS: {
#ifdef SPRITED_MIGRATION
		    status = Proc_MigResetStats();
#else 
		    status = GEN_NOT_IMPLEMENTED;
#endif
		    break;
		}
		default:{
		    status = GEN_INVALID_ARG;
		}
		break;
	    }
	    break;
	}

	case SYS_PROC_SERVERPROC_TIMES: {
	    Proc_ServerProcTimes();
	    break;
	}
	
#if 0
	case SYS_PROC_TRACE_STATS: {
	    switch(option) {
		case SYS_PROC_TRACING_PRINT:
		    printf("%s %s\n", "Warning:",
			    "Printing of proc trace records not implemented.");
		    break;
		case SYS_PROC_TRACING_ON:
		    Proc_MigrateStartTracing();
		    break;
		case SYS_PROC_TRACING_OFF:
		    proc_DoTrace = FALSE;
		    break;
		default:
		    /*
		     * The default is to copy 'option' trace records.
		     */
		    status = Trace_Dump(proc_TraceHdrPtr, option, argPtr);
		    break;
	    }
	    break;
	}
#endif /* 0 */
	case SYS_FS_PREFIX_STATS: {
	    status = Fsprefix_Dump(option, argPtr);
	    break;
	}
	case SYS_FS_PREFIX_EXPORT: {
	    status = Fsprefix_DumpExport(option, argPtr);
	    break;
	}
	case SYS_SYS_CALL_STATS_ENABLE: {
	    sys_CallProfiling = option;
	    break;
	}
	case SYS_SYS_CALL_STATS: {
	    status = Sys_OutputNumCalls(option, argPtr);
	    break;
	}
#if 0
	case SYS_NET_GET_ROUTE: {
	    status = Net_IDToRouteStub(option, sizeof(Net_RouteInfo), argPtr);
	    break;
	}
	case SYS_NET_ETHER_STATS: {
	    Net_Stats	stats;
	    status = Net_GetStats(NET_NETWORK_ETHER, &stats);
	    status = Vm_CopyOut(sizeof(Net_EtherStats),
				(Address)&stats.ether, (Address)argPtr);
	    break;
	}
#endif /* 0 */
	    /* 
	     * XXX this is something of a hack.  It doesn't support
	     * multiple network types and needs redoing.
	     */
	case SYS_NET_GEN_STATS: {
	    Net_Stats	stats;
	    status = Net_GetStats(NET_NETWORK_ETHER, &stats);
	    status = Vm_CopyOut(sizeof(Net_GenStats),
				(Address)&stats.generic, (Address)argPtr);
	    break;
	}
#if 0
	case SYS_DISK_STATS: {
	    int			count;
	    Sys_DiskStats	*statArrPtr;

	    if ((option < 0) || (option > 10000)) {
		status = GEN_INVALID_ARG;
	    } else { 
		statArrPtr = (Sys_DiskStats *)
					ckalloc(sizeof(Sys_DiskStats) * option);
		count = Dev_GetDiskStats(statArrPtr, option);
		status = Vm_CopyOut(sizeof(Sys_DiskStats) * count, 
				    (Address)statArrPtr, (Address)argPtr);
		ckfree((Address) statArrPtr);
	    }
	    break;
	}
	case SYS_LOCK_STATS: {
	    status = Sync_GetLockStats(option, argPtr);
	    break;
	}
	case SYS_LOCK_RESET_STATS: {
	    status = Sync_ResetLockStats();
	    break;
	}
	case SYS_INST_COUNTS: {
#ifdef spur
	    Mach_InstCountInfo	info[MACH_MAX_INST_COUNT];
	    Mach_GetInstCountInfo(info);
	    Vm_CopyOut(sizeof(info), (Address) info, 
		argPtr);
	    status = SUCCESS;
#else
	    status = GEN_NOT_IMPLEMENTED;
#endif
	    break;
	}
	case SYS_RESET_INST_COUNTS: {
#ifdef spur
	    bzero(mach_InstCount, sizeof(mach_InstCount));
	    status = SUCCESS;
#else
	    status = GEN_NOT_IMPLEMENTED;
#endif
	    break;
	}
	case SYS_RECOV_STATS: {
	    status = Recov_GetStats(option, argPtr);
	    break;
	}
	case SYS_RECOV_PRINT: {
	    Recov_ChangePrintLevel(option);
	    status = SUCCESS;
	    break;
	}
	case SYS_RECOV_ABS_PINGS: {
	    recov_AbsoluteIntervals = option;
	    status = SUCCESS;
	    break;
	}
	case SYS_FS_RECOV_INFO: {
	    int		length;
	    Address	resultPtr;
	    int		lengthNeeded;

	    resultPtr = argPtr;
	    /* option is actually an in/out param */
	    status = Vm_CopyIn(sizeof (int), (Address) option,
		    (Address) &length);
	    if (status != SUCCESS) {
		break;
	    }
	    if (length != 0 && (resultPtr == (Address) NIL || resultPtr ==
		    (Address) 0 || resultPtr == (Address) USER_NIL)) {
		status = GEN_INVALID_ARG;
		break;
	    }
	    if (length > 0) {
		resultPtr = (Address) ckalloc(length);
	    } else {
		resultPtr = (Address) NIL;
	    }
	    status = Fsutil_FsRecovInfo(length,
		    (Fsutil_FsRecovNamedStats *) resultPtr, &lengthNeeded);
	    if (status != SUCCESS) {
		if (resultPtr != (Address) NIL) {
		    ckfree(resultPtr);
		}
		break;
	    }
	    status = Vm_CopyOut(length, resultPtr, argPtr);
	    if (status != SUCCESS) {
		if (resultPtr != (Address) NIL) {
		    ckfree(resultPtr);
		}
		break;
	    }
	    status = Vm_CopyOut(sizeof (int), (Address) &lengthNeeded,
		    (Address) option);
	    if (resultPtr != (Address) NIL) {
		ckfree(resultPtr);
	    }
	    break;
	}
	case SYS_RECOV_CLIENT_INFO: {
	    int		length;
	    Address	resultPtr;
	    int		lengthNeeded;

	    resultPtr = argPtr;
	    /* option is actually an in/out param */
	    status = Vm_CopyIn(sizeof (int), (Address) option,
		    (Address) &length);
	    if (status != SUCCESS) {
		break;
	    }
	    if (length != 0 && (resultPtr == (Address) NIL || resultPtr ==
		    (Address) 0 || resultPtr == (Address) USER_NIL)) {
		status = GEN_INVALID_ARG;
		break;
	    }
	    if (length > 0) {
		resultPtr = (Address) ckalloc(length);
	    } else {
		resultPtr = (Address) NIL;
	    }
	    status = Recov_DumpClientRecovInfo(length, resultPtr,
		    &lengthNeeded);
	    if (status != SUCCESS) {
		if (resultPtr != (Address) NIL) {
		    ckfree(resultPtr);
		}
		break;
	    }
	    status = Vm_CopyOut(length, resultPtr, argPtr);
	    if (status != SUCCESS) {
		if (resultPtr != (Address) NIL) {
		    ckfree(resultPtr);
		}
		break;
	    }
	    status = Vm_CopyOut(sizeof (int), (Address) &lengthNeeded,
		    (Address) option);
	    if (resultPtr != (Address) NIL) {
		ckfree(resultPtr);
	    }
	    break;
	}
	case SYS_RPC_SERVER_TRACE:
	    if (option == TRUE) {
		Rpc_OkayToTrace(TRUE);
	    } else  {
		Rpc_OkayToTrace(FALSE);
	    }
	    status = SUCCESS;
		
	    break;
	case SYS_RPC_SERVER_FREE:
	    Rpc_FreeTraces();
	    status = SUCCESS;

	    break;
	case SYS_RPC_SERVER_INFO: {
	    int		length;
	    Address	resultPtr;
	    int		lengthNeeded;

	    resultPtr = argPtr;
	    /* option is actually an in/out param */
	    status = Vm_CopyIn(sizeof (int), (Address) option,
		    (Address) &length);
	    if (status != SUCCESS) {
		break;
	    }
	    if (length != 0 && (resultPtr == (Address) NIL || resultPtr ==
		    (Address) 0 || resultPtr == (Address) USER_NIL)) {
		status = GEN_INVALID_ARG;
		break;
	    }
	    if (length > 0) {
		resultPtr = (Address) ckalloc(length);
	    } else {
		resultPtr = (Address) NIL;
	    }
	    status = Rpc_DumpServerTraces(length,
		    (RpcServerUserStateInfo *)resultPtr, &lengthNeeded);
	    if (status != SUCCESS) {
		if (resultPtr != (Address) NIL) {
		    ckfree(resultPtr);
		}
		break;
	    }
	    status = Vm_CopyOut(length, resultPtr, argPtr);
	    if (status != SUCCESS) {
		if (resultPtr != (Address) NIL) {
		    ckfree(resultPtr);
		}
		break;
	    }
	    status = Vm_CopyOut(sizeof (int), (Address) &lengthNeeded,
		    (Address) option);
	    if (resultPtr != (Address) NIL) {
		ckfree(resultPtr);
	    }
	    break;
	}
	case SYS_START_STATS: {
	    /* schedule the stuff */
	    Dev_StartIOStats();
	    Sched_StartSchedStats();
	    status = SUCCESS;
	    break;
	}
	case SYS_END_STATS: {
	    /* deschedule the stuff */
	    Dev_StopIOStats();
	    Sched_StopSchedStats();
	    status = SUCCESS;
	    break;
	}
#endif /* 0 */
	default:
	    status = GEN_INVALID_ARG;
	    break;
    }
    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * GetSchedStats --
 *
 *	Collect all the information for Sched_Instrument.
 *
 * Results:
 *	Fills in the given Sched_Instrument struct.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetSchedStats(statsPtr)
    Sched_Instrument *statsPtr;	/* OUT: numbers to fill in */
{
    Time curTime;
    struct host_load_info loadInfo;
    mach_msg_type_number_t infoCount;
    kern_return_t kernStatus;
    int i;
    int numLoadNums = 3;	/* number of load numbers */

    /* 
     * Get the system load.
     */

    infoCount = HOST_LOAD_INFO_COUNT;
    kernStatus = host_info(mach_host_self(), HOST_LOAD_INFO,
			   (host_info_t)&loadInfo, &infoCount);
    if (kernStatus != KERN_SUCCESS) {
	printf("GetSchedStats: can't get host info: %s\n",
	       mach_error_string(kernStatus));
    } else if (infoCount != HOST_LOAD_INFO_COUNT) {
	printf("GetSchedStats: expected %d words, got %d.\n",
	       HOST_LOAD_INFO_COUNT, infoCount);
	kernStatus = KERN_FAILURE;
    }
    if (kernStatus != KERN_SUCCESS) {
	bzero(loadInfo.avenrun, sizeof(loadInfo.avenrun));
    }
	       
    for (i = 0; i < numLoadNums; ++i) {
	statsPtr->avenrun[i] = (float)loadInfo.avenrun[i] / LOAD_SCALE;
    }

    /*
     * Get the time since the last console input.  If no input has been
     * received, dev_LastConsoleInput will be 0, so set it to the current
     * time.
     *
     * Note: dev_LastConsoleInput isn't set during initialization, to avoid
     * a potential ordering problem between the timer and dev modules.
     */
    if (dev_LastConsoleInput.seconds == 0) {
	Timer_GetTimeOfDay(&dev_LastConsoleInput,
			   (int *) NIL, (Boolean *) NIL);
    }
    Timer_GetTimeOfDay(&curTime, (int *) NIL, (Boolean *) NIL);
    Time_Subtract(curTime, dev_LastConsoleInput,
		  &statsPtr->noUserInput);
}


/*
 *----------------------------------------------------------------------
 *
 * KillUserProcesses --
 *
 *	Kill all user processes.
 *
 * Results:
 *	Returns the number of remaining alive user processes.
 *
 * Side effects:
 *	All user processes are signaled.
 *
 *----------------------------------------------------------------------
 */

static int
KillUserProcesses()
{
    return Proc_KillAllProcesses(TRUE);
}


/*
 *----------------------------------------------------------------------
 *
 * KillServerProcesses --
 *
 *	Kill all processes.
 *
 * Results:
 *	Returns the number of remaining alive processes.  (This should only
 *	be server processes, since the user processes were already killed.) 
 *
 * Side effects:
 *	All processes are signaled.
 *
 *----------------------------------------------------------------------
 */

static int
KillServerProcesses()
{
    return Proc_KillAllProcesses(FALSE);
}


/*
 *----------------------------------------------------------------------
 *
 * Sys_GetMachineInfoStub --
 *
 *	Return information about the machine type, etc.
 *
 * Results:
 *	Returns KERN_SUCCESS.  Fills in the Sprite status code and "pending 
 *	signals" flag.
 *
 * Side effects:
 *	Writes the machine information to the user's buffer.
 *
 *----------------------------------------------------------------------
 */

kern_return_t
Sys_GetMachineInfoStub(serverPort, size, userBuffer, statusPtr,
		       sigPendingPtr)
    mach_port_t	serverPort;	/* server request port */
    int size;			/* bytes in user's buffer */
    vm_address_t userBuffer;	/* user's buffer */
    ReturnStatus *statusPtr;	/* OUT: Sprite return status */
    boolean_t	*sigPendingPtr;	/* OUT: there is a pending signal */
{
    Sys_MachineInfo info;

#ifdef lint
    serverPort = serverPort;
#endif

    if (size > sizeof(Sys_MachineInfo)) {
	size = sizeof(Sys_MachineInfo);
    }
    info.architecture = Sys_GetMachineArch();
    info.type = Sys_GetMachineType();
    info.processors = Sys_GetNumProcessors();

    *statusPtr = Vm_CopyOut(size, (Address)&info, (Address)userBuffer);
    *sigPendingPtr = Sig_Pending(Proc_GetCurrentProc());

    return KERN_SUCCESS;
}


/*
 *----------------------------------------------------------------------
 *
 * Sys_OutputNumCalls --
 *
 *	Copy the number of invocations of system calls into user space.
 *	Takes an argument, the number of calls to copy, which indicates the
 *	size of the user's buffer.  This is protection against any
 *	inconsistency between the kernel and user program's ideas of how
 *	many system calls there are.  An argument of 0 calls indicates that
 *	the statistics should be reset to 0.
 *
 * Results:
 *	The return status from Vm_CopyOut is returned.
 *
 * Side effects:
 *	Data is copied into user space.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
Sys_OutputNumCalls(numToCopy, buffer)
    int numToCopy;	/* number of system calls statistics to copy */
    Address buffer;
{
    ReturnStatus status = SUCCESS;

    if (numToCopy == 0) {
	bzero(sys_CallCounts, SYS_NUM_CALLS * sizeof(Sys_CallCount));
	Proc_ZeroServerProcTimes();
    } else {
	status = Vm_CopyOut(numToCopy * sizeof(Sys_CallCount),
			    (Address)sys_CallCounts,
			    buffer);
    }
    return(status);
}
