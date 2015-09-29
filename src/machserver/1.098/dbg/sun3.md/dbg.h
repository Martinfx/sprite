/*
 * dbg.h --
 *
 *     Exported types and procedure headers for the debugger module.
 *
 * Copyright (C) 1985 Regents of the University of California
 * All rights reserved.
 *
 *
 * $Header: /sprite/src/kernel/dbg/sun3.md/RCS/dbg.h,v 9.4 90/10/09 11:50:52 jhh Exp $ SPRITE (Berkeley)
 */

#ifndef _DBG
#define _DBG

#ifndef _SPRITE
#include <sprite.h>
#endif
#ifdef KERNEL
#include <user/netInet.h>
#include <netTypes.h>
#else
#include <netInet.h>
#include <kernel/netTypes.h>
#endif


/*
 * Variable to indicate that dbg wants a packet.
 */
extern	Boolean	dbg_UsingNetwork;

/*
 * Variable that indicates that we are under control of the debugger.
 */
extern	Boolean	dbg_BeingDebugged;

/*
 * The maximum stack address.
 */
extern	int	dbgMaxStackAddr;

/*
 * Debugger using syslog to dump output of call command or not.
 */
extern	Boolean	dbg_UsingSyslog;

/*
 * The different opcodes that kdbx can send us.
 */

typedef enum {
    DBG_READ_ALL_GPRS,		/* Read all 16 of the general purpose 
				   registers */
    DBG_WRITE_GPR,		/* Write one of the general purpose registers
				   d0-d7 or a0-a7 */
    DBG_CONTINUE, 		/* Continue execution */
    DBG_SINGLESTEP,		/* Single step execution */
    DBG_DETACH,			/* The debugger has finished with the kernel */
    DBG_INST_READ,		/* Read an instruction */
    DBG_INST_WRITE,		/* Write an instruction */
    DBG_DATA_READ,		/* Read data */
    DBG_DATA_WRITE,		/* Write data */
    DBG_SET_PID,		/* Set the process for which the stack 
				 * back trace is to be done. */
    DBG_GET_STOP_INFO,		/* Get all info needed by dbx after it stops. */
    DBG_GET_VERSION_STRING,	/* Return the version string. */
    DBG_DIVERT_SYSLOG,		/* Divert syslog output to the console. */
    DBG_REBOOT,			/* Call the reboot routine. */
    DBG_BEGIN_CALL,		/* Start a call. */
    DBG_END_CALL, 		/* Clean up after a call completes. */
    DBG_CALL_FUNCTION,		/* Call a function. */
    DBG_GET_DUMP_BOUNDS,	/* Get the bounds for the dump program. */
    DBG_UNKNOWN			/* Used for error checking */
} Dbg_Opcode;

typedef struct {
    int	regNum;
    int	regVal;
} Dbg_WriteGPR;

typedef struct {
    int		address;
    int		numBytes;
    char	buffer[100];
} Dbg_WriteMem;

typedef Dbg_WriteMem Dbg_CallFunc;

typedef struct {
    int		address;
    int		numBytes;
} Dbg_ReadMem;

typedef struct {
    int		stringLength;
    char	string[100];
} Dbg_Reboot;

typedef enum {
    DBG_SYSLOG_TO_ORIG,
    DBG_SYSLOG_TO_CONSOLE,
} Dbg_SyslogCmd;

typedef struct {
    unsigned int	pageSize;
    unsigned int	stackSize;
    unsigned int	kernelCodeStart;
    unsigned int	kernelCodeSize;
    unsigned int	kernelDataStart;
    unsigned int	kernelDataSize;
    unsigned int	kernelStacksStart;
    unsigned int	kernelStacksSize;
    unsigned int	fileCacheStart;
    unsigned int	fileCacheSize;
} Dbg_DumpBounds;

/*
 * Message format.
 */
typedef struct {
    short	opcode;
    union {
	int		pid;
	Dbg_WriteGPR	writeGPR;
	Dbg_WriteMem	writeMem;
	Dbg_CallFunc	callFunc;
	Dbg_ReadMem	readMem;
	int		pc;
	Dbg_SyslogCmd	syslogCmd;
	Dbg_Reboot	reboot;
    } data;
} Dbg_Msg;

#define	DBG_MAX_REPLY_SIZE	1400
#define	DBG_MAX_REQUEST_SIZE	1400

/*
 * The UDP port number that the kernel and kdbx use to identify a packet as
 * a debugging packet.  (composed from "uc": 0x75 = u, 0x63 = c)
 */

#define DBG_UDP_PORT 	0x7563

