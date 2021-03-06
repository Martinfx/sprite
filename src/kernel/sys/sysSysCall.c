/*
 * sysSyscall.c --
 *
 *	Routines and structs for system calls.  Contains information
 *	about each system call such as the number of arguments and how
 *	to invoke the call for migrated processes.  All local
 *	processes invoke system calls by copying in the arguments from
 *	the user's address space and passing them to the kernel
 *	routine uninterpreted.  When migrated processes invoke system
 *	calls, when possible the arguments are passed to a generic
 *	stub that packages the arguments and sends them to the home
 *	node of the process through RPC.  This file contains
 *	information about the sizes and types of each argument for
 *	those procedures.  The generic stub is called with information
 *	about which system call was invoked and what its arguments consist
 *	of.  The information stored for each argument is described below.
 *
 *	Many system calls, however, are handled exclusively on the
 *	current machine or are processed by special-purpose routines
 *	on the current machine before being sent to the home machine.
 *	In these cases no information about parameter types is kept,
 *	and the procedure is invoked in the same manner as for local
 *	processes.
 *
 *	NOTES ON ADDING SYSTEM CALLS:
 *	   Add an entry for the system call to the two arrays
 *	   declared below, sysCalls and paramsArray.  For sysCalls,
 *	   list the procedures to be invoked, whether to use the
 *	   generic stub (in which case special == FALSE), and the number
 *	   of words passed to the system call.  For paramsArray, if
 *	   special is FALSE, list the type and disposition of each
 *	   parameter.  If special is TRUE, just add a comment as a
 *	   placeholder within the array.  Finally, add an entry in
 *	   procRpc.c for the callback routine corresponding to the new
 *	   procedure (NIL if the call is not migrated).
 *
 * Copyright 1985, 1988 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /cdrom/src/kernel/Cvsroot/kernel/sys/sysSysCall.c,v 9.14 92/06/15 22:29:27 jhh Exp $ SPRITE (Berkeley)";
#endif not lint

#include <sprite.h>
#include <fs.h>
#include <sys.h>
#include <sysInt.h>
#include <dbg.h>
#include <proc.h>
#include <sync.h>
#include <sched.h>
#include <vm.h>
#include <user/vm.h>
#include <rpc.h>
#include <prof.h>
#include <devVid.h>
#include <net.h>
#include <sysSysCall.h>
#include <sysSysCallParam.h>
#include <status.h>
#include <stdio.h>
#include <sysTestCall.h>
#include <user/sys/param.h>

/*
 * Forward declarations to procedures defined in this file:
 */

static int ErrorProc _ARGS_((void));
static ReturnStatus OutputCallTimes _ARGS_((int numToCopy,
					    Address buffer));
static ReturnStatus SysMigCall _ARGS_((Sys_ArgArray args));

#define TMP_EXTERN
#ifdef TMP_EXTERN
extern	int Proc_RemoteExec();
#endif

#ifndef CLEAN
Boolean sysTraceSysCalls = FALSE;
#endif CLEAN

/*
 * For each system call, keep track of:
 *	- which procedure to invoke if the process is local;
 *	- which to invoke if it is remote;
 *	- whether the remoteFunc is "special" and is to be invoked
 *	  without interpreting the arguments, or whether the generic
 *	  stub will be called (passing it information such as the
 *	  number of arguments and the type of system call);
 *	- a pointer to an array of parameter information, which includes
 *	  the types and dispositions of each argument.  For "special"
 *	  routines, this pointer is NIL.  Refer to sysSysCallParam.h for
 *	  documentation on the Sys_CallParam type.
 */

typedef struct {
    ReturnStatus (*localFunc)();  /* procedure to invoke for local processes */
    ReturnStatus (*remoteFunc)(); /* procedure to invoke for migrated procs */
    Boolean special;		  /* whether the remoteFunc is called without
				     passing it additional information */
    int numWords;		  /* The number of 4-byte quantities that must
				     be passed to the system call. */
    Sys_CallParam *paramsPtr;	  /* pointer to parameter information for
				     generic stub to use */
} SysCallEntry;

/*
 * Sys_ParamSizes is an array of sizes corresponding to
 * each system call argument type.  Sys_ParamSizesDecl is used to
 * assign the elements of the array at compile time.  Due to compiler
 * restrictions, sys_ParamSizes needs to be a "pointer" while
 * sys_ParamSizesDecl is an "array".  The argument types are documented
 * in sysSysCallParam.h.
 */

