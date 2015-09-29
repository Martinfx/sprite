/*
 * migrate.h --
 *
 *	Declarations of types for process migration used only by the proc
 * 	module.
 *
 * Copyright 1986, 1988, 1989 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 *
 * $Header: /cdrom/src/kernel/Cvsroot/kernel/proc/migrate.h,v 9.6 91/11/15 21:02:40 kupfer Exp $ SPRITE (Berkeley)
 */

#ifndef _MIGRATE
#define _MIGRATE

/*
 * Parameters for a remote Proc_Wait.
 */

typedef struct {
    Proc_PID 	pid;	    /* ID of process doing the wait */
    int 	numPids;    /* number of pids in array */
    Boolean 	flags;      /* Flags to Proc_Wait. */
    int 	token;      /* token to use for remote notify call */
} ProcRemoteWaitCmd;

/*
 * Types of commands passed related to migration:
 *
 * PROC_MIGRATE_CMD_INIT	- Initiate a migration.
 * PROC_MIGRATE_CMD_ENTIRE	- Process control block.
 * PROC_MIGRATE_CMD_UPDATE	- Update user information that may change, 
 *		  		  such as priorities or IDs.
 * PROC_MIGRATE_CMD_CALLBACK 	- Callback by another module to transfer
 *				  additional encapsulated state.
 * PROC_MIGRATE_CMD_DESTROY	- Destroy a migrated process due to an error
 *				  during migration.
 * PROC_MIGRATE_CMD_RESUME	- Resume a migrated process after transfer.
 * PROC_MIGRATE_CMD_SUSPEND	- Remote process has suspended or continued.
 */

#define PROC_MIGRATE_CMD_INIT		0
#define PROC_MIGRATE_CMD_ENTIRE		1
#define PROC_MIGRATE_CMD_UPDATE		2
#define PROC_MIGRATE_CMD_CALLBACK	3
#define PROC_MIGRATE_CMD_DESTROY	4
#define PROC_MIGRATE_CMD_RESUME		5
#define PROC_MIGRATE_CMD_SUSPEND	6

#define PROC_MIGRATE_CMD_NUM_TYPES	7
    
/* 
 * Data sent to the other host related to migration.  This is done
 * either to perform the entire state transfer or parts (see above).
 * It always specifies a processID, which may be NIL to indicate a new
 * process is to be created (only during PROC_MIGRATE_INIT).
 */

typedef struct {
    Proc_PID		remotePid; /* Process ID on other host. */
    int			command;   /* What to do. */
    int			totalSize; /* Total size of command buffer. */
    int			offset;	   /* Offset into total buffer. */
} ProcMigCmd;
    

/*
 * Parameters when initiating migration to another machine.  This is done
 * to check permission as well as incompatible versions.
 */

typedef struct {
    int 	version;    /* Migration version number of machine starting
			     * migration (should come first) */
    Proc_PID 	processID;    /* ID of process being migrated */
    int		userID;	    /* userID of process being migrated */
    int		clientID;   /* ID of host issuing command */
} ProcMigInitiateCmd;

/*
 * Number of times to try an RPC before giving up due to RPC_TIMEOUT, while
 * waiting for the host to come up.
 */

#define PROC_MAX_RPC_RETRIES 2

/*
 * Parameter to ProcRecordUsage:
 * 	PROC_MIG_USAGE_REMOTE_CPU 	- time used by remote processes
 * 	PROC_MIG_USAGE_TOTAL_CPU	- time used by all processes
 * 	PROC_MIG_USAGE_POST_EVICTION	- time used subsequent to 1st
 * 					  eviction.
 *
 */
typedef enum {
    PROC_MIG_USAGE_REMOTE_CPU, 	
    PROC_MIG_USAGE_TOTAL_CPU,
    PROC_MIG_USAGE_POST_EVICTION,
} ProcRecordUsageType;


#endif /* _MIGRATE */
