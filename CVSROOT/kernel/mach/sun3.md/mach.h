/*
 * mach.h --
 *
 *     Exported structures for the mach module.
 *
 * Copyright (C) 1985 Regents of the University of California
 * All rights reserved.
 *
 * $Header$ SPRITE (Berkeley)
 */

#ifndef _MACH
#define _MACH

#include "devAddrs.h"
#include "machConst.h"

/*
 * The state of each processor: user mode or kernel mode.
 */
typedef enum {
    MACH_USER,
    MACH_KERNEL
} Mach_ProcessorStates;

/*
 * Structure for a Mach_SetJump and Mach_LongJump.
 */
typedef struct Mach_SetJumpState {
    int		pc;
    int		regs[12];
} Mach_SetJumpState;

/*
 * Macro to perform a set jump.
 */
#define	Mach_SetJump(setJumpPtr) \
    MachSetJump((Proc_GetCurrentProc())->machStatePtr->setJumpStatePtr = setJumpPtr)

/*
 * Macros to disable and enable interrupts.
 */
#define	Mach_DisableIntr()	asm("movw #0x2700,sr")
#define	Mach_EnableIntr()	asm("movw #0x2000,sr")
#define DISABLE_INTR() \
    if (!mach_AtInterruptLevel) { \
	Mach_DisableIntr(); \
	if (mach_NumDisableIntrsPtr[0] < 0) { \
	    Sys_Panic(SYS_FATAL, "Negative interrupt count.\n"); \
	} \
	mach_NumDisableIntrsPtr[0]++; \
    }
#define ENABLE_INTR() \
    if (!mach_AtInterruptLevel) { \
	mach_NumDisableIntrsPtr[0]--; \
	if (mach_NumDisableIntrsPtr[0] < 0) { \
	    Sys_Panic(SYS_FATAL, "Negative interrupt count.\n"); \
	} \
	if (mach_NumDisableIntrsPtr[0] == 0) { \
	    Mach_EnableIntr(); \
	} \
    }

/*
 * Macro to get processor number
 */
#define	Mach_GetProcessorNumber() 	0

extern	Boolean	mach_KernelMode;
extern	int	mach_NumProcessors;
extern	Boolean	mach_AtInterruptLevel;
extern	int	*mach_NumDisableIntrsPtr;

/*
 * Delay for N microseconds.
 */
#ifdef SUN3
#define	MACH_DELAY(n)	{ register int N = (n)<<1; N--; while (N > 0) {N--;} }
#else
#define	MACH_DELAY(n)	{ register int N = (n)>>1; N--; while (N > 0) {N--;} }
#endif

/*
 * The interrupt register on a sun3.
 */
#define	Mach_InterruptReg	((unsigned char *) DEV_INTERRUPT_REG_ADDR)

/*
 * Dispatch tables for kernel calls.
 */
extern ReturnStatus (*(mach_NormalHandlers[]))();
extern ReturnStatus (*(mach_MigratedHandlers[]))();

/*
 * The format for the vector offset register.
 */
typedef struct {
    unsigned	int	stackFormat:4;		/* Format of the stack */
    unsigned	int	   	   :2;		/* Filler */
    unsigned	int	vectorOffset:10;	/* Vector offset */
} Mach_VOR;
    
/*
 * The format for the special status word.
 */
typedef	struct {
    unsigned 	int 	rerun: 1,	 /* Rerun bus cycle (0 = processor
					    rerun, 1 = software rerun) */
    			fill1: 1,	 /* Reserved */
    			ifetch: 1,	 /* Instruction fetch to instruction
					    buffer (1 = true) */
    			dfetch: 1,	 /* Data fetch to the data input
					    buffer (1 = true) */
    			readModWrite: 1, /* Read-Modify-Write cycle */
    			highByte: 1,	 /* High byte transfer */
    			byteTrans: 1,	 /* Byte transfer flag */
    			readWrite: 1,	 /* Read/Write flag (0 = write, 
					    1 = read) */
    			fill2: 4,	 /* Reserved */
    			funcCode : 4;	 /* The function code */
} Mach_SpecStatWord;