int sys_ParamSizesDecl[] = {
    sizeof(int),			/* SYS_PARAM_INT		*/
    sizeof(char),			/* SYS_PARAM_CHAR		*/
    sizeof(Proc_PID),			/* SYS_PARAM_PROC_PID		*/
    sizeof(Proc_ResUsage),		/* SYS_PARAM_PROC_RES		*/
    sizeof(Sync_Lock),			/* SYS_PARAM_SYNC_LOCK		*/
    sizeof(Fs_Attributes),		/* SYS_PARAM_FS_ATT		*/
    FS_MAX_PATH_NAME_LENGTH,		/* SYS_PARAM_FS_NAME		*/
    sizeof(Time),			/* SYS_PARAM_TIMEPTR		*/
    sizeof(Time) / 2,			/* SYS_PARAM_TIME1		*/
    sizeof(Time) / 2,			/* SYS_PARAM_TIME2		*/
    sizeof(int),			/* SYS_PARAM_VM_CMD		*/
    0, 					/* SYS_PARAM_DUMMY	 	*/
    sizeof(int),			/* SYS_PARAM_RANGE1	 	*/
    sizeof(int),			/* SYS_PARAM_RANGE2		*/
    sizeof(Proc_ControlBlock),		/* SYS_PARAM_PCB		*/
    sizeof(Fs_Device),			/* SYS_PARAM_FS_DEVICE		*/
    sizeof(Proc_PCBArgString),		/* SYS_PARAM_PCBARG		*/
    MAXHOSTNAMELEN,			/* SYS_PARAM_HOSTNAME		*/

};

int *sys_ParamSizes = sys_ParamSizesDecl;

static int ErrorProc()
{
    printf("Warning: Obsolete system call.\n");
    return(GEN_FAILURE);
}

/*
 * sysCalls --
 *
 *	This table is used during a system call trap to branch to the
 *	correct procedure for each system call.  There are two functions,
 *	one if the process is local, the other if the process is an immigrant.
 *	The number of integers (parameters) on the user's stack that have
 *	to be copied to the kernel stack is also listed here.  The last field
 *	of each record is filled in dynamically but initialized here to NIL.
 *
 *	N.B.: The format of this table is relied on to generate the file
 *	Dummy.c, a file with just the declarations of the system calls
 *	and no body.  See the CreateDummy script in src/lib/libc.
 */

#define NILPARM ((Sys_CallParam *) NIL)
#define CAST	(ReturnStatus (*) ())

