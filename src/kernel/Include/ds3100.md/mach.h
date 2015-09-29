/*
 * mach.h --
 *
 *     	Exported structures for the mach module.
 *
 *	Copyright (C) 1989 Digital Equipment Corporation.
 *	Permission to use, copy, modify, and distribute this software and
 *	its documentation for any purpose and without fee is hereby granted,
 *	provided that the above copyright notice appears in all copies.
 *	Digital Equipment Corporation makes no representations about the
 *	suitability of this software for any purpose.  It is provided "as is"
 *	without express or implied warranty.
 *
 * $Header: /sprite/src/kernel/Cvsroot/kernel/mach/ds3100.md/mach.h,v 9.7 92/08/10 17:58:14 mgbaker Exp $ SPRITE (Berkeley)
 */

#ifndef _MACH
#define _MACH

#ifdef KERNEL
#include "machTypes.h"
#include "user/fmt.h"
#else
#include <kernel/machTypes.h>
#include <fmt.h>
#endif

/*
 * Macros to disable and enable interrupts.
 */
#define DISABLE_INTR() \
    if (!Mach_AtInterruptLevel()) { \
	Mach_DisableIntr(); \
	if (mach_NumDisableIntrsPtr[0] < 0) { \
	    panic("Negative interrupt count.\n"); \
	} \
	mach_NumDisableIntrsPtr[0]++; \
    }
#define ENABLE_INTR() \
    if (!Mach_AtInterruptLevel()) { \
	mach_NumDisableIntrsPtr[0]--; \
	if (mach_NumDisableIntrsPtr[0] < 0) { \
	    panic("Negative interrupt count.\n"); \
	} \
	if (mach_NumDisableIntrsPtr[0] == 0) { \
	    Mach_EnableIntr(); \
	} \
    }

/*
 * Macro to get level of nesting of disabled interrupts.
 */
#define Mach_IntrNesting(cpu) (mach_NumDisableIntrsPtr[(cpu)])

/*
 * A macro to test if the current processor is at interrupt level.
 */
#define	Mach_AtInterruptLevel()	(mach_AtInterruptLevel)

/*
 * A macro to test if the current processor is in kernel mode.
 */
#define	Mach_KernelMode() (mach_KernelMode)

/*
 * Delay for N microseconds.
 */
#define	MACH_DELAY(n)	{ register int N = (n)*6; while (N > 0) {N--;} }

extern Mach_State *machCurStatePtr;
/*
 * Macro to get the user's stack pointer.
 */
#define Mach_UserStack()\
	((Address)(machCurStatePtr->userState.regState.regs[SP]))

/*
 * Dispatch tables for kernel calls.
 */
extern ReturnStatus (*(mach_NormalHandlers[]))();
extern ReturnStatus (*(mach_MigratedHandlers[]))();

/*
 * Macro to get processor number
 */
#define	Mach_GetProcessorNumber() 	0

extern	Boolean	mach_KernelMode;
extern	int	mach_NumProcessors;
extern	Boolean	mach_AtInterruptLevel;
extern	int	*mach_NumDisableIntrsPtr;
/*
 * mach_MachineType is a string used to expand $MACHINE in pathnames.
 */
extern	char	*mach_MachineType;

/*
 * mach_Format defines a byte ordering/structure alignment type
 * used when servicing IOControls.  The input and output buffers for
 * IOControls have to be made right by the server.
 */
extern	Fmt_Format	mach_Format;

/*
 * Routine to initialize mach module.  Must be called first as part of boot
 * sequence.
 */
extern void Mach_Init _ARGS_((int boot_argc, MachStringTable *boot_argv));

/*
 * Routines to munge machine state struct.
 */
extern void Mach_InitFirstProc _ARGS_((Proc_ControlBlock *procPtr));
extern ReturnStatus Mach_SetupNewState _ARGS_((Proc_ControlBlock *procPtr, Mach_State *fromStatePtr, void (*startFunc)(), Address startPC, Boolean user));
extern void Mach_SetReturnVal _ARGS_((Proc_ControlBlock *procPtr, int retVal,
	int retVal2));