/*
 * 68020 stack formats are defined in "MC68020 32-Bit Microprocessor User's 
 * Manual" on pages 6-19 to 6-24.  68010 stack formats are defined in 
 * "MC68000  16/32-Bit Microprocessor Programmers Reference Manual" on pages
 * 33 to 48.
 */

#ifdef SUN3
/*
 * Address and bus error info that is stored on the stack for a  68020 short
 * bus or address fault.
 */
typedef struct {
    unsigned	int	:16;
    Mach_SpecStatWord	specStatWord;	
    short		pipeStageC;
    short		pipeStageB;
    int			faultAddr;
    unsigned 	int	:16;
    unsigned 	int	:16;
    int			dataOutBuf;
    unsigned 	int	:16;
    unsigned 	int	:16;
} Mach_ShortAddrBusErr;

/*
 * Address and bus error info that is stored on the stack for a 68020 long
 * bus or address fault.
 */
typedef struct {
    unsigned	int	:16;
    Mach_SpecStatWord	specStatWord;	
    short		pipeStageC;
    short		pipeStageB;
    int			faultAddr;
    unsigned 	int	int1;
    int			dataOutBuf;
    unsigned	int	int2[2];
    int			stageBAddr;
    unsigned 	int	int3;
    int			dataInBuf;
    unsigned	int	int4[11];
} Mach_LongAddrBusErr;

/*
 * The default stack is the long one.  Anyone who actually plays with these
 * stacks will have to look at the exception type and figure things out
 * for themselves.
 */
typedef	Mach_LongAddrBusErr	Mach_AddrBusErr;

/*
 * The structure of the exception stack
 */
typedef struct {
    short		statusReg;		/* Status register */
    int			pc;			/* Program counter */
    Mach_VOR		vor;			/* The vector offset register */
    union {
	int		instrAddr;		/* Instruction that caused
						 * the fault. */
	Mach_AddrBusErr	addrBusErr;		/* Address or bus error info */
    } tail;
} Mach_ExcStack;

#else
/*
 * Address and bus error info that is stored on the stack.
 */
typedef struct {
    Mach_SpecStatWord	specStatWord;	
    int			faultAddr;
    unsigned 	int	:16;
    short		dataOutBuf;
    unsigned 	int	:16;
    short		dataInBuf;
    unsigned 	int	:16;
    short		instInBuf;
    short		internal[16];
} Mach_AddrBusErr;

/*
 * The structure of the exception stack
 */
typedef struct {
    short		statusReg;		/* Status register */
    int			pc;			/* Program counter */
    Mach_VOR		vor;			/* The vector offset register */
    union {
	Mach_AddrBusErr	addrBusErr;		/* Address or bus error info */
    } tail;
} Mach_ExcStack;

#endif

/*
 * Bus error register 
 */
#ifdef SUN3
typedef struct {
    unsigned	int	fill:24,	/* Filler to make it a long. */
			pageInvalid: 1,	/* The page accessed did not have the
					   valid bit set. */
			protError: 1,	/* Protection error. */
			timeOut: 1,	/* Timeout error. */
			vmeBusErr: 1,	/* VME bus error. */
			fpaBusErr: 1,	/* FPA bus error. */
			fpaEnErr: 1,	/* FPA Enable error. */
			res1:1,		/* Reserved. */
			watchdog:1;	/* Watchdog or user reset. */
} Mach_BusErrorReg;
#else
typedef struct {
    unsigned	int	fill:16,	/* Filler to make it a long. */
			res1:8,		/* Reserved */
			resident:1,	/* Valid bit set */
			busErr:1,	/* System bus error */
			res2:2,		/* Reserved */
			protErr:1,	/* Protection error */
			timeOut:1,	/* Timeout error */
			parErrU:1,	/* Parity error upper byte */
			parErrL:1;	/* Parity error lower byte */
} Mach_BusErrorReg;