static SysCallEntry sysCalls[] = {
/*
 *	localFunc		  remoteFunc	   special numWords  NILPARM
 */
/* DON'T DELETE THIS LINE - CreateDummy depends on it */
    Proc_Fork,		       Proc_Fork,	   TRUE,	2,   NILPARM,
    Proc_Exec,		       Proc_Exec,	   TRUE,	5,   NILPARM,
    CAST Proc_Exit,       CAST Proc_Exit,	   TRUE,	1,   NILPARM,
    Sync_WaitTime,	       Sync_WaitTime,	   TRUE,	2,   NILPARM,
    Test_PrintOut,	       Test_PrintOut,      TRUE,       10,   NILPARM,
    Test_GetLine,	       Test_GetLine,   	   TRUE,	2,   NILPARM,
    Test_GetChar,	       Test_GetChar,   	   TRUE,	1,   NILPARM,
    Fs_OpenStub,	       Fs_OpenStub,  	   TRUE,	4,   NILPARM,
    Fs_ReadStub,	       Fs_ReadStub,  	   TRUE, 	4,   NILPARM,
    Fs_WriteStub,	       Fs_WriteStub,  	   TRUE, 	4,   NILPARM,
    Fs_UserClose,	       Fs_UserClose,  	   TRUE, 	1,   NILPARM,
    Fs_RemoveStub,	       Fs_RemoveStub,  	   TRUE,	1,   NILPARM,
    Fs_RemoveDirStub,	       Fs_RemoveDirStub,   TRUE,	1,   NILPARM,
    Fs_MakeDirStub,	       Fs_MakeDirStub,     TRUE,	2,   NILPARM,
    Fs_ChangeDirStub,	       Fs_ChangeDirStub,   TRUE,	1,   NILPARM,
    Proc_Wait,		       Proc_Wait,   	   TRUE,	8,   NILPARM,
    Proc_Detach,	       Proc_DoRemoteCall,  FALSE,	1,   NILPARM,
    Proc_GetIDs,	       Proc_GetIDs,  	   TRUE,	4,   NILPARM,
    Proc_SetIDs,	       Proc_DoRemoteCall,  FALSE,	2,   NILPARM,
    Proc_GetGroupIDs,	       Proc_GetGroupIDs,   TRUE,	3,   NILPARM,
/*
 * Need not be forwarded home because groups only used during FS operations.
 */
    Proc_SetGroupIDs,	       Proc_SetGroupIDs,   TRUE,	2,   NILPARM,
/*
 * Must be forwarded home because can ask about arbitrary process on home node.
 */
    Proc_GetFamilyID,	       Proc_DoRemoteCall,  FALSE,	2,   NILPARM,
    Proc_SetFamilyID,	       Proc_DoRemoteCall,  FALSE,	2,   NILPARM,
    Test_RpcStub,	       Test_RpcStub,   	   TRUE,	4,   NILPARM,
    Sys_StatsStub,	       Sys_StatsStub,      TRUE,	4,   NILPARM,
    Vm_CreateVA,	       Vm_CreateVA,	   TRUE, 	2,   NILPARM,
    Vm_DestroyVA,	       Vm_DestroyVA,	   TRUE, 	2,   NILPARM,
    Sig_UserSend,	       Sig_UserSend,  	   TRUE,	3,   NILPARM,
    Sig_Pause,		       Sig_Pause,          TRUE,	1,   NILPARM,
    Sig_SetHoldMask,	       Sig_SetHoldMask,    TRUE,	2,   NILPARM,
    Sig_SetAction,	       Sig_SetAction,      TRUE,	3,   NILPARM,
    Prof_Start,		       Prof_Start,  	   TRUE,	0,   NILPARM,
    Prof_End,		       Prof_End,  	   TRUE,	0,   NILPARM,
    Prof_DumpStub,	       Prof_DumpStub,  	   TRUE,	1,   NILPARM,
    Vm_Cmd,		       Vm_Cmd,  	   TRUE,	2,   NILPARM,
    Sys_GetTimeOfDay,	       Proc_DoRemoteCall,  FALSE,	3,   NILPARM,
    Sys_SetTimeOfDay,	       Proc_DoRemoteCall,  FALSE,	3,   NILPARM,
    Sys_DoNothing,	       Sys_DoNothing,      TRUE,	0,   NILPARM,
    Proc_GetPCBInfo,	       Proc_GetPCBInfo,    TRUE,	7,   NILPARM,
    Vm_GetSegInfo,	       Vm_GetSegInfo,      TRUE,	4,   NILPARM,
    Proc_GetResUsage,	       Proc_GetResUsage,   TRUE,	2,   NILPARM,
    Proc_GetPriority,	       Proc_GetPriority,   TRUE,	2,   NILPARM,
    Proc_SetPriority,          Proc_SetPriority,   TRUE,	3,   NILPARM,
    Proc_Debug,		       Proc_Debug,   	   TRUE,	5,   NILPARM,
    Proc_Profile,	       Proc_Profile,       TRUE,	6,   NILPARM,
    ErrorProc,		       ErrorProc,   	   TRUE,	2,   NILPARM,
    ErrorProc,	   	       ErrorProc, 	   TRUE,	2,   NILPARM,
    Fs_GetNewIDStub,	       Fs_GetNewIDStub,    TRUE,	2,   NILPARM,
    Fs_GetAttributesStub,      Fs_GetAttributesStub, TRUE,	3,   NILPARM,
    Fs_GetAttributesIDStub,    Fs_GetAttributesIDStub, TRUE,	2,   NILPARM,
    Fs_SetAttributesStub,      Fs_SetAttributesStub, TRUE,	3,   NILPARM,
    Fs_SetAttributesIDStub,    Fs_SetAttributesIDStub, TRUE,	2,   NILPARM,
    Fs_SetDefPermStub,	       Fs_SetDefPermStub,  TRUE,	2,   NILPARM,
    Fs_IOControlStub,	       Fs_IOControlStub,   TRUE,	6,   NILPARM,
    Dev_VidEnable,	       Proc_DoRemoteCall,  FALSE,	1,   NILPARM,
    /*
     * The following ErrorProc listings correspond to obsolete
     * environment-related procedures.
     */
    ErrorProc,       		ErrorProc,  TRUE,	2,   NILPARM,
    ErrorProc,     		ErrorProc,  TRUE,	2,   NILPARM,
    ErrorProc,    		ErrorProc,  TRUE,	2,   NILPARM,
    ErrorProc,  		ErrorProc,   TRUE,	4,   NILPARM,
    ErrorProc,   		ErrorProc,   TRUE,	2,   NILPARM,
    ErrorProc,      		ErrorProc,  TRUE,	0,   NILPARM,

    Sync_SlowLockStub,	       Sync_SlowLockStub,  TRUE,	1,   NILPARM,
    Sync_SlowWaitStub,	       Sync_SlowWaitStub,  TRUE,	3,   NILPARM,
    Sync_SlowBroadcastStub,    Sync_SlowBroadcastStub,  TRUE,	2,   NILPARM,
    Vm_PageSize,		Vm_PageSize,  	   TRUE,	1,   NILPARM,
    Fs_HardLinkStub,		Fs_HardLinkStub,   TRUE,	2,   NILPARM,
    Fs_RenameStub,		Fs_RenameStub, 	   TRUE,	2,   NILPARM,
    Fs_SymLinkStub,		Fs_SymLinkStub,    TRUE,	3,   NILPARM,
    Fs_ReadLinkStub,		Fs_ReadLinkStub,   TRUE,	4,   NILPARM,
    Fs_CreatePipeStub,		Fs_CreatePipeStub, TRUE,	2,   NILPARM,
    VmMach_MapKernelIntoUser,	Proc_RemoteDummy, FALSE,	4,   NILPARM,
    Fs_AttachDiskStub,		Proc_DoRemoteCall, FALSE,	3,   NILPARM,
    Fs_SelectStub,		Fs_SelectStub, 	   TRUE,	6,   NILPARM,
    CAST Sys_Shutdown,		Sys_Shutdown, 	   TRUE,	2,   NILPARM,
    Proc_Migrate,		Proc_DoRemoteCall, FALSE,	2,   NILPARM,
    Fs_MakeDeviceStub,		Proc_DoRemoteCall, FALSE,	3,   NILPARM,
    Fs_CommandStub,		Fs_CommandStub,    TRUE,	3,   NILPARM,
    ErrorProc,       		ErrorProc, 	   TRUE,	2,   NILPARM,
    Sys_GetMachineInfo,       	Sys_GetMachineInfo,  TRUE,	3,   NILPARM,
    Net_InstallRouteStub, 	Net_InstallRouteStub, TRUE, 	6,   NILPARM,
    Fs_ReadVectorStub, 		Fs_ReadVectorStub, TRUE, 	4,   NILPARM,
    Fs_WriteVectorStub, 	Fs_WriteVectorStub, TRUE, 	4,   NILPARM,
    Fs_CheckAccess,     	Fs_CheckAccess, 	TRUE,	3,   NILPARM,
    Proc_GetIntervalTimer,	Proc_GetIntervalTimer, 	TRUE,	2,   NILPARM,
    Proc_SetIntervalTimer,	Proc_SetIntervalTimer, 	TRUE,	3,   NILPARM,
    Fs_FileWriteBackStub,	Fs_FileWriteBackStub, TRUE,	4,   NILPARM,
    Proc_ExecEnv,		Proc_ExecEnv,	   TRUE,	4,   NILPARM,
    Fs_SetAttrStub,		Fs_SetAttrStub,	   TRUE,	4,   NILPARM,
    Fs_SetAttrIDStub,		Fs_SetAttrIDStub,   TRUE,	3,   NILPARM,
    Proc_GetHostIDs,		Proc_GetHostIDs,   TRUE,	2,   NILPARM,
    Sched_IdleProcessor,	Sched_IdleProcessor,  TRUE,	1,   NILPARM,
    Sched_StartProcessor,	Sched_StartProcessor,   TRUE,	1,   NILPARM,
#if 0
    Mach_GetNumProcessors,	Mach_GetNumProcessors,   TRUE,	1,   NILPARM,
#else
    0,                          0,                      TRUE,   1,   NILPARM,
#endif
    Prof_Profil,                Prof_Profil,            TRUE,   4,   NILPARM,
    Proc_RemoteExec,		Proc_RemoteExec,   TRUE,	4,   NILPARM,
    Sys_GetMachineInfoNew,	Sys_GetMachineInfoNew,   TRUE,	2,   NILPARM,
    Vm_Mmap,			Vm_Mmap,		TRUE,	7,   NILPARM,
    Vm_Munmap,			Vm_Munmap,		TRUE,	3,   NILPARM,
    Vm_Msync,			Vm_Msync,		TRUE,	2,   NILPARM,
    Vm_Mlock,			Vm_Mlock,		TRUE,	2,   NILPARM,
    Vm_Munlock,			Vm_Munlock,		TRUE,	2,   NILPARM,
    Vm_Mincore,			Vm_Mincore,		TRUE,	3,   NILPARM,
    Sync_SemctlStub,		Sync_SemctlStub,	TRUE,	5,   NILPARM,
    Sync_SemgetStub,		Sync_SemgetStub,	TRUE,	4,   NILPARM,
    Sync_SemopStub,		Sync_SemopStub,		TRUE,	4,   NILPARM,
    Vm_Mprotect,		Vm_Mprotect,		TRUE,   3,   NILPARM,
    Proc_Vfork,	                Proc_Vfork,	        TRUE,	0,   NILPARM,
    Net_GetRoutes,		Net_GetRoutes,		TRUE,	5,   NILPARM,
    Net_DeleteRouteStub,	Net_DeleteRouteStub,	TRUE,	1,   NILPARM,
    /*
     * The following are placeholders for Zebra system calls which aren't 
     * in the standard kernel. 
     */
    ErrorProc,   		ErrorProc,   		TRUE,	3,   NILPARM,
    ErrorProc,   		ErrorProc,   		TRUE,	3,   NILPARM,
    Sys_GetHostName,		Proc_DoRemoteCall, 	FALSE,	1,   NILPARM,
    Sys_SetHostName,		Proc_DoRemoteCall, 	FALSE,	1,   NILPARM,
};