extern void Mach_StartUserProc _ARGS_((Proc_ControlBlock *procPtr, Address entryPoint));
extern void Mach_ExecUserProc _ARGS_((Proc_ControlBlock *procPtr, Address userStackPtr, Address entryPoint));
extern void Mach_FreeState _ARGS_((Proc_ControlBlock *procPtr));
extern void Mach_CopyState _ARGS_((Mach_State *statePtr, Proc_ControlBlock *destProcPtr));
extern void Mach_GetDebugState _ARGS_((Proc_ControlBlock *procPtr, Proc_DebugState *debugStatePtr));
extern void Mach_SetDebugState _ARGS_((Proc_ControlBlock *procPtr, Proc_DebugState *debugStatePtr));
extern Address Mach_GetUserStackPtr _ARGS_((Proc_ControlBlock *procPtr));

/*
 * Migration routines.
 */
extern ReturnStatus Mach_EncapState _ARGS_((register Proc_ControlBlock *procPtr, int hostID, Proc_EncapInfo *infoPtr, Address buffer));
extern ReturnStatus Mach_DeencapState _ARGS_((register Proc_ControlBlock *procPtr, Proc_EncapInfo *infoPtr, Address buffer));
extern ReturnStatus Mach_GetEncapSize _ARGS_((Proc_ControlBlock *procPtr, int hostID, Proc_EncapInfo *infoPtr));
extern Boolean Mach_CanMigrate _ARGS_((Proc_ControlBlock *procPtr));
extern int Mach_GetLastSyscall _ARGS_((void));


/*
 * Other routines.
 */
extern void Mach_InitSyscall _ARGS_((int callNum, int numArgs, ReturnStatus (*normalHandler)(), ReturnStatus (*migratedHandler)()));

extern int Mach_GetNumProcessors _ARGS_((void));
extern Mach_ProcessorStates Mach_ProcessorState _ARGS_((int processor));
extern Address			Mach_GetPC();
/*
 * Machine dependent routines.
 */
extern void Mach_GetEtherAddress _ARGS_((Net_EtherAddress *etherAddrPtr));
extern	void		Mach_ContextSwitch();
extern	int		Mach_TestAndSet();
extern int Mach_GetMachineArch _ARGS_((void));
extern int Mach_GetMachineType _ARGS_((void));
extern Address Mach_GetStackPointer _ARGS_((Proc_ControlBlock *procPtr));
extern void Mach_CheckSpecialHandling _ARGS_((int pnum));
extern int Mach_GetBootArgs _ARGS_((int argc, int bufferSize, char **argv, char *buffer));
extern  ReturnStatus	Mach_Probe _ARGS_((int size, Address srcAddress,
					Address, destAddress));
extern  ReturnStatus	Mach_ProbeAddr _ARGS_((int numArgs));
extern void Mach_FlushCode _ARGS_((Address addr, unsigned len));
extern void Mach_Return2 _ARGS_((int val));
extern int Mach_SigreturnStub _ARGS_((struct sigcontext *sigContextPtr));

/*
 * Machine dependent variables.
 */
extern	Address	mach_KernStart;
extern	Address	mach_CodeStart;
extern	Address	mach_StackBottom;
extern	int	mach_KernStackSize;
extern	Address	mach_KernEnd;
extern	Address	mach_FirstUserAddr;
extern	Address	mach_LastUserAddr;
extern	Address	mach_MaxUserStackAddr;
extern	int	mach_LastUserStackPage;

/*
 * Set the errno for Unix compatible system calls.
 */
#define Mach_SetErrno(err) Proc_GetActualProc()->unixErrno = (err)

/*
 * Fast recovery definitions.
 */
extern  int     storedDataSize;
extern  char    storedData[];
extern  char    *mach_RestartTablePtr;
extern  ReturnStatus    Mach_FastBoot _ARGS_((void));
extern  int     Mach_GetRestartTableSize _ARGS_((void));

#endif /* _MACH */
