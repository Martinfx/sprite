/*
 * procMigrate.h --
 *
 *	Declarations of procedures and constants for process migration. 
 *
 * Copyright 1986, 1988 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 *
 * $ProcMigrate: proto.h,v 1.4 86/03/20 14:00:21 andrew Exp $ SPRITE (Berkeley)
 */

#ifndef _PROCMIGRATE
#define _PROCMIGRATE

#include "proc.h"
#include "trace.h"
#include "sys.h"
#include "net.h"

/*
 * Flags for the migFlags field in a PCB.
 * 	PROC_EVICTING		- [defined in user/proc.h for now]
 * 				  Process in the middle of eviction
 *   	PROC_WAS_EVICTED	- Process was evicted at some time, so
 * 				  the record of the time it used prior
 * 				  to eviction is relevant.
 * 			
 */
#define PROC_WAS_EVICTED	0x2

/*
 * Define a macro to get a valid PCB for a migrated process.  This gets the
 * PCB corresponding to the process ID, and if it is a valid PCB the macro
 * then checks to make sure the process is migrated and from the specified
 * host.
 */

#define PROC_GET_MIG_PCB(pid, procPtr, hostID) 		    	\
	procPtr = Proc_LockPID(pid);	 			\
	if (procPtr != (Proc_ControlBlock *) NIL && 		\
	        (!(procPtr->genFlags & PROC_FOREIGN) || 	\
	        procPtr->peerHostID != remoteHostID)) {		\
	    Proc_Unlock(procPtr);				\
 	    procPtr = (Proc_ControlBlock *) NIL; 		\
	}

/*
 * Structure to contain information for the arguments to a system call
 * for a migrated process.    The size is the size of the argument passed
 * to or from the other node.  The disposition is SYS_PARAM_IN and/or
 * SYS_PARAM_OUT.
 */

typedef struct {
    int size;
    int disposition;
} Proc_ParamInfo;

/*
 * Define a simple buffer for passing both buffer size and pointer in
 * a single argument.  (This simplifies the case when the buffer is NIL).
 */

typedef struct {
    int size;
    Address ptr;
} Proc_MigBuffer;

/* 
 * Generic information sent when migrating a system call back to the
 * home machine.  The processID transferred is the ID of the process
 * on the machine servicing the RPC.
 */
	
typedef struct {
    Proc_PID			processID;
    int			 	callNumber;
    Boolean			parseArgs;
    int				numArgs;
    int				replySize;
    Proc_ParamInfo		info[SYS_MAX_ARGS];
} Proc_RemoteCall;

/*
 * Declare variables and constants for instrumentation.  First,
 * declare variables for the trace package.  These are followed by
 * structures that are passed into the trace package.  Each trace
 * record contains the process ID of the process being operated upon,
 * whether the operation is done for a process on its home node or a
 * remote one, and either a system call number and ReturnStatus or a
 * migration meta-command such as transferring state.
 */

extern Trace_Header proc_TraceHeader;
extern Trace_Header *proc_TraceHdrPtr;
#define PROC_NUM_TRACE_RECS 500

/*
 * "Events" for the trace package.
 *
 *	PROC_MIGTRACE_BEGIN_MIG 	- starting to transfer a process
 *	PROC_MIGTRACE_END_MIG 		- completed transferring a process
 *	PROC_MIGTRACE_COMMAND  	- a particular transfer operation
 *	PROC_MIGTRACE_CALL		- a migrated system call
 *	
 */

#define PROC_MIGTRACE_BEGIN_MIG 	0
#define PROC_MIGTRACE_END_MIG 		1
#define PROC_MIGTRACE_COMMAND  	2
#define PROC_MIGTRACE_CALL		3
#define PROC_MIGTRACE_MIGTRAP		4

typedef struct {
    int callNumber;
    ReturnStatus status;
} Proc_SysCallTrace;

typedef struct {
    int type;
    ClientData data;
} Proc_CommandTrace;


typedef struct {
    Proc_PID processID;
    int flags;
    union {
	Proc_SysCallTrace call;
	Proc_CommandTrace command;
	int filler;
    } info;
} Proc_TraceRecord;
	
/*
 * Flags for Proc_TraceRecords:
 *
 * 	PROC_MIGTRACE_START - start of an RPC
 *	PROC_MIGTRACE_HOME  - operation is for a process on its home machine
 *
 * Both of these flags are boolean, so absence of a flag implies its
 * opposite (end of an RPC, or that the operation is for a foreign process).
 */

#define PROC_MIGTRACE_START	0x01
#define PROC_MIGTRACE_HOME	0x02

/*
 * Define the statistics "version".  This is used to make sure we're
 * gathering consistent sets of statistics.  It's stored in the kernel as
 * a static variable so it can be changed with adb or the debugger if
 * need be.  It's copied into a structure at initialization time.
 */
#ifndef PROC_MIG_STATS_VERSION
#define PROC_MIG_STATS_VERSION 1002
#endif /* PROC_MIG_STATS_VERSION */

/*
 * Define a structure to keep track of statistics.
 * Times are kept in terms of hundreds of milliseconds.
 */

#define PROC_MIG_TIME_FOR_STATS(time) \
      ((time).seconds * 10 + ((time).microseconds + 50000) / 100000)