/*
 * paramsArray is a static array of parameter information.  The array is
 * one gigantic array so that it may be initialized at compile time, but
 * conceptually it is distinct arrays, one per system call.  SysInitSysCall,
 * called at system initialization time, maps points within this array
 * to paramsPtr fields within the sysCalls array.  ParamsPtr is initialized
 * to NIL at compile time, but for procedures that are not flagged as
 * "special", paramsPtr is reset to point into paramsArray at the point of
 * the first Sys_CallParam structure corresponding to that procedure.
 *
 * For system calls that are "special", there is no entry in paramsArray.
 * However, a comment with the system call number and " special" is useful
 * to keep track of the correspondence between parameter information and
 * the rest of the sysCall struct.  Note that "special" is equivalent to
 * "local": "special" usually means a special-purpose routine is called,
 * while "local" means the same routine is used for both local and migrated
 * processes.
 *
 * The format of paramsArray is as follows.  For each non-special
 * SysCallEntry, there should be -numWords- Sys_CallParam structures.
 * A number of defined constants are given to simplify the information
 * given for each one.  Essentially, the crucial things to consider are
 * whether a given parameter is passed IN to a procedure, OUT of it, or
 * both.  At the same time, is the parameter passed into the system call
 * in its entirety, such as an integer; or is the parameter a pointer to
 * something that needs to be copied into or out of the kernel address
 * space, or made accessible?  Finally, if the parameter is a pointer, is
 * it a pointer to an object of fixed size or does it point to an array
 * of objects?  The generic stub will handle arrays if the size of the
 * array is an IN parameter that is passed in just before the pointer to
 * the array, in the argument list.  It will also handle arrays with
 * a range of numbers that indicates the size of the array; for example,
 * if the preceding arguments were 2 and 5, the size of the array would
 * be (5 - 2 + 1) * sizeof(...).
 *
 * Note that multi-word parameters must be treated in this array as
 * *separate* arguments.  For example, Time structures are given as
 * TIME1 and TIME2.  This is because the procedures & structures in this
 * file do not know the actual number of arguments, but rather the number
 * of words that a system call is passed.
 */