/*
 * The different statuses that we send kdbx after we stop.  There is one
 * status for each exception.  These numbers matter because kdbx uses them to
 * index an array.  Therefore don't change any of them without also changing
 * kdbx's array in ../kdbx/machine.c.
 *
 *     DBG_INTERRUPT		No error just an interrupt from the console.
 *     DBG_RESET		System reset.
 *     DBG_BUS_ERROR		Bus error.
 *     DBG_ADDRESS_ERROR	Address error.
 *     DBG_ILLEGAL_INST		Illegal instruction.
 *     DBG_ZERO_DIV		Division by zero.
 *     DBG_CHK_INST		A CHK instruction failed.
 *     DBG_TRAPV		Overflow trap.
 *     DBG_PRIV_VIOLATION	Privledge violation.
 *     DBG_TRACE_TRAP		Trace trap.
 *     DBG_EMU1010		Emulator 1010 trap.
 *     DBG_EMU1111		Emulator 1111 trap.
 *     DBG_STACK_FMT_ERROR	Stack format error.
 *     DBG_UNINIT_VECTOR	Unitiailized vector error.
 *     DBG_SPURIOUS_INT		Spurious interrupt.
 *     DBG_LEVEL1_INT		Level 1 interrupt.
 *     DBG_LEVEL2_INT		Level 2 interrupt.
 *     DBG_LEVEL3_INT		Level 3 interrupt.
 *     DBG_LEVEL4_INT		Level 4 interrupt.
 *     DBG_LEVEL5_INT		Level 5 interrupt.
 *     DBG_LEVEL6_INT		Level 6 interrupt.
 *     DBG_LEVEL7_INT		Level 7 interrupt.
 *     DBG_SYSCALL_TRAP		A system call trap.
 *     DBG_SIG_RET_TRAP		A return from signal trap.
 *     DBG_BAD_TRAP		Bad trap.
 *     DBG_BRKPT_TRAP		Breakpoint trap.
 *     DBG_UKNOWN_EXC		Unknown exception.
 */

#define DBG_INTERRUPT		0
#define	DBG_RESET		1
#define	DBG_BUS_ERROR		2
#define	DBG_ADDRESS_ERROR	3
#define	DBG_ILLEGAL_INST	4
#define	DBG_ZERO_DIV		5
#define	DBG_CHK_INST		6
#define	DBG_TRAPV		7
#define	DBG_PRIV_VIOLATION	8
#define	DBG_TRACE_TRAP		9
#define	DBG_EMU1010		10
#define	DBG_EMU1111		11
#define	DBG_STACK_FMT_ERROR	14
#define	DBG_UNINIT_VECTOR	15
#define	DBG_SPURIOUS_INT	24
#define	DBG_LEVEL1_INT		25
#define	DBG_LEVEL2_INT		26
#define	DBG_LEVEL3_INT		27
#define	DBG_LEVEL4_INT		28
#define	DBG_LEVEL5_INT		29
#define	DBG_LEVEL6_INT		30
#define	DBG_LEVEL7_INT		31
#define	DBG_SYSCALL_TRAP	33
#define	DBG_SIG_RET_TRAP	34
#define	DBG_BAD_TRAP		35
#define	DBG_BRKPT_TRAP		47

#define DBG_FP_UNORDERED_COND  48
#define DBG_FP_INEXACT_RESULT  49
#define DBG_FP_ZERO_DIV        50
#define DBG_FP_UNDERFLOW       51
#define DBG_FP_OPERAND_ERROR   52
#define DBG_FP_OVERFLOW        53
#define DBG_FP_NAN             54
#define	DBG_UNKNOWN_EXC	       55

/*
 * Variable that is set to true when we are called through the DBG_CALL macro.
 */
extern	Boolean	dbgPanic;

/* 
 * Macro to call the debugger from kernel code.
 */
#define DBG_CALL	dbgPanic = TRUE; asm("trap #15");

/*
 * Number of bytes between acknowledgements when the the kernel is writing
 * to kdbx.
 */
#define DBG_ACK_SIZE	256

/*
 * Info returned when GETSTOPINFO command is submitted.
 */
typedef struct {
    int			codeStart;
    int			maxStackAddr;
    int			termReason;
    int			trapCode;
    unsigned	int	statusReg;
    int			genRegs[16];
    int			pc;
} StopInfo;

extern	void	Dbg_Init _ARGS_((void));
extern	void	Dbg_InputPacket _ARGS_((Net_Interface *interPtr,
				    Address packetPtr, int packetLength));
extern	Boolean	Dbg_InRange _ARGS_((unsigned int addr, int numBytes,
				    Boolean writeable));
extern Boolean
    Dbg_ValidatePacket _ARGS_((int size, Net_IPHeader *ipPtr, int *lenPtr,
			       Address *dataPtrPtr,
			       Net_InetAddress *destIPAddrPtr,
			       Net_InetAddress *srcIPAddrPtr,
			       unsigned int *srcPortPtr));
extern void
    Dbg_FormatPacket _ARGS_((Net_InetAddress srcIPAddress,
			     Net_InetAddress destIPAddress,
			     unsigned int destPort, int dataSize,
			     Address dataPtr));
extern int	Dbg_PacketHdrSize _ARGS_((void));

#endif /*_DBG */
