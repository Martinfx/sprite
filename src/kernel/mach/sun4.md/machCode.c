/* 
 * machCode.c --
 *
 *     C code for the mach module.
 *
 * Copyright (C) 1985 Regents of the University of California
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Header: /cdrom/src/kernel/Cvsroot/kernel/mach/sun4.md/machCode.c,v 9.42 93/01/06 20:09:15 mgbaker Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include <stddef.h>
#include <sprite.h>
#include <swapBuffer.h>
#include <machConst.h>
#include <machMon.h>
#include <machInt.h>
#include <mach.h>
#include <proc.h>
#include <prof.h>
#include <sys.h>
#include <sched.h>
#include <vm.h>
#include <vmMach.h>
#include <user/sun4.md/sys/machSignal.h>
#include <procUnixStubs.h>

#include <stdlib.h>
#include <stdio.h>
#include <bstring.h>
#include <compatInt.h>
#include <ctype.h>
#include <recov.h>

/*
 *  Number of processors in the system.
 */
#ifndef NUM_PROCESSORS
#define NUM_PROCESSORS 1
#endif NUM_PROCESSORS

int mach_NumProcessors = NUM_PROCESSORS;

/*
 * The following two variables should be in the initialized data space, but
 * marked as not initialized.  Then, when they are updated as part of booting,
 * their new values get preserved over a fast restart.
 */
int	storedDataSize = -1;
char	*mach_RestartTablePtr = (char *) NIL;

/*
 * TRUE if cpu was in kernel mode before the interrupt, FALSE if was in 
 * user mode.
 */
Boolean	mach_KernelMode;

/*
 * Sp saved into this before debugger call.
 */
int	machSavedRegisterState = 0;

/*
 *  Flag used by routines to determine if they are running at
 *  interrupt level.
 */
Boolean mach_AtInterruptLevel = FALSE;

/*
 * The machine type string is imported by the file system and
 * used when expanding $MACHINE in file names.
 */

char *mach_MachineType = "sun4";

/*
 * The byte ordering/alignment type used with Fmt_Convert and I/O control data.
 * For compatablity we set this to the old Swap_Buffer constant.
 */
Fmt_Format	mach_Format = FMT_SPARC_FORMAT;

/*
 *  Count of number of ``calls'' to enable interrupts minus number of calls
 *  to disable interrupts.  Kept on a per-processor basis.
 */
int mach_NumDisableInterrupts[NUM_PROCESSORS];
int *mach_NumDisableIntrsPtr = mach_NumDisableInterrupts;

extern int debugProcStubs;

/*
 * Machine dependent variables.
 */
Address	mach_KernStart;
Address	mach_CodeStart;
Address	mach_StackBottom;
int	mach_KernStackSize;
Address	mach_KernEnd;
Address	mach_FirstUserAddr;
Address	mach_LastUserAddr;
Address	mach_MaxUserStackAddr;
int	mach_LastUserStackPage;

Address	machTBRAddr;			/* address of trap table.  The value
					 * is set up and stored here in
					 * bootSysAsm.s.*/
#define	MACH_NUM_VECTORS	256	/* Number of interrupt vector slots */
Address	machVectorTable[MACH_NUM_VECTORS];	/* Table of autovector and
					 * vectored interrupt handlers. */
ClientData	machInterruptArgs[MACH_NUM_VECTORS];	/* Table of clientData
					 * args to pass interrupt handlers */
int	machMaxSysCall;			/* Hightest defined system call. */
int	machArgOffsets[SYS_NUM_SYSCALLS];/* For each system call, tells how
					 * much to add to the fp at the time
					 * of the call to get to the highest
					 * argument on the stack.  */
Address	machArgDispatch[SYS_NUM_SYSCALLS];/* For each system call, gives an
					 * address to branch to, in the
					 * middle of MachFetchArgs, to copy the
					 * right # of args from user space to
					 * the kernel's stack. */
ReturnStatus	(*(mach_NormalHandlers[SYS_NUM_SYSCALLS]))();
					/* For each system call, gives the
					 * address of the routine to handle
					 * the call for non-migrated processes.
				         */
ReturnStatus (*(mach_MigratedHandlers[SYS_NUM_SYSCALLS]))();
					/* For each system call, gives the
					 * address of the routine to handle
				         * the call for migrated processes. */
int	machKcallTableOffset;		/* Byte offset of the kcallTable field
					 * in a Proc_ControlBlock. */
int	machStatePtrOffset;		/* Byte offset of the machStatePtr
					 * field in a Proc_ControlBlock. */
int	machSpecialHandlingOffset;	/* Byte offset of the specialHandling
					 * field in a Proc_ControlBlock. */
int	machTmpRegsOffset;		/* Offset of overflow temp regs. */
int	machTmpRegsStore[2];		/* Temporary storage. */
int	machGenFlagsOffset;		/* offset of genFlags field in a
					 * Proc_ControlBlock. */
int	machForeignFlag;		/* Value of PROC_FOREIGN available
					 * in assembler. */
int	MachPIDOffset;			/* Byte offset of pid in PCB */
char	mach_DebugStack[0x2000];	/* The debugger stack. */
unsigned int	machDebugStackStart;	/* Contains address of base of debugger
					 * stack. */
int	machSignalStackSizeOnStack;	/* size of mach module sig stack */
int	machSigStackSize;		/* size of Sig_Stack structure */
int	machSigStackOffsetOnStack;	/* offset of sigStack field in
					 * MachSignalStack structure on the
					 * the user stack. */
int	machSigStackOffsetInMach;	/* offset to sigStack field in mach
					 * state structure. */
int	machSigContextSize;		/* size of Sig_Context structure */
int	machSigContextOffsetOnStack;	/* offset of sigContext field in
					 * MachSignalStack structure on the
					 * user stack. */
int	machSigContextOffsetInMach;	/* offset to sigContext field in mach
					 * state structure. */
int	machSigUserStateOffsetOnStack;	/* offset of machine-dependent field
					 * on the stack, called machContext,
					 * in the Sig_Context part of the
					 * MachSignalStack structure. */
int	machSigTrapInstOffsetOnStack;	/* offset of trapInst field in
					 * MachSignalStack on user stack. */
int	machSigNumOffsetInSig;		/* offset of sigNum field in
					 * Sig_Stack structure. */
int	machSigAddrOffsetInSig;		/* offset of sigAddr field in
					 * Sig_Stack structure. */
int	machSigCodeOffsetInSig;		/* offset of sigCode field in
					 * Sig_Stack structure. */
int	machSigPCOffsetOnStack;		/* offset of pcValue field in
					 * MachSignalStack on user stack. */
int	machLastSysCallOffset;		/* offset of lastSysCall field in
					 * Mach_State structure. */


Proc_ControlBlock *machFPUSaveProcPtr;	/* Set to the Proc_ControlBlock of the
					 * process we are saving the FPU
					 * state for in Mach_Context switch. */
int		machFPUSyncInst();     /* PC of stfsr instruction in 
					* context switch. */
int		machFPUDumpSyncInst(); /* PC of stfsr instruction in 
					* MachDumpFPUState. */
/*
 * Pointer to the state structure for the current process.
 */
Mach_State	*machCurStatePtr = (Mach_State *)NIL;

char	MachUnixAddr[] =
				"Unix addr %x\n"; 
char	MachUnixString[] =
				"Unix syscall %d\n"; 
char	MachUnixYesString[] =
				"syscall success\n"; 
char	MachUnixNoString[] =
				"syscall failure\n"; 
char	MachRunUserDeathString[] =
				"MachRunUserProc: killing process!\n"; 
char	MachHandleSignalDeathString[] =
				"MachHandleSignal: killing process!\n"; 
char	MachReturnFromSignalDeathString[] =
				"MachReturnFromSignal: killing process!\n"; 
char	MachReturnFromTrapDeathString[] =
				"MachReturnFromTrap: killing process!\n"; 
char	MachHandleWindowUnderflowDeathString[] =
				"MachHandleWindowUnderflow: killing process!\n";

/*
 * For testing correctness of defined offsets.
 */
#if 0
Mach_RegState		testMachRegState;
Mach_State		testMachState;
Proc_ControlBlock	testPCB;
MachSignalStack		testSignalStack;
Sig_Context		testContext;
Sig_Stack		testStack;
#endif
int			debugCounter = 0;		/* for debugging */
int			debugSpace[500];
Address			theAddrOfVmPtr = 0; 
Address			theAddrOfMachPtr = 0;
Address			oldAddrOfVmPtr = 0; 
Address			oldAddrOfMachPtr = 0;

					/*
					 * Make sure machRomVectorPtr is
					 * in the initialized data section.
					 * This is needed when transparent
					 * recovery is used.
					 */
MachMonRomVector	*machRomVectorPtr = (MachMonRomVector *) NIL;
MachMonBootParam	machMonBootParam;
#ifdef sun4c
unsigned char		*machInterruptReg;
unsigned int		machClockRate;
struct idprom		machIdProm = {0};
#endif
					/*
					 * Make sure these variables are
					 * in the initialized data section.
					 */
unsigned int		machNumWindows = 0;
unsigned int		machWimShift = 0;		/* # windows - 1 */

/*
 * Forward declarations.
 */
static void FlushTheWindows _ARGS_((int num));
static void HandleFPUException _ARGS_((Proc_ControlBlock *procPtr, 
				       Mach_State *machStatePtr));

static void CheckFastRestart _ARGS_((void));