typedef struct {
    int			evictions;	/* Number of processes evicted
				     	   from this host */
    int			pagesWritten;	/* Number of pages flushed as a
					   result of migration */
    int			rpcKbytes; 	/* Total number of Kbytes sent during
					   migration. */
    int 		timeToMigrate;	/* Cumulative time to export
					   running processes */
    int 		timeToExec;	/* Cumulative time to do remote
					   exec's */
    int 		timeToEvict;	/* Cumulative time to evict
					   processes, individually. */
    int 		totalEvictTime;	/* Cumulative time to evict
					   processes, from start of eviction
					   request to completion of last
					   eviction. */
    int 		totalCPUTime;   /* Cumulative time used by all
					   processes belonging to this host. */
    int 		remoteCPUTime;  /* Cumulative time used by all
					   processes belonging to this host,
					   while executing remotely. */
    int 		evictionCPUTime;/* Cumulative time used by all
					   processes subsequent to first
					   eviction. */
} Proc_MigVarStats;

typedef struct {
    int			statsVersion;   /* Used to distinguish old structures.
					   */
    int			foreign; 	/* Number of foreign processes on
				     	   this machine */
    int			remote;		/* Number of processes belonging
			     	   	   to this host, running elsewhere */
    int			exports;	/* Number of times we have exported
			     		   processes */
    int			execs;		/* Number of times these were remote
			     	   	   execs */
    int			imports;	/* Number of times we have imported
			     	   	   processes */
    int			errors;		/* Number of times migration has
					   failed */
    int			returns;	/* Number of times we have had our own
					   process migrate back to us,
					   including evictions */
    int			evictionsToUs;	/* Number of times we have had our own
					   process evicted. */
    int			hostCounts[NET_NUM_SPRITE_HOSTS];
    					/* Array of counts of
					   migration to each host */
    int			migrationsHome; /* Number of times processes migrate
					   home, excluding evictions. */
    int			evictCalls; 	/* Number of times user-level daemon
					   requested evictions. */
    int			evictsNeeded; 	/* Number of times requests resulted
					   in >= 1 evictions. */
    int			evictionsInProgress;
    					/* Number of processes currently
					   being evicted. */
    int			processes;	/* Number of exited processes
					   belonging to this host (used
					   for averaging CPU times). */
    Proc_MigVarStats	varStats; 	/* Other stats (see above) counted
					   both normally and as
					   sum-of-squares. */
    Proc_MigVarStats	squared; 	/* Sum-of-squares. */
					   
} Proc_MigStats;


/*
 * Macros to manipulate this structure using a monitor.
 */
#define PROC_MIG_INC_STAT(stat) \
	Proc_MigAddToCounter(1, &proc_MigStats.stat, (int *) NIL)
#define PROC_MIG_DEC_STAT(stat) \
	Proc_MigAddToCounter(-1, &proc_MigStats.stat, (int *) NIL)




/*
 * Define a structure for passing information via callback for killing
 * a migrated process.  [Not used yet, but potentially.]
 */
typedef struct Proc_DestroyMigProcData {
    Proc_ControlBlock *procPtr;		/* local copy of process to kill */
    ReturnStatus status;		/* status to return when it exits */
} Proc_DestroyMigProcData;

/*
 * Information for encapsulating process state.
 */

/*
 * Identifiers to match encapsulated states and modules.
 * Make sure to update PROC_MIG_NUM_CALLBACKS if one is added!
 */
typedef enum {
    PROC_MIG_ENCAP_PROC,
    PROC_MIG_ENCAP_VM,
    PROC_MIG_ENCAP_FS,
    PROC_MIG_ENCAP_MACH,
    PROC_MIG_ENCAP_PROF,
    PROC_MIG_ENCAP_SIG,
    PROC_MIG_ENCAP_EXEC,
} Proc_EncapToken;

#define PROC_MIG_NUM_CALLBACKS 7

/*
 * Each module that participates has a token defined for it.
 * It also provides routines to encapsulate and deencapsulate data,
 * as well as optional routines that may be called prior to migration
 * and subsequent to migration.  This structure is passed around to
 * other modules performing encapsulation.
 */
typedef struct {
    Proc_EncapToken	token;		/* info about encapsulated data */
    int			size;		/* size of encapsulated data */
    ClientData		data;		/* for use by encapsulator */
    int			special;	/* indicates special action required */
    ClientData		specialToken;	/* for use during special action */
    int			processed;	/* indicates this module did possibly
					   destructive encapsulation operation
					   and should be called to clean up
					   on failure */
					   
} Proc_EncapInfo;
  
    
    


/*
 * External declarations of variables and procedures.
 */
extern int proc_MigDebugLevel;		/* amount of debugging info to print */
extern int proc_MigrationVersion;	/* to distinguish incompatible
					   versions of migration */
extern Boolean proc_DoTrace;		/* controls amount of tracing */
extern Boolean proc_DoCallTrace;	/* ditto */
extern Boolean proc_MigDoStats;		/* ditto */
extern Boolean proc_KillMigratedDebugs;	/* kill foreign processes instead
					   of putting them in the debugger */
extern int proc_AllowMigrationState;	/* how much migration to permit */
extern Proc_MigStats proc_MigStats;	/* migration statistics */

/*
 * Functions for process migration.  [Others should be moved here.]
 */
extern void Proc_ResumeMigProc();
extern void Proc_DestroyMigratedProc();

extern ReturnStatus Proc_WaitForHost();
extern ReturnStatus Proc_WaitForMigration();

extern ReturnStatus Proc_RpcMigCommand();
extern ReturnStatus Proc_RpcRemoteCall();


#endif /* _PROCMIGRATE */