#define PARM 		0
#define PARM_I 		SYS_PARAM_IN
#define PARM_O 		SYS_PARAM_OUT
#define PARM_IO		(SYS_PARAM_IN | SYS_PARAM_OUT)
#define PARM_IA 	(SYS_PARAM_IN | SYS_PARAM_ACC)
#define PARM_OA 	(SYS_PARAM_OUT | SYS_PARAM_ACC)
#define PARM_IOA	(PARM_IO | SYS_PARAM_ACC)
#define PARM_IC 	(SYS_PARAM_IN | SYS_PARAM_COPY)
#define PARM_OC		(SYS_PARAM_OUT | SYS_PARAM_COPY)
#define PARM_IOC	(PARM_IO | SYS_PARAM_COPY)
#define PARM_ICR 	(SYS_PARAM_IN | SYS_PARAM_COPY | SYS_PARAM_ARRAY)
#define PARM_OCR	(SYS_PARAM_OUT | SYS_PARAM_COPY | SYS_PARAM_ARRAY)

static Sys_CallParam paramsArray[] = {
    /* special */				/* SYS_PROC_FORK	0 */
    /* special */			     	/* SYS_PROC_EXEC	1 */
    /* special */ 				/* SYS_PROC_EXIT	2 */
    /* local */		      			/* SYS_SYNC_WAITTIME	3 */
    /* local */					/* SYS_TEST_PRINTOUT	4 */
    /* local */					/* SYS_TEST_GETLINE	5 */
    /* local */					/* SYS_TEST_GETCHAR	6 */
    /* local */					/* SYS_FS_OPEN		7 */
    /* local */					/* SYS_FS_READ		8 */
    /* local */					/* SYS_FS_WRITE		9 */
    /* local */					/* SYS_FS_CLOSE		10 */
    /* local */					/* SYS_FS_REMOVE	11 */
    /* local */					/* SYS_FS_REMOVE_DIR	12 */
    /* local */					/* SYS_FS_MAKE_DIR	13 */
    /* local */					/* SYS_FS_CHANGE_DIR	14 */
    /* special */			     	/* SYS_PROC_WAIT	15 */
    SYS_PARAM_INT,	      PARM_I,		/* SYS_PROC_DETACH	16 */
    /* local */				     	/* SYS_PROC_GETIDS	17 */
    SYS_PARAM_INT,	      PARM_I,		/* SYS_PROC_SETIDS	18 */
    SYS_PARAM_INT,	      PARM_I,
    /* local */				     	/* SYS_PROC_GETGROUPIDS 19 */
    /* local */				     	/* SYS_PROC_SETGROUPIDS 20 */
    SYS_PARAM_PROC_PID,	      PARM_I,		/* SYS_PROC_GETFAMILYID 21 */
    SYS_PARAM_PROC_PID,	      PARM_OC,
    SYS_PARAM_PROC_PID,	      PARM_I,		/* SYS_PROC_SETFAMILYID 22 */
    SYS_PARAM_INT,	      PARM_I,
    /* test */					/* SYS_TEST_RPC		23 */
    /* test */					/* SYS_SYS_STATS	24 */
    /* local */					/* SYS_VM_CREATEVA	25 */
    /* local */					/* SYS_VM_DESTROYVA	26 */
    /* local */					/* SYS_SIG_SEND		27 */
    /* local */ 				/* SYS_SIG_PAUSE	28 */
    /* local */ 				/* SYS_SIG_SETHOLDMASK	29 */
    /* local */				     	/* SYS_SIG_SETACTION	30 */

    /* local */				     	/* SYS_PROF_START	31 */
    /* local */				     	/* SYS_PROF_END		32 */
    /* local */				     	/* SYS_PROF_DUMP	33 */
    /* local */					/* SYS_VM_CMD		34 */
    SYS_PARAM_TIMEPTR,	      PARM_OC,		/* SYS_SYS_GETTIMEOFDAY 35 */
    SYS_PARAM_INT,	      PARM_OC,
    SYS_PARAM_INT,	      PARM_OC,
    SYS_PARAM_TIMEPTR,	      PARM_IC,		/* SYS_SYS_SETTIMEOFDAY 36 */
    SYS_PARAM_INT,	      PARM_I,
    SYS_PARAM_INT,	      PARM_I,
    /* local */				     	/* SYS_SYS_DONOTHING	37 */
    /* local */				     	/* SYS_PROC_GETPCBINFO	38 */
    /* local */					/* SYS_VM_GETSEGINFO	39 */
    /* local */					/* SYS_PROC_GETRESUSAGE 40 */
    /* local */					/* SYS_PROC_GETPRIORITY 41 */
    /* local */					/* SYS_PROC_SETPRIORITY 42 */
    /* special (don't migrate?) */	     	/* SYS_PROC_DEBUG	43 */
    /* local case not implemented */		/* SYS_PROC_PROFILE	44 */
    /* local */					/* SYS_FS_TRUNC		45 */
    /* local */					/* SYS_FS_TRUNC_ID	46 */
    /* local */ 				/* SYS_FS_GET_NEW_ID	47 */
    /* local */					/* SYS_FS_GET_ATTRIBUTES 48 */
    /* local */ 				/* SYS_FS_GET_ATTR_ID	49 */
    /* local */					/* SYS_FS_SET_ATTRIBUTES 50 */
    /* local */ 				/* SYS_FS_SET_ATTR_ID	51 */
    /* local */					/* SYS_FS_SET_DEF_PERM	52 */
    /* local */			     		/* SYS_FS_IO_CONTROL	53 */
    SYS_PARAM_INT,	      PARM_I,		/* SYS_SYS_ENABLEDISPLAY 54 */
    /* obsolete */				/* SYS_PROC_SET_ENVIRON 55 */
    /* obsolete */				/* SYS_PROC_UNSET_ENVIRON 56 */
    /* obsolete */				/* ..._GET_ENVIRON_VAR	57 */
    /* obsolete */				/* ..._GET_ENVIRON_RANGE 58 */
    /* obsolete */				/* ..._INSTALL_ENVIRON	59 */
    /* obsolete */				/* SYS_PROC_COPY_ENVIRON 60 */
    /* local */					/* SYS_SYNC_SLOWLOCK	61 */
    /* local */					/* SYS_SYNC_SLOWWAIT	62 */
    /* local */					/* SYS_SYNC_SLOWBROADCAST 63 */
    /* local */					/* SYS_VM_PAGESIZE	64 */
    /* local */					/* SYS_FS_HARDLINK	65 */
    /* local */					/* SYS_FS_RENAME	66 */
    /* local */					/* SYS_FS_SYMLINK	67 */
    /* local */					/* SYS_FS_READLINK	68 */
    /* local */					/* SYS_FS_CREATEPIPE	69 */
    SYS_PARAM_INT,	      PARM_I,		/* ..VM_MAPKERNELINTOUSER 70 */
    SYS_PARAM_INT,	      PARM_I,
    SYS_PARAM_INT,	      PARM_I,
    SYS_PARAM_INT,	      PARM_OC,
    SYS_PARAM_FS_NAME,	      PARM_IA,		/* SYS_FS_ATTACH_DISK	71 */
    SYS_PARAM_FS_NAME,	      PARM_IA,
    SYS_PARAM_INT,	      PARM_I,
    /* local */			     		/* SYS_FS_SELECT	72 */
    /* local */					/* SYS_SYS_SHUTDOWN	73 */
    SYS_PARAM_PROC_PID,	      PARM_I,		/* SYS_PROC_MIGRATE	74 */
    SYS_PARAM_INT,	      PARM_I,
    SYS_PARAM_FS_NAME, 	      PARM_IA,		/* SYS_FS_MAKE_DEVICE	75 */
    SYS_PARAM_FS_DEVICE,      PARM_IC,
    SYS_PARAM_INT,	      PARM_I,
    /* local */					/* SYS_FS_COMMAND	76 */
    /* local */					/* -obsolete-		77 */
    /* local */					/* SYS_GETMACHINEINFO	78 */
    /* special */				/* SYS_NET_INSTALL_ROUTE 79 */
    /* local */					/* SYS_FS_READVECTOR	80 */
    /* local */					/* SYS_FS_WRITEVECTOR	81 */
    /* local */					/* SYS_FS_CHECKACCESS	82 */
    /* local */				/* SYS_PROC_GETINTERVALTIMER	83 */
    /* local */				/* SYS_PROC_SETINTERVALTIMER	84 */
    /* local */				/* SYS_FS_WRITEBACKID		85 */
    /* special */			     	/* SYS_PROC_EXEC_ENV	86 */
    /* local */				/* SYS_FS_SET_ATTR_NEW		87 */
    /* local */ 			/* SYS_FS_SET_ATTR_ID_NEW	88 */
    /* local */ 			/* SYS_PROC_GETHOSTIDS		89 */
    /* local */ 			/* SYS_SCHED_IDLE_PROCESSOR	90 */
    /* local */ 			/* SYS_SCHED_START_PROCESSOR	91 */
    /* local */ 			/* SYS_MACH_NUM_PROCESSORS	92 */
    /* local */                         /* SYS_PROF_PROFIL              93 */
    /* local */                         /* SYS_PROC_REMOTE_EXEC         94 */
    /* local */                         /* SYS_SYS_GETMACHINEINFO_NEW   95 */
    /* local */                         /* SYS_VM_MMAP			96 */
    /* local */                         /* SYS_VM_MUNMAP		97 */
    /* local */                         /* SYS_VM_MSYNC			98 */
    /* local */                         /* SYS_VM_MLOCK			99 */
    /* local */                         /* SYS_VM_MUNLOCK		100 */
    /* local */                         /* SYS_VM_MINCORE		101 */
    /* local */                         /* SYS_SYNC_SEMCTL		102 */
    /* local */                         /* SYS_SYNC_SEMGET		103 */
    /* local */                         /* SYS_SYNC_SEMOP		104 */
    /* local */                         /* VM_MPROTECT			105 */
    /* special */			/* SYS_PROC_VFORK	        106 */
    /* local */				/* SYS_NET_GET_ROUTES		107 */
    /* local */				/* SYS_NET_DELETE_ROUTE		108 */
    /* local */				/* SYS_ZSS_CMD			109 */
    /* local */				/* SYS_ZEBRA_CMD		110 */
    SYS_PARAM_HOSTNAME,		PARM_OC,	/* SYS_SYS_GET_HOSTNAME	111 */
    SYS_PARAM_HOSTNAME,		PARM_IA,	/* SYS_SYS_SET_HOSTNAME	112 */

    /*
     * Insert new system call information above this line.
     */
    NIL,		      NIL		/* array compatibility check */
};