/*
 * ----------------------------------------------------------------------------
 *
 * Mach_Init --
 *
 *	Initialize some stuff.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
void
Mach_Init()
{
    int		i;
    extern	void	MachVectoredInterrupt();
    extern	void	MachHandleDebugTrap();
    int		offset;

    /*
     * Set exported machine dependent variables.
     */
    mach_KernStart = (Address)MACH_KERN_START;
    mach_KernEnd = (Address)MACH_KERN_END;
    mach_CodeStart = (Address)MACH_CODE_START;
    mach_StackBottom = (Address)MACH_STACK_BOTTOM;
    mach_KernStackSize = MACH_KERN_STACK_SIZE;
    mach_FirstUserAddr = (Address)MACH_FIRST_USER_ADDR;
    mach_LastUserAddr = (Address)MACH_LAST_USER_ADDR;
    mach_MaxUserStackAddr = (Address)MACH_MAX_USER_STACK_ADDR;
    mach_LastUserStackPage = MACH_LAST_USER_STACK_PAGE;

#define	CHECK_SIZE(c, d)		\
    if (sizeof (c) != d) {		\
	panic("Bad size for structure.  Redo machConst.h!\n");\
    }
	
#define	CHECK_OFFSETS(s, o)		\
    if (offsetof(Mach_State, s) != o) { \
	panic("Bad offset for registers.  Redo machConst.h!\n");\
    }
#define	CHECK_TRAP_REG_OFFSETS(s, o)	   \
    if (offsetof(Mach_RegState, s) != o) { \
	panic("Bad offset for trap registers.  Redo machConst.h!\n");\
    }


    CHECK_SIZE(Mach_RegState, MACH_SAVED_STATE_FRAME);
    CHECK_OFFSETS(trapRegs, MACH_TRAP_REGS_OFFSET);
    CHECK_OFFSETS(switchRegs, MACH_SWITCH_REGS_OFFSET);
    CHECK_OFFSETS(savedRegs[0][0], MACH_SAVED_REGS_OFFSET);
    CHECK_OFFSETS(savedMask, MACH_SAVED_MASK_OFFSET);
    CHECK_OFFSETS(savedSps[0], MACH_SAVED_SPS_OFFSET);
    CHECK_OFFSETS(kernStackStart, MACH_KSP_OFFSET);
    CHECK_OFFSETS(fpuStatus, MACH_FPU_STATUS_OFFSET)
    CHECK_TRAP_REG_OFFSETS(curPsr, MACH_LOCALS_OFFSET);
    CHECK_TRAP_REG_OFFSETS(ins[0], MACH_INS_OFFSET);
    CHECK_TRAP_REG_OFFSETS(globals[0], MACH_GLOBALS_OFFSET);
    CHECK_TRAP_REG_OFFSETS(fsr, MACH_FPU_FSR_OFFSET);
    CHECK_TRAP_REG_OFFSETS(numQueueEntries, MACH_FPU_QUEUE_COUNT);
    CHECK_TRAP_REG_OFFSETS(fregs[0], MACH_FPU_REGS_OFFSET);
    CHECK_TRAP_REG_OFFSETS(fqueue[0], MACH_FPU_QUEUE_OFFSET);

#ifdef sun4c
    if (romVectorPtr->v_romvec_version < 2) {
	if ((*(romVectorPtr->virtMemory))->address !=
		(unsigned) VMMACH_DEV_START_ADDR ||
		((unsigned) VMMACH_DEV_START_ADDR +
		(*(romVectorPtr->virtMemory))->size - 1)
		!= (unsigned) VMMACH_DEV_END_ADDR) {
	    panic("VMMACH_DEV_START_ADDR and VMMACH_DEV_END_ADDR are wrong.\n");
	}
    }
#endif /* sun4c */
#undef CHECK_SIZE
#undef CHECK_OFFSETS
    /*
     * Initialize some of the dispatching information.  The rest is initialized
     * by Mach_InitSysCall, below.
     */

    /*
     * Get offset of machStatePtr in proc control blocks.  This one is
     * subject to a different module, so it's easier not to use a constant.
     */
    machStatePtrOffset = offsetof(Proc_ControlBlock, machStatePtr);
    machKcallTableOffset = offsetof(Proc_ControlBlock, kcallTable);
    machSpecialHandlingOffset = offsetof(Proc_ControlBlock, specialHandling);

    if (MACH_UNIX_ERRNO_OFFSET != offsetof(Proc_ControlBlock, unixErrno)) {
	panic("MACH_UNIX_ERRNO_OFFSET is wrong!\n");
    }

    machMaxSysCall = -1;
    MachPIDOffset = offsetof(Proc_ControlBlock, processID);
    machGenFlagsOffset = offsetof(Proc_ControlBlock, genFlags);
    machForeignFlag = PROC_FOREIGN;

    /*
     * Initialize all the horrid offsets for dealing with getting stuff from
     * signal things in the mach state structure to signal things on the user
     * stack.
     */
    machSignalStackSizeOnStack = sizeof (MachSignalStack);
    if ((machSignalStackSizeOnStack & 0x7) != 0) {
	panic("MachSignalStack struct must be a multiple of double-words!\n");
    }

    machSigStackSize = sizeof (Sig_Stack);
    machSigStackOffsetOnStack = offsetof(MachSignalStack, sigStack);
    machSigStackOffsetInMach = offsetof(Mach_State, sigStack);  
    machSigContextSize = sizeof (Sig_Context);
    machSigContextOffsetOnStack = offsetof(MachSignalStack, sigContext);
    machSigContextOffsetInMach = offsetof(Mach_State, sigContext);
    machSigUserStateOffsetOnStack = offsetof(MachSignalStack, 
        sigContext.machContext.userState);
    machSigTrapInstOffsetOnStack = offsetof(MachSignalStack,
        sigContext.machContext.trapInst);
    machSigNumOffsetInSig = offsetof(Sig_Stack, sigNum);
    machSigAddrOffsetInSig = offsetof(Sig_Stack, sigAddr);
    machSigCodeOffsetInSig = offsetof(Sig_Stack, sigCode);
    machSigPCOffsetOnStack = offsetof(MachSignalStack, 
        sigContext.machContext.pcValue);

    machLastSysCallOffset = offsetof(Mach_State, lastSysCall);

    /*
     * base of the debugger stack
     */
    machDebugStackStart = (unsigned int) mach_DebugStack +
						sizeof (mach_DebugStack);

    /*
     * Initialize the interrupt vector table.
     */
    for (i = 0; i < MACH_NUM_VECTORS; i++) {
#ifndef sun4c
	if (i == 13 || i == 11 || i == 9 || i == 7 || i == 5 || i == 3 ||
		i == 2) {
	    machVectorTable[i] = (Address) MachVectoredInterrupt;
	    /*
	     * Set arg to vme vector address for this trap level.
	     * In the high bits of the address, we want MACH_VME_INTR_VECTOR.
	     * In bits 3 to 1 we want the VME bus level.  We want bit 0 to be
	     * 1.  So, for example, for interrupt level 13, we want to put
	     * VME level7 into bits 3 to 1 and then set bit 0 high.  But this
	     * is equivalent to putting the number 14 into bits 3 to 0 and then
	     * setting bit 0 high.  So this means adding 1 to the interrupt
	     * level and then setting bit 0 high.
	     */
	    machInterruptArgs[i] = (ClientData)
		    (MACH_VME_INTR_VECTOR | ((i + 1) | 1));
	} else { 
#endif
	    machVectorTable[i] = (Address) MachHandleDebugTrap;
	    machInterruptArgs[i] = (ClientData) 0;
#ifndef sun4c
	}
#endif
    }

    /* Temporary: for debugging net module and debugger: */
    mach_NumDisableInterrupts[0] = 1;

#ifdef sun4c
    if (romVectorPtr->v_romvec_version < 2) {
#endif
	/*
	 * Copy the boot parameter structure. The original location will get
	 * unmapped during vm initialization so we need to get our own copy.
	 */
	machMonBootParam = **(romVectorPtr->bootParam);
	offset = (unsigned int) *(romVectorPtr->bootParam) - 
		 (unsigned int) &(machMonBootParam);
	for (i = 0; i < 8; i++) {
	    if (machMonBootParam.argPtr[i] != (char *) 0 &&
	     machMonBootParam.argPtr[i] != (char *) NIL) {
		machMonBootParam.argPtr[i] -= offset;
	    }
	}
#ifdef sun4c
    } 
#endif
#ifndef sun4c
    /*
     * Clear out the line input buffer to the prom so we don't get extra
     * characters at the end of shorter reboot strings.
     */
    bzero((char *)(romVectorPtr->lineBuf), *romVectorPtr->lineSize);
#endif

#ifdef sun4c
    if (Mach_MonSearchProm("*", "clock-frequency",
	    (char *)&machClockRate,
	    sizeof machClockRate) != sizeof machClockRate) {
	panic("Clock rate not found.\n");
    }
    /*
     * Keep clock rate precision to 1 decimal place.
     */
    machClockRate /= 100000;
#ifdef NOTDEF		/* No need to print this stuff. */
    Mach_MonPrintf("PROM: Clock rate is %d.%dMHz\n",
	machClockRate / 10, machClockRate % 10);
#endif /* NOTDEF */

    if (Mach_MonSearchProm("interrupt-enable", "address",
	    (char *)&machInterruptReg,
	    sizeof machInterruptReg) != sizeof machInterruptReg) {
	panic("Interrupt register not found.\n");
    }
#ifdef NOTDEF		/* No need to print this stuff. */
    Mach_MonPrintf("PROM: Interrupt register is at %x\n", machInterruptReg);
#endif /* NOTDEF */

    /*
     * This gets turned on by the profiler init when it is called.
     */
    *Mach_InterruptReg &= ~MACH_ENABLE_LEVEL14_INTR;
#endif
    
    if (recov_Transparent) {
	CheckFastRestart();
    }

    return;
}