#endif

/*
 * State for each process.
 *
 * IMPORTANT NOTE: If the order or size of fields in these structures change
 *		   then the constants which give the offsets must be
 *		   changed in "machConst.h".
 */

/*
 * The user state for a process.
 */
typedef struct {
    Address		userStackPtr;		/* The user stack pointer */
    int			trapRegs[MACH_NUM_GPRS];/* General purpose registers
						 * when trap into kernel. */
    Mach_ExcStack	*excStackPtr;		/* The exception stack */
} Mach_UserState;

/*
 * The kernel and user state for a process.
 */
typedef struct Mach_State {
    Mach_UserState	userState;		/* User state for a process. */
    int			switchRegs[MACH_NUM_GPRS];/* Where registers are saved
						 * and restored to/from during
						 * context switches. */
    Address		kernStackStart;		/* Address of the beginning of
						 * the kernel stack. */
    Mach_SetJumpState	*setJumpStatePtr;	/* Pointer to set jump state.*/
    int			sigExcStackSize;	/* Amount of valid data in the
						 * signal exception stack. */
    Mach_ExcStack	sigExcStack;		/* Place to store sig exception 
						 * stack on return from signal
						 * handler.*/
} Mach_State;

/*
 * The stack that is created when a trap occurs.   If you change the size
 * of this structure then you must also change the MACH_TRAP_INFO_SIZE constant
 * in machConst.h.
 */
typedef struct {
    int			trapType;	/* Type of trap. */
    Mach_BusErrorReg	busErrorReg;	/* Bus error register at time of trap.*/
    int			tmpRegs[4];	/* The tmp registers d0, d1, a0 and 
					 * a1.*/
    Mach_ExcStack	excStack;	/* The exception stack. */
} Mach_TrapStack;

/*
 * The stack that is created when an interrupt occurs.
 */
typedef struct {
    int			tmpRegs[4];	/* The tmp registers d0, d1, a0 and 
					 * a1.*/
    Mach_ExcStack	excStack;	/* The exception stack. */
} Mach_IntrStack;

/*
 * Macro to get processor number
 */
#define	Mach_GetProcessorNumber() 	0

extern	Boolean	mach_KernelMode;
extern	int	mach_NumProcessors;
extern	Boolean	mach_AtInterruptLevel;
extern	int	*mach_NumDisableIntrsPtr;
/*
 * Routine to initialize mach module.  Must be called first as part of boot 
 * sequence.
 */
extern void	Mach_Init();

/*
 * Routines to munge machine state struct.
 */
extern	void		Mach_InitFirstProc();
extern	ReturnStatus	Mach_SetupNewState();
extern	void		Mach_SetReturnVal();
extern	void		Mach_StartUserProc();
extern	void		Mach_ExecUserProc();
extern	void		Mach_FreeState();
extern	void		Mach_CopyState();
extern	void		Mach_GetDebugState();
extern	void		Mach_SetDebugState();

/*
 * Migration routines.
 */
extern void			Mach_EncapState();
extern ReturnStatus		Mach_DeencapState();
extern int			Mach_GetEncapSize();

/*
 * Other routines.
 */
extern void			Mach_InitSyscall();
extern void			Mach_SetHandler();
extern int			Mach_GetExcStackSize();
extern Mach_ProcessorStates	Mach_ProcessorState();
extern void			Mach_UnsetJump();

/*
 * Machine dependent routines.
 */
extern	void	Mach_GetEtherAddress();
extern	void	Mach_ContextSwitch();
extern	int	Mach_TestAndSet();
extern	int	Mach_GetMachineType();
extern	int	Mach_GetMachineArch();
extern	Address	Mach_GetStackPointer();

/*
 * spriteStart is defined in bootSys.s with an underscore.
 */
extern	int		spriteStart;
extern	int		endBss;
extern	int		endText;

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


#endif _MACH