/*
 * Define an array to count the number of system calls performed for local
 * and foreign processes, as well as subscripts and a macro to reset it.
 */

#define LOCAL_CALL 0
#define FOREIGN_CALL 1
int sys_NumCalls[SYS_NUM_SYSCALLS];
#define RESET_NUMCALLS() bzero((Address) sys_NumCalls, \
				SYS_NUM_SYSCALLS * sizeof(int));

/* 
 * Define an array of ticks, to keep track of the total time spent in each 
 * system call.  sys_CallProfiling indicates whether to maintain the array 
 * or not.
 */

Boolean sys_CallProfiling = FALSE;
Timer_Ticks sys_CallTimes[SYS_NUM_SYSCALLS];


/*
 *----------------------------------------------------------------------
 *
 * SysInitSysCall --
 *
 *	Initialize the data structures for performing a system call.
 *	Make sure the last entry in the array of parameters is (NIL, NIL)
 * 	(serving as a cross check on the total number of parameters to
 *	be initialized).  Initialize the count of the number of system
 *	calls performed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The mapping between system calls and their argument types is
 *	established.
 *
 *----------------------------------------------------------------------
 */

void
SysInitSysCall()
{
    int sysCallNum;
    SysCallEntry *entryPtr;
    Sys_CallParam *paramPtr;

    paramPtr = paramsArray;
    entryPtr = sysCalls;
    for (sysCallNum = 0; sysCallNum < SYS_NUM_SYSCALLS; sysCallNum++) {
	if (!entryPtr->special) {
	    entryPtr->paramsPtr = paramPtr;
	    paramPtr += entryPtr->numWords;
	/*
	 * Won't lint due to cast of function pointer.
	 */
#ifndef lint
	    Mach_InitSyscall(sysCallNum, entryPtr->numWords,
		    entryPtr->localFunc, SysMigCall);
#endif /* lint */
	} else {
	/*
	 * Won't lint due to cast of function pointer.
	 */
#ifndef lint
	    Mach_InitSyscall(sysCallNum, entryPtr->numWords,
		    entryPtr->localFunc, entryPtr->remoteFunc);
#endif /* lint */
	}
	entryPtr++;
    }
    if (paramPtr->type != NIL || paramPtr->disposition != NIL) {
	panic("SysInitSysCall: error initializing parameter array.\n");
    }
    RESET_NUMCALLS();
}