/*
 *----------------------------------------------------------------------
 *
 * CheckFastRestart --
 *
 *	Check if enough space was allocated for the fast restart.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static void
CheckFastRestart()
{
    char	*src, *dest;
    int		count;
    extern	int	edata;
    extern	int	etext();

    src =  (char *) &etext;
    dest = (char *) &edata;
    count = (unsigned int) dest - (unsigned int) src;
    if (count > MACH_RESTART_DATA_SIZE) {
	Mach_MonPrintf("Not enough restart space saved for kernel data.\n");
	Mach_MonAbort();
    }

    return;
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_GetRestartTableSize --
 *
 *	Return the size allocated for the fast restart table area.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
Mach_GetRestartTableSize()
{
    return MACH_RESTART_TABLE_SIZE;
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_InitFirstProc --
 *
 *	Initialize the machine state struct for the very first process.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Machine info allocated and stack start set up.
 *
 *----------------------------------------------------------------------
 */
void
Mach_InitFirstProc(procPtr)
    Proc_ControlBlock	*procPtr;
{
    procPtr->machStatePtr = (Mach_State *)Vm_RawAlloc(sizeof(Mach_State));
    bzero((char *)(procPtr->machStatePtr), sizeof (Mach_State));
    procPtr->machStatePtr->kernStackStart = mach_StackBottom;
    procPtr->machStatePtr->trapRegs = (Mach_RegState *) NIL;
    procPtr->machStatePtr->switchRegs = (Mach_RegState *) NIL;
    machCurStatePtr = procPtr->machStatePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_SetupNewState --
 *
 *	Initialize the machine state for this process.  This includes 
 *	allocating and initializing a kernel stack.  Assumed that will
 *	be called when starting a process after a fork or restarting a
 *	process after a migration.
 *
 * Results:
 *	PROC_NO_STACKS if couldn't allocate a kernel stack.  SUCCESS otherwise.
 *
 * Side effects:
 *	Machine state in the destination process control block is overwritten.
 *
 *----------------------------------------------------------------------
 */ 
ReturnStatus
Mach_SetupNewState(procPtr, fromStatePtr, startFunc, startPC, user)
    Proc_ControlBlock	*procPtr;	/* Pointer to process control block
					 * to initialize state for. */
    Mach_State		*fromStatePtr;	/* State of parent on fork or from
					 * other machine on migration. */
    void		(*startFunc)();	/* Function to call when process first
					 * starts executing. */
    Address		startPC;	/* Address to pass as argument to 
					 * startFunc.  If NIL then the address
					 * is taken from *fromStatePtr's 
					 * exception stack. */
    Boolean		user;		/* TRUE if is a user process. */
{
    register	Mach_RegState	*stackPtr;
    register	Mach_State	*statePtr;

    /*
     * If it's a user process forking, we must make sure all its windows have
     * been saved to the stack so that when the register state and the stack
     * are copied to the new process, it will get the real stuff.
     */
    if (user) {
	Mach_DisableIntr();
	Mach_FlushWindowsToStack();
	Mach_EnableIntr();
    }
    if (procPtr->machStatePtr == (Mach_State *)NIL) {
	procPtr->machStatePtr = (Mach_State *)Vm_RawAlloc(sizeof(Mach_State));
    }
    bzero((char *) procPtr->machStatePtr, sizeof (Mach_State));
    statePtr = procPtr->machStatePtr;
    statePtr->trapRegs = (Mach_RegState *)NIL;
    /* 
     * Allocate a kernel stack for this process.
     */
    statePtr->kernStackStart = (Address) Vm_GetKernelStack(0);
    if (statePtr->kernStackStart == (Address)NIL) {
	return(PROC_NO_STACKS);
    }

    /*
     * Pointer to context switch register's save area is also a pointer
     * to the top of the stack, since the regs are saved there.
     * If this is a kernel process, we only need space MACH_SAVED_STATE_FRAME
     * + MACH_FULL_STACK_FRAME, but if it's a user process we need more:
     * (2 * MACH_SAVED_STATE_FRAME).  Both types of processes need the
     * first MACH_SAVED_STATE_FRAME for their context switch regs area.  A
     * kernel process then only needs space under that on its stack for its
     * first routine to store its arguments in its "caller's" stack frame, so
     * this extra space just fakes a caller's stack frame.  But for a user
     * process, we have trap regs.  And these trapRegs get stored under
     * the context switch regs on the kernel stack.
     */
    statePtr->switchRegs = (Mach_RegState *)((statePtr->kernStackStart) +
	    MACH_KERN_STACK_SIZE - (2 * MACH_SAVED_STATE_FRAME));
    statePtr->switchRegs = (Mach_RegState *)
	(((unsigned int)(statePtr->switchRegs)) & ~0x7);  /* should be okay already */
    /*
     * Initialize the stack so that it looks like it is in the middle of
     * Mach_ContextSwitch.
     */
    stackPtr = statePtr->switchRegs;	/* stack pointer is set from this. */
    /*
     * Fp is set to saved window area for window we'll return to.  The area for
     * the window of Mach_ContextSwitch is a Mach_RegState.  Below this on
     * the stack (at higher address than) is the saved window area of the
     * routine we'll return to from Mach_ContextSwitch.  So the fp must be
     * set to the top of this saved window area.
     */
    *((Address *)(((Address)stackPtr) + MACH_FP_OFFSET)) =
	    ((Address)stackPtr) + MACH_SAVED_STATE_FRAME;
    /*
     * We are to return to startFunc from Mach_ContextSwitch, but
     * Mach_ContextSwitch will do a return to retPC + 8, so we subtract
     * 8 from it here to get to the right place.
     */
    *((Address *)(((Address)stackPtr) + MACH_RETPC_OFFSET)) =
	    ((Address)startFunc) - 8;

    /*
     * Set the psr to restore to have traps enabled and interrupts off.
     */
    stackPtr->curPsr = MACH_HIGH_PRIO_PSR;
    /* 
     * Set up the state of the process.  User processes inherit from their
     * parent or the migrated process.  If the PC is not specified, take it
     * from the parent as well.
     */
    if (user) {
	/*
	 * Trap state regs are the same for child process.
	 */
	statePtr->trapRegs = (Mach_RegState *)
		(((Address) stackPtr) + MACH_SAVED_STATE_FRAME);
	bcopy((Address)fromStatePtr->trapRegs, (Address)statePtr->trapRegs,
		sizeof (Mach_RegState));
	/*
	 * Check to see if any register windows were saved to internal buffer
	 * in Mach_FlushWindowsToStack(), above.  If so, copy the buffer state
	 * to the new process, so that when it returns from the fork trap, it
	 * will copy out the saved windows to its stack.
	 */
	if (fromStatePtr->savedMask != 0) {
	    procPtr->specialHandling = 1;
	    statePtr->savedMask = fromStatePtr->savedMask;
	    bcopy((Address) fromStatePtr->savedRegs,
		    (Address) statePtr->savedRegs,
		    sizeof (fromStatePtr->savedRegs));
	    bcopy((Address) fromStatePtr->savedSps,
		    (Address) statePtr->savedSps,
		    sizeof (fromStatePtr->savedSps));
	}
    }
    if (startPC == (Address)NIL) {
	*((Address *)(((Address)stackPtr) + MACH_ARG0_OFFSET)) =
		(Address) fromStatePtr->trapRegs->pc;
    } else {
	/*
	 * The first argument to startFunc is supposed to be startPC.  But that
	 * would be in an in register in startFunc's window which is one before
	 * Mach_ContextSwitch's window.  But startFunc will do a save operation
	 * at the beginning so it will actually be executing in Mach_ContextS's
	 * window, so arg0 to startFunc must actually be arg0 that is restored
	 * at the end of Mach_ContextSwitch, so we have to put it in the in
	 * register area of Mach_RegState area on the stack.  Weird.
	 */
	*((Address *)(((Address)stackPtr) + MACH_ARG0_OFFSET)) = startPC;
    }
    return(SUCCESS);
}

/*
 *----------------------------------------------------------------------
 *
 * Mach_SetReturnVal --
 *
 *	Set the return value for a process from a system call.  Intended to
 *	be called by the routine that starts a user process after a fork.
 *	Interrupts must be off here!
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Register D0 is set in the user registers.
 *
 *----------------------------------------------------------------------
 */ 
void
Mach_SetReturnVal(procPtr, retVal, retVal2)
    Proc_ControlBlock	*procPtr;	/* Process to set return value for. */
    int			retVal;		/* Value for process to return. */
    int			retVal2;	/* 2nd Value for process to return. */
{
    if (procPtr->machStatePtr->trapRegs == (Mach_RegState *) NIL ||
	    procPtr->machStatePtr->trapRegs == (Mach_RegState *) 0) {
	return;
    }
    procPtr->machStatePtr->trapRegs->ins[0] = retVal;
    procPtr->machStatePtr->trapRegs->ins[1] = retVal2;
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_StartUserProc --
 *
 *	Start a user process executing for the first time.
 *	Interrupts must be off here!
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stack pointer and the program counter set for the process and
 *	the current process's image is replaced.
 *
 *----------------------------------------------------------------------
 */
void
Mach_StartUserProc(procPtr, entryPoint)
    Proc_ControlBlock	*procPtr;	/* Process control block for process
					 * to start. */
    Address		entryPoint;	/* Where process is to start
					 * executing. */
{
    register	Mach_State	*statePtr;

    statePtr = procPtr->machStatePtr;
    /*
     * MachRunUserProc will put the values from our trap regs into the actual
     * registers so that we'll be in shape to rett back to user mode.
     */
    Mach_DisableIntr();

    /*
     * Return from trap pc.
     */
    statePtr->trapRegs->pc = (unsigned int) entryPoint;
    statePtr->trapRegs->nextPc = (unsigned int) (entryPoint + 4);

    MachRunUserProc();
    /* THIS DOES NOT RETURN */
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_ExecUserProc --
 *
 *	Replace the calling user process's image with a new one.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stack pointer set for the process.
 *
 *----------------------------------------------------------------------
 */
void
Mach_ExecUserProc(procPtr, userStackPtr, entryPoint)
    Proc_ControlBlock	*procPtr;		/* Process control block for
						 * process to exec. */
    Address		userStackPtr;		/* Stack pointer for when the
						 * user process resumes 
						 * execution. */
    Address		entryPoint;		/* Where the user process is
						 * to resume execution. */
{
    Mach_RegState	tmpTrapState;

     /*
      * We do not call DISABLE_INTR here because there's an implicit enable
      * of interrupts in MachRunUserProc().
      */
    Mach_DisableIntr();
    machCurStatePtr = procPtr->machStatePtr;
    /*
     * Since we're not returning, we can just use this space on our kernel
     * stack as trapRegs.  This is safe, since we only fill in the fp, tbr,
     * pc, and nextPc fields (in Mach_StartUserProc()) and these just touch
     * the saved-window section of our stack and won't mess up any of our
     * arguments.
     */
    procPtr->machStatePtr->trapRegs = &tmpTrapState;
    /*
     * The user stack pointer gets MACH_FULL_STACK_FRAME subtracted from it
     * so that the user stack has space for its first routine to store its
     * arguments in its caller's stack frame.  (So we create a fake caller's
     * stack frame this way.)
     */
    procPtr->machStatePtr->trapRegs->ins[MACH_FP_REG] = (unsigned int)
	    (userStackPtr - MACH_FULL_STACK_FRAME);
    procPtr->machStatePtr->trapRegs->curPsr = MACH_FIRST_USER_PSR;
    procPtr->machStatePtr->trapRegs->pc = (unsigned int) entryPoint;
    procPtr->machStatePtr->trapRegs->tbr = (unsigned int) machTBRAddr;
    /*
     * Initialized the floating point state.
     */
    procPtr->machStatePtr->fpuStatus = 0;
    /*
     * Return value is cleared for exec'ing user process.  This shouldn't
     * matter since a good exec won't return.
     */
    procPtr->machStatePtr->trapRegs->ins[0] = 0;
    Mach_StartUserProc(procPtr, entryPoint);
    /* THIS DOES NOT RETURN */
}

/*
 *----------------------------------------------------------------------
 *
 * Mach_FreeState --
 *
 *	Free up the machine state for the given process control block.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Free up the kernel stack.
 *
 *----------------------------------------------------------------------
 */
void
Mach_FreeState(procPtr)
    Proc_ControlBlock	*procPtr;	/* Process control block to free
					 * machine state for. */
{

    if (procPtr->machStatePtr->kernStackStart != (Address)NIL) {
	Vm_FreeKernelStack(procPtr->machStatePtr->kernStackStart);
	procPtr->machStatePtr->kernStackStart = (Address)NIL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_GetDebugState --
 *
 *	Extract the appropriate fields from the machine state struct
 *	and store them into the debug struct.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Debug struct filled in from machine state struct.
 *
 *----------------------------------------------------------------------
 */ 
void
Mach_GetDebugState(procPtr, debugStatePtr)
    Proc_ControlBlock	*procPtr;
    Proc_DebugState	*debugStatePtr;
{
    register	Mach_State	*machStatePtr;

    machStatePtr = procPtr->machStatePtr;
    bcopy((Address)machStatePtr->trapRegs,
	      (Address)(&debugStatePtr->regState), sizeof(Mach_DebugState));
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_SetDebugState --
 *
 *	Extract the appropriate fields from the debug struct
 *	and store them into the machine state struct.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Machine state struct filled in from the debug state struct.
 *
 *----------------------------------------------------------------------
 */ 
void
Mach_SetDebugState(procPtr, debugStatePtr)
    Proc_ControlBlock	*procPtr;
    Proc_DebugState	*debugStatePtr;
{
    register	Mach_State	*machStatePtr;

/* y, pc's g1-g7 all in's*/
    machStatePtr = procPtr->machStatePtr;
    machStatePtr->trapRegs->pc = debugStatePtr->regState.pc;
    machStatePtr->trapRegs->nextPc = debugStatePtr->regState.nextPc;
    machStatePtr->trapRegs->y = debugStatePtr->regState.y;
    bcopy((Address)debugStatePtr->regState.ins,
	    (Address)machStatePtr->trapRegs->ins, MACH_NUM_INS * sizeof (int));
    bcopy((Address)debugStatePtr->regState.globals,
	    (Address)machStatePtr->trapRegs->globals,
	    MACH_NUM_GLOBALS * sizeof (int));
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_InitSyscall --
 *
 *	During initialization, this procedure is called once for each
 *	kernel call, in order to set up information used to dispatch
 *	the kernel call.  This procedure must be called once for each
 *	kernel call, in order starting at 0.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes the dispatch tables for the kernel call.
 *
 *----------------------------------------------------------------------
 */
void
Mach_InitSyscall(callNum, numArgs, normalHandler, migratedHandler)
    int callNum;			/* Number of the system call. */
    int numArgs;			/* Number of one-word arguments passed
					 * into call on stack. */
    ReturnStatus (*normalHandler)();	/* Procedure to process kernel call
					 * when process isn't migrated. */
    ReturnStatus (*migratedHandler)();	/* Procedure to process kernel call
					 * for migrated processes. */
{
    machMaxSysCall++;
    if (machMaxSysCall != callNum) {
	printf("Warning: out-of-order kernel call initialization, call %d\n",
	       callNum);
    }
    if (machMaxSysCall >= SYS_NUM_SYSCALLS) {
	printf("Warning: too many kernel calls.\n");
	machMaxSysCall--;
	return;
    }
    if (numArgs > SYS_MAX_ARGS) {
	printf("Warning: too many arguments to kernel call %d\n", callNum);
	numArgs = SYS_MAX_ARGS;
    }
    /*
     * Offset of beginning of args on stack - offset from frame pointer.
     * Figure out offset  from fp of params past the sixth.
     * It copies from last arg to first
     * arg in that order, so we start offset at bottom of last arg.
     */
    /*
     * TURN THESE INTO PROPER CONSTANTS!
     */
    /*
     * We copy going towards higher addresses, rather than lower, as the sun3
     * does it.  So our offset is at top of extra parameters to copy, and not
     * below them (stack-wise speaking, not address-wise speaking).
     */
    machArgOffsets[machMaxSysCall] = MACH_SAVED_WINDOW_SIZE +
	    MACH_ACTUAL_HIDDEN_AND_INPUT_STORAGE;
	
    /*
     * Where to jump to in fetching routine to copy the right amount from
     * the stack.  The fewer args we have, the farther we jump...  Figure out
     * how many are in registers, then do rest from stack.  There's instructions
     * to copy 10 words worth, for now, since 6 words worth of arguments are
     * in the input registers.  If this number changes, change machTrap.s
     * and the jump offset below!
     */
    if (numArgs <= 6) {
	machArgDispatch[machMaxSysCall] =  (Address) MachFetchArgsEnd;
    } else {
	machArgDispatch[machMaxSysCall] = (10 - (numArgs - 6))*16 +
		((Address)MachFetchArgs);
    }
    mach_NormalHandlers[machMaxSysCall] = normalHandler;
    mach_MigratedHandlers[machMaxSysCall] = migratedHandler;
}


/*
 * ----------------------------------------------------------------------------
 *
 * Mach_SetHandler --
 *
 *	This is used both for autovectored devices and for regular interrupt
 *	routines for device interrupt levels.  For autovectored devices,
 *	the routine MachVectoredInterrupt will already have been installed
 *	for the auto-vectored interrupt levels.  Then this routine should be
 *	be called with the interrrupt vector for the device and its
 *	real interrupt handler.  For non-autovectored interrupt handlers, the
 *	handler should just be installed with a vector that is the
 *	device's interrupt level.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     The exception vector table is modified.
 *
 * ----------------------------------------------------------------------------
 */
void
Mach_SetHandler(vectorNumber, handler, clientData)
    int vectorNumber;		/* Vector number that the device generates */
    int (*handler)();		/* Interrupt handling procedure */
    ClientData	clientData;	/* ClientData for interrupt callback routine. */
{

    if (vectorNumber < 0 || vectorNumber > 255) {
	panic("Warning: Bad vector number %d\n", vectorNumber);
    } else {
	machVectorTable[vectorNumber] = (Address) handler;
	machInterruptArgs[vectorNumber] = (ClientData) clientData;
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * Routines to set up and return from signal handlers.
 *
 * In order to call a handler four things must be done:
 *
 *	1) The current state of the process must be saved so that when
 *	   the handler returns a normal return to user space can occur.
 *	2) The user stack must be set up so that the signal number and the
 *	   the signal code are passed to the handler.
 *	3) Things must be set up so that when the handler returns it returns
 *	   back into the kernel so that state can be cleaned up.
 *	4) The trap stack that was created when the kernel was entered and is
 *	   used to return a process to user space must be modified so that
 *	   the signal handler is called instead of executing the
 *	   normal return.
 *
 * The last one is done by simply changing the program counter where the
 * user process will execute on return to be the address of the signal
 * handler and the user stack pointer to point to the proper place on
 * the user stack.  The first three of these are accomplished by 
 * setting up the user stack properly.  The top entry on the stack is the
 * return address where the handler will start executing upon return.  But 
 * this is just the address of a trap instruction that is stored on the stack
 * below.  Thus when a handler returns it will execute a trap instruction 
 * and drop back into the kernel. 
 */


/*
 * ----------------------------------------------------------------------------
 *
 * MachCallSigReturn --
 *
 *      Process a return from a signal handler.  Call the Sig_Return
 *	routine with appropriate args.
 *	
 * Results:
 *      None.
 *
 * Side effects:
 *	Whatever Sig_Return does.
 *
 * ----------------------------------------------------------------------------
 */
void
MachCallSigReturn()
{
    Proc_ControlBlock	*procPtr;
    Mach_State		*statePtr;
    Sig_Stack		*sigStackPtr;

    procPtr = Proc_GetCurrentProc();
    statePtr = procPtr->machStatePtr;

    sigStackPtr = &(statePtr->sigStack);
    sigStackPtr->contextPtr = &(statePtr->sigContext);

    /*
     * Take the proper action on return from a signal.
     */
    Sig_Return(procPtr, sigStackPtr);
}


/*
 * ----------------------------------------------------------------------------
 *
 * Mach_ProcessorState --
 *
 *	Determines what state the processor is in.
 *
 * Results:
 *	MACH_USER	if was at user level
 *	MACH_KERNEL	if was at kernel level
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
/*ARGSUSED*/
Mach_ProcessorStates 
Mach_ProcessorState(processor)
    int processor;	/* processor number for which info is requested */
{
    if (mach_KernelMode) {
	return(MACH_KERNEL);
    } else {
	return(MACH_USER);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * Mach_GetMachineArch --
 *
 *	Return the machine architecture (SYS_SUN2 or SYS_SUN3).
 *
 * Results:
 *	The machine architecture.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
int
Mach_GetMachineArch()
{
#       ifdef sun2
	return SYS_SUN2;
#       endif sun2

#       ifdef sun3
	return SYS_SUN3;
#       endif sun3

#	ifdef sun4
	return SYS_SUN4;
#	endif sun4
}

/*
 * ----------------------------------------------------------------------------
 *
 *  Mach_CheckSpecialHandling--
 *
 *	Forces a processor to check the special handling flag of a process.
 *	This should only be called on a multiprocessor.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
void
Mach_CheckSpecialHandling(pnum)
    int		pnum;		/* Processor number. */
{
    panic("Mach_CheckSpecialHandling called for processor %d\n",pnum);
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_GetNumProcessors() --
 *
 *	Return the number of processors in the system.  NOTE: This should
 *	really be in a machine-independent area of the mach module.  Note
 *	further: if this is used only as a system call, it should return
 *	a ReturnStatus!
 *
 * Results:
 *	The number of processors is returned.  
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int
Mach_GetNumProcessors()
{
	return (mach_NumProcessors);
}



/*
 *----------------------------------------------------------------------
 *
 * MachPageFault() -
 *
 *	Handle a page fault.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	A page causing a memory access error is made valid.  If it's an
 *	illegal page fault in the kernel, we will call panic.
 *
 *----------------------------------------------------------------------
 */
void
MachPageFault(busErrorReg, addrErrorReg, trapPsr, pcValue)
    unsigned	int	busErrorReg;
    Address		addrErrorReg;
    unsigned	int	trapPsr;
    Address		pcValue;
{
    Proc_ControlBlock	*procPtr;
    Boolean		protError;
    Boolean		copyInProgress = FALSE;
    ReturnStatus	status;
    extern		int	VmMachQuickNDirtyCopy();
    extern		int	VmMachEndQuickCopy();

    /*
     * Are we in quick cross-context copy routine?  If so, we can't page fault
     * in it.
     */
    if ((pcValue >= (Address) VmMachQuickNDirtyCopy) &&
	    (pcValue < (Address) VmMachEndQuickCopy)) {
	/*
	 * This doesn't return to here.  It erases the fact that the
	 * page fault happened and makes the copy routine that
	 * got the page fault return FAILURE to its caller.  We must turn off
	 * interrupts before calling MachHandleBadQuickCopy().
	 */
	Mach_DisableIntr();
	MachHandleBadQuickCopy();
	Mach_EnableIntr();
    }
    /*
     * Are we poking at or peeking into memory-mapped devices?
     * We must check this before looking for the current process, since this
     * can happen during boot-time before we have set up processes.
     */
    if ((pcValue >= (Address) MachProbeStart)  &&
	    (pcValue < (Address) MachProbeEnd)) {
	/*
	 * This doesn't return to here.  It erases the fact that the
	 * page fault happened and makes the probe routine that
	 * got the page fault return FAILURE to its caller.  We must turn off
	 * interrupts before calling MachHandleBadProbe().
	 */
	Mach_DisableIntr();
	MachHandleBadProbe();
	Mach_EnableIntr();
    }
#ifdef sun4
    /*
     * On the sun4/200 with the Jaguar HBA we get VME timeout errors from 
     * the board. This code retries the error up to 100000 times before droping
     * into the code below which panics. 
     * These errors happen a lot with the RAID HPPI boards, so we disable
     * the printout.
     */
    {
	static timeoutRetryCount = 0;
	extern void MachVectoredInterruptLoad();

	if ((trapPsr & MACH_PS_BIT) && (busErrorReg&MACH_TIMEOUT_ERROR)) {
	    /*
	     * If the error occurred on a the load of the interrupt
	     * vector make the routine return.
	     */
	    if (pcValue == (Address) MachVectoredInterruptLoad) {
		/*
		 * This doesn't return to here.  It erases the fact that the
		 * page fault happened and makes the MachVectoredInterrupt
		 * routine that got the page fault return FAILURE 
		 * to its caller.  
		 */
		Mach_MonPrintf(
"MachPageFault: Bus timeout error on VME interrupt vector load pc:0x%x, addr:0x%x\n",
		     pcValue, addrErrorReg);
		MachHandleBadQuickCopy();
	    }
#if 0
	    Mach_MonPrintf(
 "MachPageFault: Bus timeout error retry %d at pc:0x%x, addr:0x%x\n",
		    timeoutRetryCount, pcValue, addrErrorReg);
#endif
	    if (timeoutRetryCount < 1000000) {
		timeoutRetryCount++;
		return;
	    }
	}
	timeoutRetryCount = 0;
    }
    /* We used to enable interrupts before we called this routine, but we
     * don't want them enabled if it is a VME bus timeout, so we enable them
     * now */
    Mach_EnableIntr();
#endif /* sun4 */

    procPtr = Proc_GetActualProc();
    if (procPtr == (Proc_ControlBlock *) NIL) {
	panic(
	"MachPageFault: Current process is NIL!!  Trap pc is 0x%x, addr 0x%x\n",
		(unsigned) pcValue, addrErrorReg);
    }
    /* process kernel page fault */
    if (trapPsr & MACH_PS_BIT) {		/* kernel mode before trap */
	if (!(procPtr->genFlags & PROC_USER)) {
	    /*
	     * This fault happened inside the kernel and it wasn't on behalf
	     * of a user process.  This is an error.
	     */
	    panic(
"MachPageFault: page fault in kernel process! pc:0x%x, addr:0x%x, Error:0x%x\n",
		    pcValue, addrErrorReg, busErrorReg);
	}
	/*
	 * A page fault on a user process while executing in
	 * the kernel.  This can happen when information is
	 * being copied back and forth between kernel and user state
	 * (indicated by particular values of the program counter).
	 */
	if ((pcValue >= (Address) Vm_CopyIn) &&
		(pcValue < (Address) VmMachCopyEnd)) {
	    copyInProgress = TRUE;
	} else if ((pcValue >= (Address) MachFetchArgs) &&
		(pcValue <= (Address) MachFetchArgsEnd)) {
	    copyInProgress = TRUE;
	} else if (procPtr->vmPtr->numMakeAcc == 0) {
	    /*
	     * ERROR: pc faulted in a bad place!
	     */
	    panic(
	    "MachPageFault: kernel page fault at illegal pc: 0x%x, addr 0x%x\n",
		    pcValue, addrErrorReg);
	}
	protError = (busErrorReg & MACH_PROT_ERROR);
	/*
	 * Try to fault in the page.
	 */
	status = Vm_PageIn(addrErrorReg, protError);
	if (status != SUCCESS) {
	    if (copyInProgress) {
		/*
		 * This doesn't return to here.  It erases the fact that the
		 * page fault happened and makes the copy routine that
		 * got the page fault return SYS_ARG_NO_ACCESS to its caller.
		 * We must turn off interrupts before calling
		 * MachHandleBadArgs().
		 */
		Mach_DisableIntr();
		MachHandleBadArgs();
		Mach_EnableIntr();
	    } else {
		/* kernel error */
		panic(
	    "MachPageFault: couldn't page in kernel page at 0x%x, pc 0x%x\n",
			addrErrorReg, pcValue);
	    }
	}
	return;
    }
    /* user page fault */
    protError = busErrorReg & MACH_PROT_ERROR;
    if (Vm_PageIn(addrErrorReg, protError) != SUCCESS) {
	printf(
    "MachPageFault: Bus error in user proc %x, PC = %x, addr = %x BR Reg %x\n",
#ifdef sun4c
		procPtr->processID, pcValue, addrErrorReg, busErrorReg);
#else
		procPtr->processID, pcValue, addrErrorReg, (short) busErrorReg);
#endif
	/* Kill user process */
	Sig_Send(SIG_ADDR_FAULT, SIG_ACCESS_VIOL, procPtr->processID, FALSE,
	       (Address)addrErrorReg);
	return;
    }
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * MachUserAction() -
 *
 *	Check what sort of user-process action is needed.  We already know
 *	that some sort of action is needed, since the specialHandling flag
 *	should be checked before calling this routine.  The possible actions
 *	are to take a context switch, to push saved user windows from the mach
 *	state structure out to the user stack, or to set things up to handle
 *	pending signals.  We assume traps are enabled before this routine is
 *	called.
 *
 * Results:
 *	The return value 0 indicates we have no pending signal.
 *	The return value 1 indicates we have a pending Sprite signal.
 *	The return value 2 indicates we have a pending Unix signal.
 *
 * Side effects:
 *	The mach state structure may change, especially the mask that indicates
 *	which windows were saved into the internal buffer.
 *
 *----------------------------------------------------------------------
 */
int
MachUserAction()
{
    Proc_ControlBlock	*procPtr;
    Mach_State		*machStatePtr;
    Sig_Stack		*sigStackPtr;
    Address		pc;
    int			unixSignal;
    int			restarted=0;

    procPtr = Proc_GetCurrentProc();
    if (procPtr->unixProgress != PROC_PROGRESS_NOT_UNIX &&
            procPtr->unixProgress != PROC_PROGRESS_UNIX && debugProcStubs) {
        printf("UnixProgress = %d entering MachUserReturn\n",
		procPtr->unixProgress);
    }

HandleItAgain:
    if (procPtr->Prof_Scale != 0 && procPtr->Prof_PC != 0) {
	Prof_RecordPC(procPtr);
    }
    procPtr->specialHandling = 0;
    /*
     * Take a context switch if one is pending for this process.
     * If other stuff, such as flushing the windows to the stack needs to
     * be done, it will happen when the process is switched back in again.
     * We came from MachReturnFromTrap, where interrupts were off, so we
     * must turn them on.
     */
    Mach_EnableIntr();
    if (procPtr->schedFlags & SCHED_CONTEXT_SWITCH_PENDING) {
	Sched_LockAndSwitch();
    }

    machStatePtr = procPtr->machStatePtr;

    /*
     * Save the windows that were stored in internal buffers to the user stack.
     * The windows were saved to internal buffers due to the user stack not
     * being resident.  The overflow handler can't take page faults, but
     * we can.
     */
    if (machStatePtr->savedMask != 0) {
	int	i;

	for (i = 0; i < MACH_NUM_WINDOWS; i++) {
	    if ((1 << i) & machStatePtr->savedMask) {
		/*
		 * Clear the mask for this window.  We must turn off interrupts
		 * to do this, since changing the savedMask must be an
		 * atomic operation.  If it weren't, and an interrupt came
		 * in that caused us to save some other window to the stack
		 * after we have read the savedMask, we would overwrite the
		 * fact when storing the saved Mask...
		 */
		Mach_DisableIntr();
		machStatePtr->savedMask &= ~(1 << i);
		Mach_EnableIntr();
		/*
		 * Push the window to the stack.
		 */ 
		if (Vm_CopyOut(MACH_SAVED_WINDOW_SIZE,
			(Address)(machStatePtr->savedRegs[i]),
			(Address)(machStatePtr->savedSps[i])) != SUCCESS) {
		    printf("MachUserAction: pid 0x%x being killed: %s 0x%x.\n",
			    procPtr->processID, "bad stack pointer?",
			    machStatePtr->savedSps[i]);
		    Proc_ExitInt(PROC_TERM_DESTROYED, PROC_BAD_STACK, 0);
		}
	    }
	}
    }
    /*
     * We must check again here to see if the specialHandling flag has been
     * set again.  We've been taking interrupts this whole time, and the
     * Vm code above may have called deeply, so we may have saved further
     * user windows to internal buffers.  If we have, go back up to the
     * beginning of the routine, and do this all again.
     */

    /*
     * For now, we also flush all the windows to make sure the signal-handling
     * stuff below won't cause us to save windows to internal buffers.  This
     * is a slow thing to do, but it is currenlty unclear what to do if
     * the signal handler causes a window to get saved to an internal buffer.
     * It will have to call some sort of MachUserAction-type routine itself
     * in that case.
     */
    Mach_DisableIntr();
    Mach_FlushWindowsToStack();
    if (procPtr->specialHandling != 0) {
	goto HandleItAgain;
    }
    Mach_EnableIntr();
    /*
     * Check for floating point problems.
     */
    if (machStatePtr->fpuStatus & MACH_FPU_EXCEPTION_PENDING) {
	HandleFPUException(procPtr, machStatePtr);
    }
    /*
     * Now check for signal stuff. We must check again for floating
     * point exception because the Sig_Handle might do a context switch
     * during which the excpetion would get posted. 
     * 
     * Note: This is really wrong.  We should check for and process 
     * any floating point exceptions before  handling a signal. 
     * The problem here is by the time Sig_Handle returns we are
     * already committed to doing this signal.
     */
    sigStackPtr = &(machStatePtr->sigStack);
    sigStackPtr->contextPtr = &(machStatePtr->sigContext);
    if (procPtr->unixProgress == PROC_PROGRESS_RESTART ||
	    procPtr->unixProgress > 0) {
	/*
	 * If we received a normal signal, we want to restart
	 * the system call when we leave.
	 * If we received a migrate signal, we will get here on
	 * the new machine.
	 * We must also ensure that the argument registers are the
	 * same as when we came in.
	 */
	restarted = 1;
	if (debugProcStubs) {
	    printf("Restarting system call with progress %d\n",
		    procPtr->unixProgress);
	}
	procPtr->unixProgress = PROC_PROGRESS_UNIX;
    }
    if (Sig_Handle(procPtr, sigStackPtr, &pc)) {
	machStatePtr->sigContext.machContext.pcValue = pc;
	machStatePtr->sigContext.machContext.trapInst = MACH_SIG_TRAP_INSTR;
	/* leave interrupts disabled */
	if (machStatePtr->fpuStatus & MACH_FPU_EXCEPTION_PENDING) {
	    HandleFPUException(procPtr, machStatePtr);
	}
	Mach_DisableIntr();
	if (procPtr->unixProgress != PROC_PROGRESS_NOT_UNIX) {
	    /*
	     * We have to build a proper Unix signal stack.
	     */
	    int n[16];
	    struct sigcontext	unixContext;
	    procPtr->unixProgress = PROC_PROGRESS_UNIX;
	    if (Compat_SpriteSignalToUnix(sigStackPtr->sigNum,
		    &unixSignal) != SUCCESS) {
		printf("Signal %d invalid in SetupSigHandler\n",
			sigStackPtr->sigNum);
		return 0;
	    }

	    if (restarted) {
		if (debugProcStubs) {
		    printf("Moving PC to restart system call (doing signal\n");
		}
		machStatePtr->trapRegs->nextPc = machStatePtr->trapRegs->pc;
		machStatePtr->trapRegs->pc = machStatePtr->trapRegs->nextPc-4;
		/*
		 * We need to restore %o0 which got clobbered by
		 * the system call.
		 */
		machStatePtr->trapRegs->ins[0] = machStatePtr->savedArgI0;
	    }

	    if (debugProcStubs) {
		printf("Unix signal %d(%d) to %x\n", sigStackPtr->sigNum,
			unixSignal, procPtr->processID);
	    }
	    sigStackPtr->sigNum = unixSignal;
	    unixContext.sc_onstack = 0;
	    unixContext.sc_mask = machStatePtr->sigContext.oldHoldMask;
	    unixContext.sc_sp = machStatePtr->trapRegs->ins[6];
	    /*
	     * pc and npc are where to continue the interrupted routine.
	     */
	    if (debugProcStubs) {
		printf("PSR = %x\n", machStatePtr->trapRegs->curPsr);
	    }
	    unixContext.sc_pc = machStatePtr->trapRegs->pc;
	    unixContext.sc_npc = machStatePtr->trapRegs->nextPc;
	    if (debugProcStubs) {
		printf("trapRegs->pc=%x, trapRegs->npc=%x, context.pcValue=%x\n",
			machStatePtr->trapRegs->pc, machStatePtr->trapRegs->nextPc,
			machStatePtr->sigContext.machContext.pcValue);
	    }
	    unixContext.sc_psr = machStatePtr->trapRegs->curPsr;
	    unixContext.sc_g1 = machStatePtr->trapRegs->globals[1];
	    unixContext.sc_o0 = machStatePtr->trapRegs->ins[0];
	    /*
	     * machContext.pcValue is the address of the handler.
	     */
	    machStatePtr->trapRegs->pc = (unsigned int)
		    machStatePtr->sigContext.  machContext.pcValue;
	    machStatePtr->trapRegs->nextPc = (unsigned int)
		    machStatePtr->sigContext.machContext.pcValue+4;
	    if (debugProcStubs) {
		printf("new pc = %x\n", machStatePtr->trapRegs->nextPc);
	    }
	    /*
	     * Copy the window to the signal stack.
	     */
	    Vm_CopyIn(16*sizeof(int), (Address)unixContext.sc_sp, (Address)n);
	    if (debugProcStubs) {
		printf("Regs: %x %x %x, %x %x %x\n", n[0], n[1], n[2], n[8],
			n[9], n[10]);
	    }
	    machStatePtr->trapRegs->ins[6] += MACH_SAVED_WINDOW_SIZE;
	    unixContext.sc_wbcnt = 0;
	    sigStackPtr->contextPtr = (Sig_Context *)
		    (unixContext.sc_sp-sizeof(struct sigcontext));
	    if (Vm_CopyOut(MACH_SAVED_WINDOW_SIZE,
		    (Address)n,
		    (Address)unixContext.sc_sp - sizeof(struct sigcontext)
			    - 4*sizeof(int) - MACH_SAVED_WINDOW_SIZE) !=
			    SUCCESS) {
		return 0;
	    }
	    if (debugProcStubs) {
		printf("Copied window to %x\n",
			(Address)unixContext.sc_sp - sizeof(struct sigcontext)
				- 4*sizeof(int) - MACH_SAVED_WINDOW_SIZE);
	    }
	    /*
	     * Copy the sigStack and sigContext to the signal window.
	     */
	    if (Vm_CopyOut(4*sizeof(int), (Address)sigStackPtr,
		    (Address)unixContext.sc_sp - sizeof(struct sigcontext)
			    - 4*sizeof(int)) != SUCCESS) {
		return 0;
	    }
	    if (Vm_CopyOut(sizeof(struct sigcontext), (Address)&unixContext,
		    (Address)unixContext.sc_sp - sizeof(struct sigcontext))
			    != SUCCESS) {
		return 0;
	    }
	    return 2;
	} else {
	    return 1;
	}
    } else {
	if (procPtr->unixProgress == PROC_PROGRESS_MIG_RESTART ||
                    procPtr->unixProgress == PROC_PROGRESS_RESTART) {
	    restarted = 1;
	    if (debugProcStubs) {
		printf("No signal action, so we restarted call\n");
	    }
	    procPtr->unixProgress = PROC_PROGRESS_UNIX;
	} else if (restarted && debugProcStubs) {
	    printf("No signal, yet we restarted system call!\n");
	}
    }

    if (machStatePtr->fpuStatus & MACH_FPU_EXCEPTION_PENDING) {
	HandleFPUException(procPtr, machStatePtr);
    }
    /*
     * It is possible for Sig_Handle to mask the migration signal
     * if a process is not in a state where it can be migrated.
     * As soon as we return to user mode, though, we will allow migration.
     * Clear the bit anytime something's pending, for simplicity.
     */
    if (procPtr->sigPendingMask) {
	Sig_AllowMigration(procPtr);
    }

    if (restarted) {
	if (debugProcStubs) {
	    printf("Moving PC to restart system call (no signal\n");
	}
	machStatePtr->trapRegs->nextPc = machStatePtr->trapRegs->pc;
	machStatePtr->trapRegs->pc = machStatePtr->trapRegs->nextPc-4;
	/*
	 * We need to restore %o0 which got clobbered by
	 * the system call.
	 */
	machStatePtr->trapRegs->ins[0] = machStatePtr->savedArgI0;
	procPtr->unixProgress = PROC_PROGRESS_UNIX;
    }

    if (procPtr->unixProgress != PROC_PROGRESS_NOT_UNIX &&
            procPtr->unixProgress != PROC_PROGRESS_UNIX && debugProcStubs) {
        printf("UnixProgress = %d leaving MachUserReturn\n",
		procPtr->unixProgress);
    }

    /*
     * Go back to MachReturnFromTrap.  We are expected to have interrupts
     * off there.
     */
    Mach_DisableIntr();
	
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * MachHandleTrap --
 *
 *	Handle an instruction trap, such as an illegal instruction trap,
 *	an unaligned address, or a floating point problem.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If it occured in the kernel, we panic.  If it occured in a user
 *	program, we take appropriate signal action.
 *
 *----------------------------------------------------------------------
 */
void
MachHandleTrap(trapType, pcValue, trapPsr)
    int			trapType;
    Address		pcValue;
    unsigned	int	trapPsr;
{
    Proc_ControlBlock	*procPtr;

    /*
     * Find the current process.  If we took a MACH_FP_EXCEP at one of the
     * marked FPU sync instructions, then we use the process saved
     * in machFPUSaveProcPtr.
     */
    procPtr = ((trapType == MACH_FP_EXCEP) && 
	       (pcValue == (Address) machFPUSyncInst)) ?
		   machFPUSaveProcPtr : Proc_GetCurrentProc();
    if ((procPtr == (Proc_ControlBlock *) NIL)) {
	    printf("%s: pc = 0x%x, trapType = %d\n",
		"MachHandleTrap", pcValue, trapType);
	    panic("Current process was NIL!\n");
    }
    /*
     * Handle kernel-mode traps.
     */
    if (trapPsr & MACH_PS_BIT) {
	switch (trapType) {
	case MACH_ILLEGAL_INSTR:
	    printf("%s %s\n", "MachHandleTrap: illegal",
		    "instruction trap in the kernel!");
	    break;
	case MACH_PRIV_INSTR:
	    printf("%s %s\n", "MachHandleTrap: privileged",
		    "instruction trap in the kernel!");
	    break;
	case MACH_MEM_ADDR_ALIGN:
	    printf("%s %s\n", "MachHandleTrap: unaligned",
		    "address trap in the kernel!");
	    break;
	case MACH_TAG_OVERFLOW:
	    printf("%s %s\n", "MachHandleTrap: tag",
		    "overflow trap in the kernel!");
	    break;
	case MACH_FP_EXCEP: {
#ifndef NO_FLOATING_POINT
	    /*
	     * We got a FP execption while running in kernel mode. If this
	     * exception occured at a known location we clear the
	     * exception and mark the Mach_State.
	     */
	    if (pcValue == (Address) machFPUDumpSyncInst) {
		/*
		 * Already doing a MachFPUDumpState.  Whoever's doing the
		 * dump should check the pending flag and set fpuStatus if
		 * it's set.
		 */
		procPtr->machStatePtr->fpuStatus |=
		    MACH_FPU_EXCEPTION_PENDING;
		return;
	    }
	    if (pcValue == (Address) machFPUSyncInst) {
		  MachFPUDumpState(procPtr->machStatePtr->trapRegs);
		  procPtr->machStatePtr->fpuStatus |= 
		      (procPtr->machStatePtr->trapRegs->fsr
					& MACH_FSR_TRAP_TYPE_MASK) |
			  MACH_FPU_EXCEPTION_PENDING;
		  procPtr->specialHandling = 1;
		  return;
	    } 
	    printf("%s. ",
	"MachHandleTrap: FPU exception from kernel process.");
	    break;
#else /* NO_FLOATING_POINT */
	    printf("Floating point op in kernel code: not supported in this\n");
	    panic("kernel due to copyright reasons.");
#endif /* NO_FLOATING_POINT */
	}
	case MACH_FP_DISABLED:
	    printf("%s %s\n", "MachHandleTrap: fp unit",
		    "disabled trap in the kernel!");
	    break;
	default:
	    printf("%s %s\n", "MachHandleTrap: hit default",
		    "in case statement - bad trap instruction called us!");
	    break;
	}
	panic("%s %s %s %x %s %x\n",
		"MachHandleTrap: the error occured in a",
		procPtr->genFlags & PROC_USER ? "user" : "kernel",
		"process, with procPtr =", (unsigned int) procPtr,
		"and pc =", pcValue);
    }
    /*
     * The trap occured in user-mode.
     */
    switch (trapType) {
    case MACH_ILLEGAL_INSTR:
	(void) Sig_Send(SIG_ILL_INST, SIG_ILL_INST_CODE, procPtr->processID,
		FALSE, pcValue);
	break;
    case MACH_PRIV_INSTR:
	(void) Sig_Send(SIG_ILL_INST, SIG_PRIV_INST, procPtr->processID,
		FALSE, pcValue);
	break;
    case MACH_MEM_ADDR_ALIGN:
	(void) Sig_Send(SIG_ADDR_FAULT, SIG_ADDR_ERROR, procPtr->processID,
		FALSE, (Address)0);
	break;
    case MACH_FP_EXCEP: {
         unsigned int fsr;
	 /*
	  * An FP exception from user mode.  Clear the exception and 
	  * mark it in the Mach_State struct.  
	  * 
	  */
	 MachFPUDumpState(procPtr->machStatePtr->trapRegs);
	 fsr = procPtr->machStatePtr->trapRegs->fsr;
	 if (!(procPtr->machStatePtr->fpuStatus & MACH_FPU_ACTIVE)) {
		 printf(
"FPU exception from process without MACH_FPU_ACTIVE, fsr = 0x%x\n",fsr);
	 }
	 procPtr->machStatePtr->fpuStatus |= (fsr & MACH_FSR_TRAP_TYPE_MASK) |
					    MACH_FPU_EXCEPTION_PENDING;
	 procPtr->specialHandling = 1;
	 break;
	}
    case MACH_FP_DISABLED: {
	register Mach_State 	*machStatePtr;

	Mach_FlushWindowsToStack();
	machStatePtr = procPtr->machStatePtr;
	/*
	 * Upon a user's first FPU disable trap we initialize and enable
	 * the FPU for him. 
	 */
	if (machStatePtr->fpuStatus & MACH_FPU_ACTIVE) {
	    panic("Double FPU_DISABLE trap.\n");
	}
	machStatePtr->fpuStatus = MACH_FPU_ACTIVE;
	/*
	 * Enable the FPU in the trap PSR.
	 */
	machStatePtr->trapRegs->curPsr |= MACH_ENABLE_FPP;
	/*
	 * Initialize the FPU registers. 
	 */
	machStatePtr->trapRegs->fsr = 0;
	bzero((Address) (machStatePtr->trapRegs->fregs), MACH_NUM_FPS*4);
	MachFPULoadState(machStatePtr->trapRegs);
	break;
	}
    case MACH_TAG_OVERFLOW:
	panic("%s %s\n", "MachHandleTrap: tag",
		"overflow trap in user process, but I don't deal with it yet.");
	break;
    default:
	panic("%s %s\n", "MachHandleTrap: hit default",
		"in case statement - bad trap instruction from user mode.");
	break;

    }
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * FlushTheWindows --
 *
 *	A recursive C routine that will force window overflows and thereby
 *	flush the register windows to the stack.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The register windows are flushed.
 *
 *----------------------------------------------------------------------
 */
static void
FlushTheWindows(num)
    int	num; 
{
    num--;
    if (num > 0) {
	FlushTheWindows(num);
    }
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_FlushWindowsToStack --
 *
 *	Calls a routine to flush the register windows to the stack.
 *	This routine can be caled from traps, or wherever.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The register windows are flushed.
 *
 *----------------------------------------------------------------------
 */
void
Mach_FlushWindowsToStack()
{
    /*
     * We want to do NWINDOWS - 1 saves and then restores to make sure all our
     * register windows have been saved to the stack.  Calling here does one
     * save, so we want to do NWINDOWS - 2 more calls and returns.
     */
    FlushTheWindows(MACH_NUM_WINDOWS - 2);
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * MachUserDebug --
 *
 *	This will cause the current process to go into the debugger.  It can
 *	be called from trap handlers, etc.  It first checks to see if the
 *	current process is NIL.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The process gets a breakpoint signal.
 *
 *----------------------------------------------------------------------
 */
void
MachUserDebug()
{
    Proc_ControlBlock	*procPtr; 

    procPtr = Proc_GetCurrentProc();
    if (procPtr == (Proc_ControlBlock *) NIL) {
	panic("MachUserDebug: current process was NIL!\n");
    }
    Sig_Send(SIG_BREAKPOINT, SIG_NO_CODE, procPtr->processID, FALSE,
	    (Address)0);
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_GetBootArgs --
 *
 *	Returns the arguments out of the boot parameter structure. 
 *
 * Results:
 *	Number of elements returned in argv.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Mach_GetBootArgs(argc, bufferSize, argv, buffer)
	int	argc;			/* Number of elements in argv */
	int	bufferSize;		/* Size of buffer */
	char	**argv;			/* Ptr to array of arg pointers */
	char	*buffer;		/* Storage for arguments */
{
#ifdef sun4c
    if (romVectorPtr->v_romvec_version < 2) {
#endif
	int		i;
	int		offset;
	bcopy(machMonBootParam.strings, buffer, 
	      (bufferSize < 100) ? bufferSize : 100);
	offset = (unsigned int) machMonBootParam.strings -(unsigned int) buffer;
	for(i = 0; i < argc; i++) {
	    if (machMonBootParam.argPtr[i] == (char *) 0 ||
		machMonBootParam.argPtr[i] == (char *) NIL) {
		break;
	    }
	    argv[i] = (char *) (machMonBootParam.argPtr[i] - (char *) offset);
	}
	return i;
#ifdef sun4c
    } else {
	char	*bufEndPtr = buffer + bufferSize - 1;
	char	*bufferPtr = buffer;
	int	argcsLeft = argc;
	char	*p;

	/*
	 * On version 2 and greater the bootstring is stored in 
	 * two null terminated strings.  We copy these strings
	 * into the argc,argv format needed by Mach_GetBootArgs.
	 */
	if (argc == 0) {
	    return 0;
	}
	p = *(romVectorPtr->bootpath);
	while (*p && isspace(*p)) { /* Skip any spaces. */
		p++;
	}
	*argv = bufferPtr;
	argv++; argcsLeft--;
	while (*p && (bufferPtr < bufEndPtr)) {
	    *bufferPtr++ = *p++;
	}
	*bufferPtr++ = 0;

	p = *(romVectorPtr->bootargs);
	while (*p && isspace(*p)) {  /* Skip any spaces. */
	    p++;
	}
	while ((bufferPtr < bufEndPtr) && (argcsLeft > 0) && *p) {
	    *argv = bufferPtr;
	    argv++; argcsLeft--;
	    while (*p && !isspace(*p) && (bufferPtr < bufEndPtr)) {
		*bufferPtr++ = *p++;
	    }
	    *bufferPtr++ = 0;
	    while (*p && isspace(*p)) {  /* Skip any spaces. */
		p++;
	    }
        }
	return argc - argcsLeft;
    }
#endif /* sun4c */
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_GetStackPointer --
 *
 *	This is a stub routine for the sun4.
 *
 * Results:
 *	Address.
 *
 * Side effects:
 *	It panics since it should never be called.  If it ends up being
 *	useful someday, change it so it doesn't panic.
 *
 *----------------------------------------------------------------------
 */
Address
Mach_GetStackPointer()
{
	panic("Mach_GetStackPointer");
	return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * HandleFPUException --
 *
 *	Handle any FPU exception present.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	FPU instruction emulated, process may be sent signal.
 *
 *----------------------------------------------------------------------
 */

static void
HandleFPUException(procPtr, machStatePtr)
    Proc_ControlBlock *procPtr;	/* Process control block of offending process*/
    Mach_State 	*machStatePtr;  /* Machine state of process. */
{    
    int		i;
    Mach_RegWindow	*curWindow;

    switch ((int) (machStatePtr->fpuStatus & MACH_FSR_TRAP_TYPE_MASK)) {
	case	MACH_FSR_IEEE_TRAP:
	case	MACH_FSR_UNFINISH_TRAP:
	case	MACH_FSR_UNIMPLEMENT_TRAP:
	    break;
	case	MACH_FSR_SEQ_ERRROR_TRAP: {
	    panic("Floating point sequence error, fsr = 0x%x\n",
		 machStatePtr->trapRegs->fsr);
	    break;
	}
	case	MACH_FSR_NO_TRAP:
	default: {
	    panic("Floating point exception with bad trap code, fsr = 0x%x\n", 
		    machStatePtr->trapRegs->fsr);
	    break;
	}
    }
    machStatePtr->fpuStatus &= 
		~(MACH_FPU_EXCEPTION_PENDING|MACH_FSR_TRAP_TYPE_MASK);
    /*
     * Emulate the evil instructions, and restore the result into the FPU.
     */
    curWindow = (Mach_RegWindow *)
		(machStatePtr->trapRegs->ins[MACH_FP_REG]);
    for (i = 0; i < machStatePtr->trapRegs->numQueueEntries; i++) {
#ifndef NO_FLOATING_POINT
	MachFPU_Emulate(procPtr->processID, 
		    machStatePtr->trapRegs->fqueue[i].address,
		    machStatePtr->trapRegs,
		    curWindow);
#else /* NO_FLOATING_POINT */
	printf(
	"Cannot emulate floating point operations in this kernel version\n");
	printf("Killing process 0x%x\n", procPtr->processID);
	(void) Sig_Send(SIG_ILL_INST, SIG_ILL_INST_CODE, procPtr->processID,
			FALSE, machStatePtr->trapRegs->pc);
#endif /* NO_FLOATING_POINT */
    }
    MachFPULoadState(machStatePtr->trapRegs);
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_SigreturnStub --
 *
 *	Return from a unix signal or long jump.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes control of execution.
 *
 *----------------------------------------------------------------------
 */
int
Mach_SigreturnStub(jmpBuf)
jmp_buf *jmpBuf;
{
    struct sigcontext context;
    Proc_ControlBlock	*procPtr = Proc_GetCurrentProc();
    Mach_State		*machStatePtr = procPtr->machStatePtr;

    if (Vm_CopyIn(9*sizeof(int), (Address)jmpBuf, (Address)&context) !=
	    SUCCESS) {
	printf("jmp_buf copy in failure\n");
	return -1;
    }
    if (debugProcStubs) {
	printf("Unix sigreturn: pc = %x, sp = %x, psr = %x\n", context.sc_pc,
		context.sc_sp, context.sc_psr&MACH_DISABLE_TRAP_BIT &
		~MACH_PS_BIT);
    }

    /*
     * Flush register windows to stack and maybe our problems will go away.
     */
    Mach_DisableIntr();
    Mach_FlushWindowsToStack();
    Mach_EnableIntr();

    machStatePtr->trapRegs->pc = context.sc_pc;
    machStatePtr->trapRegs->nextPc = context.sc_npc;

    machStatePtr->trapRegs->globals[1] = context.sc_g1;
    machStatePtr->trapRegs->ins[0] = context.sc_o0;
    machStatePtr->sigContext.oldHoldMask = context.sc_mask;
    machStatePtr->trapRegs->curPsr = (machStatePtr->trapRegs->curPsr&
	    ~MACH_PSR_SIG_RESTORE) | (context.sc_psr&MACH_PSR_SIG_RESTORE);
    machStatePtr->trapRegs->ins[6] = context.sc_sp;
    Sig_Return(procPtr, &machStatePtr->sigStack);
    return 0; /* Dummy */
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_FastBoot --
 *
 *	Do a fast reboot (using copied initialized heap data, etc.)
 *
 * Results:
 *	FAILURE if we're not set up to do a fast boot.  Otherwise, we don't
 *	return, but boot instead.
 *
 * Side effects:
 *	Will probably cause fast reboot.
 *
 *----------------------------------------------------------------------
 */
ReturnStatus
Mach_FastBoot()
{
    if (!recov_DoInitDataCopy) {
	printf("Can fast reboot: initialized data wasn't copied.");
	return FAILURE;
    }
    MachDoFastBoot();
    return FAILURE;		/* Should never get here. */
}


/*
 *----------------------------------------------------------------------
 *
 * Mach_GetMachineType --
 *
 *	Get the machine type from the idprom.
 *
 * Results:
 *	The machine type.
 *
 * Side effects:
 *	Reads from PROM the first time.
 *
 *----------------------------------------------------------------------
 */
#ifdef sun4c
int
Mach_GetMachineType()
{
    if (machIdProm.id_format != IDFORM_1) {
	if (Mach_MonSearchProm("*", "idprom", (char *)&machIdProm,
		sizeof machIdProm) != sizeof machIdProm) {
	    panic("Where is the idprom?");
	}
    }
    return machIdProm.id_machine;
}
#endif /* sun4c */

/*
 *----------------------------------------------------------------------
 *
 * Mach_GetEtherAddress --
 *
 *	Get the ethernet address.
 *
 * Results:
 *	A pointer to the ethernet address.
 *
 * Side effects:
 *	Reads from PROM the first time.
 *
 *----------------------------------------------------------------------
 */
#ifdef sun4c
Net_EtherAddress *
Mach_GetEtherAddress(etherAddressPtr)
    Net_EtherAddress *etherAddressPtr;
{
    if (machIdProm.id_format != IDFORM_1) {
	if (Mach_MonSearchProm("*", "idprom", (char *)&machIdProm,
		sizeof machIdProm) != sizeof machIdProm) {
	    panic("Where is the idprom?");
	}
    }
    bcopy((char *)machIdProm.id_ether, (char *)etherAddressPtr,
	sizeof(Net_EtherAddress));
    return etherAddressPtr;	/* which copy should I return? */
}
#endif /* sun4c */