/*
 *----------------------------------------------------------------------
 *
 * SysMigCall --
 *
 *	This procedure is invoked whenever a migrated process invokes
 *	a kernel call that doesn't have "special" set.  It arranges
 *	for the kernel call to be sent home in a standard fashion.
 *
 * Results:
 *	Returns the result of the kernel call, whatever that is.
 *
 * Side effects:
 *	Depends on the kernel call.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
SysMigCall(args)
    Sys_ArgArray args;			/* The arguments to the system call. */
{
    int sysCall;
    register SysCallEntry *entryPtr;

    sysCall = Mach_GetLastSyscall();
    entryPtr = &sysCalls[sysCall];
    return (*entryPtr->remoteFunc)(sysCall, entryPtr->numWords,
	    (ClientData *) &args, entryPtr->paramsPtr);
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

ReturnStatus
Sys_OutputNumCalls(requestedCount, buffer, doTimes)
    int requestedCount;	/* number of system calls statistics to copy */
    Address buffer;		/* start address of user's buffer */
    Boolean doTimes;		/* copy per-call times as well */
{
    ReturnStatus status = SUCCESS;
    int numToCopy;		/* number of calls actually copied out */

    if (requestedCount == 0) {
	RESET_NUMCALLS();
	bzero(sys_CallTimes, SYS_NUM_SYSCALLS * sizeof(Timer_Ticks));
    } else {
	/* 
	 * If the user wants the per-call times as well, put them after the 
	 * per-call counts.  If there are fewer calls than the user 
	 * requested, there will be a gap.  (Otherwise, how would the user 
	 * know where to find the times?)
	 */
	numToCopy = (requestedCount > SYS_NUM_SYSCALLS
		     ? SYS_NUM_SYSCALLS
		     : requestedCount);
	status = Vm_CopyOut(numToCopy * sizeof(int), (Address) sys_NumCalls,
			    buffer);
	if (doTimes && status == SUCCESS) {
	    status = OutputCallTimes(numToCopy,
				     buffer + requestedCount * sizeof(int));
	}
    }
    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * OutputCallTimes --
 *
 *	Copy the per-call times to a user buffer.
 *
 * Results:
 *	Returns the usual Sprite status code.
 *
 * Side effects:
 *	The current cumulative times for the system calls are converted 
 *	from Ticks to Time's and then copied out.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
OutputCallTimes(numToCopy, buffer)
    int numToCopy;		/* number of calls to copy; already 
				 * truncated if necessary*/
    Address buffer;		/* where to put the times */
{
    Time times[SYS_NUM_SYSCALLS];
    int index;

    for (index = 0; index < SYS_NUM_SYSCALLS; ++index) {
	Timer_TicksToTime(sys_CallTimes[index], &times[index]);
    }

    return Vm_CopyOut(numToCopy * sizeof(Time), (Address)times,
		      buffer);
}


/*
 *----------------------------------------------------------------------
 *
 * Sys_RecordCallStart --
 *
 *	Record the current time in the PCB.  Called at the start of a 
 *	system call.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Sys_RecordCallStart()
{
    Proc_ControlBlock *procPtr = Proc_GetCurrentProc();

    Timer_GetCurrentTicks(&procPtr->syscallStartTime);
}


/*
 *----------------------------------------------------------------------
 *
 * Sys_RecordCallFinish --
 *
 *	Update the time spent for a particular system call.  Called after 
 *	the call has been handled.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the appropriate tick count in sys_CallTimes.
 *
 *----------------------------------------------------------------------
 */

void
Sys_RecordCallFinish(callNum)
    int callNum;		/* system call number */
{
    Timer_Ticks totalTime;
    Timer_Ticks now;
    Proc_ControlBlock *procPtr;

    procPtr = Proc_GetCurrentProc();
    Timer_GetCurrentTicks(&now);
    Timer_SubtractTicks(now, procPtr->syscallStartTime, &totalTime);
    Timer_AddTicks(sys_CallTimes[callNum], totalTime,
		   &sys_CallTimes[callNum]);
}
