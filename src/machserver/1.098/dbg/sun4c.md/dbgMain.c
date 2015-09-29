/* dbgMain.c -
 *
 *     This contains the routines which read and execute commands from kdbx.
 *
 *
 * Copyright 1989 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/kernel/dbg/sun4.md/RCS/dbgMain.c,v 9.12 91/06/28 12:09:22 mgbaker Exp $ SPRITE (Berkeley)";
#endif not lint

#include <sprite.h>
#include <dbg.h>
#include <dbgInt.h>
#include <main.h>
#include <mach.h>
#include <proc.h>
#ifndef FIRST_RUN
#include <vm.h>
#include <vmInt.h>
#include <vmMach.h>
#include <vmMachInt.h>
#endif
#include <machMon.h>
#include <net.h>
#include <netEther.h>
#include <netInet.h>
#include <dev.h>
#include <devVid.h>
#include <stdio.h>
#include <bstring.h>
#include <string.h>

Boolean dbg_InDebugger = FALSE;			/* TRUE if we are currently in
						 * the debug command loop. */
Boolean	dbg_BeingDebugged = FALSE;		/* TRUE if are under control
						 * of kdbx or kgdb.*/
Boolean	dbg_UsingNetwork = FALSE;		/* TRUE if the debugger is
						 * using the network interface*/
char	requestBuffer[DBG_MAX_REQUEST_SIZE];	/* Buffer to receive request
						 * into. */
int	requestOffset;				/* Offset in buffer where next
						 * bytes should be read from.*/
char	replyBuffer[DBG_MAX_REPLY_SIZE+2];	/* Buffer to hold reply. */
int	replyOffset = 0;			/* Offset in buffer where next
						 * bytes in reply should go. */
int	curMsgNum;				/* The current message that
						 * is being processed. */
int	dbgMonPC;				/* Place to get the PC from
						 * if trap via the monitor.*/
int	dbgTraceLevel;				/* The debugger tracing
						 * level. */
Boolean	dbg_Rs232Debug = FALSE;			/* TRUE if we are using the
						 * RS@#@ line to debug.  FALSE
						 * if we are using the network.
						 * On the sun4, we have only
						 * used the network.
						 */
Boolean	dbg_SyncedDisks = FALSE;		/* For determining in the
						 * debugger whether the disks
						 * got sync'd or not. */

/*
 * Number of times to poll before timing out and resending (about 2 seconds).
 */
#ifdef sun3
int	dbgTimeout = 50000;
#endif
#ifdef sun2
int	dbgTimeout = 15000;
#endif
#ifdef sun4
int	dbgTimeout = 150000;
#endif

/*
 * Information about the latest packet received.
 */
Boolean			gotPacket;	
int			dbgPacketLength;
Net_InetAddress		dbgMyIPAddr;
Net_InetAddress		dbgSrcIPAddr;
Net_InetAddress		dbgSrcPort;
Net_EtherHdr		dbgEtherHdr;
Net_ScatterGather	dbgGather;
Net_Interface		*dbgInterPtr = (Net_Interface *) NIL;

/*
 * Size of debugging packet header and data.
 */
#define	PACKET_HDR_SIZE (sizeof(Net_EtherHdr) + Dbg_PacketHdrSize() + 4 + 2)
#define PACKET_DATA_SIZE (DBG_MAX_REPLY_SIZE - PACKET_HDR_SIZE)

/*
 * Strings which describe each of the opcodes that kdbx can send us.
 */
static char *opcodeNames[] = DBG_OPCODE_NAMES ;

/*
 * Strings which describe the different exceptions that can occur.
*/
static char *exceptionNames[] = DBG_EXECPTION_NAMES ;

/*
 * The type of machine that we are on.
 */
int		machineType;

/*
 * Whether syslog should remain diverted on continue or not.
 */
static Boolean	syslogDiverted = FALSE;

/*
 * Declare global variables.
 */
int		dbgSfcReg;
int		dbgDfcReg;
int 		dbgUserContext;
int 		dbgKernelContext;
int		dbgTermReason;
int		dbgInDebugger;
int		dbgIntPending;
int		dbgExcType;
Boolean		dbgPanic;
int		dbgSavedSP;
int		dbgMaxStackAddr;
Boolean		dbg_UsingSyslog = FALSE;
Boolean		dbgCanUseSyslog = TRUE;
static	   int	oldContext;


/* 
 * Forward declarations:
 */
static void DbgCheckNmis _ARGS_((void));
static char *	TranslateOpcode _ARGS_((Dbg_Opcode opcode));
static char *	TranslateException _ARGS_((int exception));
static Boolean	ReadRequest _ARGS_((Boolean timeout));
static void	SendReply _ARGS_((void));
static void	GetRequestBytes _ARGS_((int numBytes, Address dest));
static void	PutReplyBytes _ARGS_((int numBytes, Address src));


/*
 * ----------------------------------------------------------------------------
 *
 * DbgCheckNmis --
 *
 *	Turn Non-maskable-interrupts on and off to allow keyboard events to
 *	take place.  Only needed on Sun-2's because of the funny mapping
 *	between kernel and user address spaces - they are turned on in the
 *	main debugging loop on Sun-3's.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
static void
DbgCheckNmis()
{
#ifdef sun2
    int	oldContext;
    oldContext = VmMachGetKernelContext(); 
    VmMachSetKernelContext(VMMACH_KERN_CONTEXT);
    Mach_MonStartNmi();
    Mach_MonStopNmi();
    VmMachSetKernelContext(oldContext);
#endif
}


/*
 * ----------------------------------------------------------------------------
 *
 * Dbg_InRange --
 *
 *     Return true if the given address is a valid kernel address and false
 *     otherwise.
 *
 * Results:
 *     True if the given address is a valid kernel address and false
 *     otherwise.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
Boolean
Dbg_InRange(addr, numBytes, writeable) 
    unsigned 	int addr; 	/* Beginning address to check. */
    int		numBytes; 	/* Number of bytes to check. */
    Boolean	writeable;	/* TRUE => address must be writeable. */
{
#ifndef FIRST_RUN
    VmMachPTE		pte;
#endif
    int			i;
    unsigned	int	prot;
    int			firstPage;
    int			lastPage;
    unsigned	int	maxAddr;

#ifdef sun2
    maxAddr = 0x1000000;
#else
    maxAddr = 0x10000000;
#endif
    if (dbgTraceLevel >= 5) {
	printf("Dbg_InRange called with addr 0x%x %d bytes, and writable = %d\n",
	    addr, numBytes, (unsigned int) writeable);
    }
    /*
     * Don't look at anything in device space. 
     */
    if (!(((addr + numBytes - 1) < VMMACH_DEV_START_ADDR) ||
	 (addr >= VMMACH_DMA_START_ADDR))) {
	return FALSE;
    }
    if ((int) (addr) & 0x1) {
	printf("Dbg: odd address: %x\n", addr);
	return(FALSE);
    }
#ifndef FIRST_RUN
    firstPage = ((unsigned int) addr) >> VMMACH_PAGE_SHIFT;
    lastPage = (((unsigned int) addr) + numBytes - 1) >> VMMACH_PAGE_SHIFT;
    for (i = firstPage; i <= lastPage; i++) {
	pte = VmMachGetPageMap((Address)(i << VMMACH_PAGE_SHIFT));
	if (dbgTraceLevel >= 5) {
	    printf("pte value was 0x%x\n", pte);
	}
	prot = pte & VMMACH_PROTECTION_FIELD;
	if (!(pte & VMMACH_RESIDENT_BIT)) {
	    return(FALSE);
	} else if (writeable) {
	    if (prot != VMMACH_KRW_PROT && prot != VMMACH_URW_PROT) {
		return(FALSE);
	    }
	} else {
	    if (prot != VMMACH_KRW_PROT && prot != VMMACH_URW_PROT &&
		prot != VMMACH_KR_PROT && prot != VMMACH_UR_PROT) {
		return(FALSE);
	    }
	}
    }
#endif

    return(TRUE);
}


/*
 * ----------------------------------------------------------------------------
 *
 * TranslateOpcode --
 *
 *     Return the string which describes the given opcode.
 *
 * Results:
 *     The string which describes the given opcode.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
char *
TranslateOpcode(opcode)
    Dbg_Opcode opcode;		/* The opcode which is to be translated. */
{
	int index;

	index = (int) opcode;
	if (index < 0 || index > (int) DBG_UNKNOWN) {
	    index = (int) DBG_UNKNOWN;
	}

	return(opcodeNames[index]);
}


/*
 * ----------------------------------------------------------------------------
 *
 * TranslateException --
 *
 *     Return the string that describes the given exception.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
char *
TranslateException(exception)
    int exception;		/* The exception which is to be translated. */
{

	if (exception < 0 || exception > DBG_UNKNOWN_EXCEPT) {
	    exception = DBG_UNKNOWN_EXCEPT;
	}

	return(exceptionNames[exception]);
}




/*
 * ----------------------------------------------------------------------------
 *
 * Dbg_Init --
 *
 *     Initialize the debugger.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     dbgMonPC and dbgDoTrace are initialized.
 *
 * ----------------------------------------------------------------------------
 */
void
Dbg_Init()
{
    dbgMonPC = 0;
    dbgTraceLevel = 0;
    dbgInDebugger = 0;
    dbgIntPending = 0;
    dbgPanic = FALSE;
    dbg_BeingDebugged = FALSE;
    machineType = 0;
    Mach_MonPrintf("Machine type %d\n", machineType);
}


/*
 * ----------------------------------------------------------------------------
 *
 * Dbg_InputPacket --
 *
 *     See if the current packet is for us.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     gotPacket is set to true if we got a packet that we liked.
 *
 * ----------------------------------------------------------------------------
 */
void
Dbg_InputPacket(interPtr, packetPtr, packetLength)
    Net_Interface	*interPtr;
    Address		packetPtr;
    int			packetLength;
{
    Address	dataPtr;
    int		dataLength;
    Net_EtherHdr	*etherHdrPtr;

    if (interPtr->netType != NET_NETWORK_ETHER) {
	printf("Got a debugger packet on non-ethernet interface %s\n",
	    interPtr->name);
	return;
    }
    etherHdrPtr = (Net_EtherHdr *)packetPtr;
    if (etherHdrPtr->type != NET_ETHER_IP) {
	if (dbgTraceLevel >= 5) {
	    printf("Non-IP (Type=0x%x) ", (int)etherHdrPtr->type);
	}
	return;
    }
    if (gotPacket) {
	return;
    }
    if (dbgTraceLevel >= 4) {
	printf("Validating packet\n");
    }
    { 
	static char alignedBuffer[NET_ETHER_MAX_BYTES];
        /*
	 * Make sure the packet starts on a 32-bit boundry so that we can
	 * use structures for describe the data.
	 */
	if ( (unsigned int) (packetPtr + sizeof(Net_EtherHdr)) & 0x3 ) {
	      bcopy (packetPtr + sizeof(Net_EtherHdr), alignedBuffer,
			    packetLength - sizeof(Net_EtherHdr));
	      packetPtr = alignedBuffer;
	} else {
	      packetPtr = packetPtr + sizeof(Net_EtherHdr);
	}

	if (Dbg_ValidatePacket(packetLength - sizeof(Net_EtherHdr),
			       (Net_IPHeader *)(packetPtr),
			       &dataLength, &dataPtr,
			       &dbgMyIPAddr, &dbgSrcIPAddr, &dbgSrcPort)) {
	    if (dbgTraceLevel >= 4) {
		printf("Got a packet: length=%d\n", dataLength);
	    }
	    bcopy((Address)etherHdrPtr, (Address)&dbgEtherHdr,
		    sizeof(Net_EtherHdr));
	    gotPacket = TRUE;
	    bcopy(dataPtr, requestBuffer, dataLength);
	    /*
	     * Set the interface we are using. 
	     */
	    dbgInterPtr = interPtr;
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * ReadRequest --
 *
 *     Read the next request from kdbx.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     TRUE if didn't time out.
 *
 * ----------------------------------------------------------------------------
 */
static Boolean
ReadRequest(timeout)
    Boolean	timeout;	/* TRUE if should timeout after waiting a 
				 * while. */
{
	int	timeOutCounter;
	Net_Interface	*interPtr;
	int		i;

	gotPacket = FALSE;
	timeOutCounter = dbgTimeout;
	do {
	    DbgCheckNmis();
	    /*
	     * Listen on all the interfaces. The debugger is relatively
	     * stateless so its easiest to just listen on them all.
	     */
	    for (i = 0; ; i++) {
		interPtr = Net_NextInterface(TRUE, &i);
		if (interPtr == (Net_Interface *) NIL) {
		    break;
		}
		Net_RecvPoll(interPtr);
		if (gotPacket) {
		    break;
		}
	    }
	    if (timeout) {
		timeOutCounter--;
	    }
	} while(!gotPacket && timeOutCounter != 0);
	if (gotPacket) {
	    replyOffset = PACKET_HDR_SIZE;
	    requestOffset = 4;
	    curMsgNum = *(int *)(requestBuffer);
	    if (dbgTraceLevel >= 4) {
		printf("MsgNum = %d\n", curMsgNum);
	    }
	}

	return(gotPacket);
}


/*
 * ----------------------------------------------------------------------------
 *
 * GetRequestBytes --
 *
 *     Get the next numBytes bytes from the current request.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
static void
GetRequestBytes(numBytes, dest)
    int		numBytes;
    Address	dest;
{
	bcopy(requestBuffer + requestOffset, dest, numBytes);
	requestOffset += numBytes;
}


/*
 * ----------------------------------------------------------------------------
 *
 * PutReplyBytes --
 *
 *     Put the given bytes into the reply buffer.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
static void
PutReplyBytes(numBytes, src)
    int		numBytes;
    Address	src;
{
    if (replyOffset + numBytes > DBG_MAX_REPLY_SIZE) {
	printf("PutReplyBytes: Buffer overflow\n");
	numBytes = DBG_MAX_REPLY_SIZE - replyOffset;
    }
    bcopy(src, &replyBuffer[replyOffset], numBytes);
    replyOffset += numBytes;
}


/*
 * ----------------------------------------------------------------------------
 *
 * SendReply --
 *
 *     Send a reply to kdbx.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
static void
SendReply()
{
    {
	Net_EtherHdr		*etherHdrPtr;

	if (dbgTraceLevel >= 4) {
	    printf("Sending reply\n");
	}
	etherHdrPtr = (Net_EtherHdr *) (replyBuffer+2);
	etherHdrPtr->source = dbgEtherHdr.destination;
	etherHdrPtr->destination = dbgEtherHdr.source;
	etherHdrPtr->type = dbgEtherHdr.type;
	dbgGather.bufAddr = replyBuffer + sizeof(Net_EtherHdr)+2;
	dbgGather.length = replyOffset - sizeof(Net_EtherHdr)-2;
	dbgGather.mutexPtr = (Sync_Semaphore *) NIL;
	bcopy((char *)&curMsgNum,(char *)(replyBuffer + PACKET_HDR_SIZE - 4),4);
	Dbg_FormatPacket(dbgMyIPAddr, dbgSrcIPAddr, dbgSrcPort,
		     replyOffset - sizeof(Net_EtherHdr) - Dbg_PacketHdrSize()-2,
		     replyBuffer + sizeof(Net_EtherHdr) + 2);
	Net_RawOutput(dbgInterPtr, (Address) etherHdrPtr, &dbgGather, 1);
	if (dbgTraceLevel >= 4) {
	    printf("Sent reply\n");
	}
    }
}



/*
 * ----------------------------------------------------------------------------
 *
 * Dbg_Main --
 *
 *     The main debugger loop.  This will read commands from the network
 *     and call the proper routine to execute them.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
/*ARGSUSED*/
void
Dbg_Main(trapType, trapStatePtr)
    int			trapType;	/* The reason we were called. */
    Mach_RegState	*trapStatePtr;	/* The CPU's state at the trap. */
{
    Boolean	  	done;		/* Boolean to tell us whether to leave
					 * the main debugger loop */
    Dbg_Opcode	  	opcode;	        /* The operation that was requested */
					/* Process table entry that we switched
					 * stacks to. */
    Proc_ControlBlock	*procPtr = (Proc_ControlBlock *) NIL;
    Boolean		atInterruptLevel;/* TRUE if we were entered from an
					  * interrupt handler. */
    static int 		curContext;

#ifdef sun3
    /*
     * Turn on non-maskable interrupts.
     */
    Mach_MonStartNmi();
#endif
#ifndef FIRST_RUN
    /*
     * Switch to kernel context so that we can access the monitor.
     */
    curContext = oldContext = VmMachGetKernelContext();
    VmMachSetKernelContext(VMMACH_KERN_CONTEXT);
#endif
#ifdef NOTDEF
/*
 * This is the code the other machine types execute to sync the disks
 * when going into the debugger.  But it seems to cause us to pop out of the
 * debugger, since it context switches to the backend write process.
 * Why does it work (does it?) on the other machines?
 */
    if (!dbg_BeingDebugged) {
	/*
	 * Try to sync the disks if we aren't at interrupt level.  If we
	 * are don't bother because we'll just hang waiting for interrupts.
	 * Of course I could force interrupts to be enabled but I'm not sure
	 * if that's a great idea.
	 */
	if (mach_NumDisableIntrsPtr[0] == 0 && !mach_AtInterruptLevel) {
	    Mach_EnableIntr();
	    Sys_SyncDisks(MACH_CALL_DBG_TRAP);
	    dbg_SyncedDisks = TRUE;
	    Mach_DisableIntr();
	}
    }
#endif NOTDEF

    dbg_InDebugger = TRUE;
    /*
     * Put us at interrupt level so that printf won't accidently enable
     * interrupts.
     */
    atInterruptLevel = mach_AtInterruptLevel;
    mach_AtInterruptLevel = TRUE;

    /*
     * Force system log output to the console.
     */
    if (!syslogDiverted) {
	Dev_SyslogDebug(TRUE);
    }

    if (dbgPanic && DBG_CVT_MACH_TRAP(trapType) == DBG_BREAKPOINT_TRAP ) { 
	dbgPanic = FALSE;
	trapType = DBG_INTERRUPT;
	/*
	 * Set the pc to the instruction after the trap.
	 */
	trapStatePtr->pc = trapStatePtr->nextPc;
	trapStatePtr->nextPc = trapStatePtr->nextPc+4;
    } 
    /*
     * We want to inform the user what caused the problem.  However we only
     * tell him if: 1) we are debugging the debugger; 2) we are not under 
     * debugger control (i.e. we don't want to inform the user on every trace 
     * trap), 3) we got something besides a trace trap or a breakpoint trap
     * exception.
     */
    if (dbgTraceLevel >= 1 || !dbg_BeingDebugged || 
        (DBG_CVT_MACH_TRAP(trapType) != DBG_BREAKPOINT_TRAP)) { 
	Dev_VidEnable(TRUE);	/* unblank the screen */
	printf("Entering debugger with a %s (%d) exception at PC 0x%x\r\n",
		   TranslateException(DBG_CVT_MACH_TRAP(trapType)),trapType,
		   (unsigned) trapStatePtr->pc);
    }


    /*
     * Inform the network module that we're in charge now.
     */
    dbg_UsingNetwork = TRUE;
    /*
     * If we are stopped after a continue or single step must write a
     * null byte to the debugger at the other end.  It knows that if it sees
     * a null byte we are stopped at this end.
     */
    if (dbg_BeingDebugged) {
        unsigned        char    ch;
	int	timeout = 5;
        ch = 0;
        PutReplyBytes(1, (Address)&ch);
        SendReply();
        do {
            if (ReadRequest(TRUE)) {
                GetRequestBytes(4, (Address)&opcode);
		if (opcode != DBG_CONTINUE) {
		    break;
		} else {
		    PutReplyBytes(4, (Address) &opcode);
		    SendReply();
		    continue;
		}
            }
            /*
             * We can only timeout if we are using network debugging.
             */
	    Net_RawOutput(dbgInterPtr, (Address) replyBuffer, 
		    &dbgGather, 1);
            if (dbgTraceLevel >= 5) {
                printf("DBG: Timeout\n");
            }
            printf("TI ");
        } while (timeout-- > 0);
    } else {
         short	t_opcode;
	 /*
	  * The kmsg program still sends short opcodes. If the top 16 bits
	  * are zero we assume that it is a long opcode.
	  */
        (void) ReadRequest(FALSE);
        GetRequestBytes(2, (Address)&t_opcode);
	if (t_opcode == 0) {
	    GetRequestBytes(2, (Address)&t_opcode);
	}
	opcode = (Dbg_Opcode) t_opcode;

    }

    /*
     * Now read commands until kdbx tells us that we can return.
     */
    done = FALSE;
    while (!done) {
	if (dbgTraceLevel >= 2) {
	    printf("Request: %s ", TranslateOpcode(opcode));
	}

	/*
	 * Process the request 
	 */
	switch (opcode) {

	    /*
	     * The client wants to read some data from us ...
	     */

	    case DBG_GET_STOP_INFO: {
		StopInfo	stopInfo;
		stopInfo.codeStart = (int)mach_CodeStart;
		if (procPtr != (Proc_ControlBlock *) NIL &&
		    procPtr->machStatePtr != (Mach_State *)NIL) {
		    stopInfo.regs = *(procPtr->machStatePtr->switchRegs);
		    stopInfo.regs.tbr = trapStatePtr->tbr;
		    stopInfo.regs.y = trapStatePtr->y;
		    stopInfo.regs.pc = ((int) &Mach_ContextSwitch)+16;
		    stopInfo.regs.nextPc = stopInfo.regs.pc+4;

		} else {
		    stopInfo.regs = *trapStatePtr;
		}
		stopInfo.trapType = trapType;
		PutReplyBytes(sizeof(stopInfo), (Address)&stopInfo);
		SendReply();
		break;
	    }
	    case DBG_READ_ALL_REGS: 

		if (procPtr != (Proc_ControlBlock *) NIL &&
		    procPtr->machStatePtr != (Mach_State *)NIL) {
		    Mach_RegState	regState;
		    regState = *(procPtr->machStatePtr->switchRegs);
		    regState.tbr = trapStatePtr->tbr;
		    regState.y = trapStatePtr->y;
		    regState.pc = ((int) &Mach_ContextSwitch)+16;
		    regState.nextPc = regState.pc+4;
		    PutReplyBytes(sizeof(regState), (Address) &regState);
		} else {
		    PutReplyBytes(sizeof(*trapStatePtr),
			         (Address) trapStatePtr);
		}
		SendReply();
		break;

	    case DBG_GET_DUMP_BOUNDS: {
		Dbg_DumpBounds bounds;
		extern unsigned int end;
		bounds.pageSize = vm_PageSize;
		bounds.stackSize = mach_KernStackSize;
		bounds.kernelCodeStart = (unsigned int) mach_KernStart;
		bounds.kernelCodeSize  = 
			(unsigned int) (((Address)(&end)) - mach_KernStart);
		bounds.kernelDataStart	= ((unsigned int)(&end));
		bounds.kernelDataSize	= (unsigned int) 
				(vmMemEnd - ((Address)(&end)));
		bounds.kernelStacksStart = (unsigned int)vmStackBaseAddr;
		bounds.kernelStacksSize = (unsigned int) 
				(vmStackEndAddr - vmStackBaseAddr);
		bounds.fileCacheStart	= (unsigned int)vmBlockCacheBaseAddr;
		bounds.fileCacheSize	= (unsigned int) (vmBlockCacheEndAddr - 
						vmBlockCacheBaseAddr);

		PutReplyBytes(sizeof(bounds), (char *)&bounds);
		SendReply();
		break;
	    }

	    case DBG_GET_VERSION_STRING: {
		char	*version;

		version = SpriteVersion();
		PutReplyBytes(strlen(version) + 1, version);
		SendReply();
		break;
	    }
	    
	    case DBG_INST_READ:
	    case DBG_DATA_READ: {
		Dbg_ReadMem	readMem;
		int		status;

		GetRequestBytes(sizeof(readMem), (Address) &readMem); 
		if (dbgTraceLevel >= 2) {
		    printf("Addr=%x Numbytes=%d ",
				readMem.address, readMem.numBytes);
		}
#ifndef FIRST_RUN
		VmMachSetKernelContext(curContext);
#endif
		if (Dbg_InRange((unsigned int) readMem.address, readMem.numBytes,
			    FALSE)) {
		    status = 1;
		    PutReplyBytes(sizeof(status), (Address)&status);
		    PutReplyBytes(readMem.numBytes, (Address)readMem.address);
		} else {
		    if (dbgTraceLevel >= 2) {
			printf("FAILURE ");
		    }
		    status = 0;
		    PutReplyBytes(sizeof(status), (Address)&status);
		}
#ifndef FIRST_RUN
		VmMachSetKernelContext(VMMACH_KERN_CONTEXT);
#endif
		SendReply();
		break;
	    }

	    /*
	     * The client wants to write something to us.
	     */
	    case DBG_SET_PID: {
		Proc_PID	pid;

		GetRequestBytes(sizeof(pid), (Address) &pid);
		 {
		    int	dummy;

		    PutReplyBytes(4, (Address) &dummy);
		    SendReply();
		}
		if (dbgTraceLevel >= 2) {
		    printf("pid %x ", pid);
		}
		if (pid == 0) {
		    procPtr = (Proc_ControlBlock *) NIL;
		    curContext = oldContext;
		} else {
		    procPtr = Proc_GetPCB(pid);
		    if (procPtr == (Proc_ControlBlock *) NIL ||
		        procPtr == (Proc_ControlBlock *) 0 ||
			procPtr->state == PROC_UNUSED ||
		        procPtr->state == PROC_DEAD ||
			procPtr->state == PROC_NEW) {
			printf("Can't backtrace stack for process %x\n",
					pid);
			procPtr = (Proc_ControlBlock *) NIL;
		    } else {
			curContext = VmMach_GetContext(procPtr);
			if (curContext == -1) {
			    printf("No user context loaded for pid 0x%x\n", 
				   pid);
			    curContext = VMMACH_KERN_CONTEXT;
			}
		    }
		}
		break;
	    }
	    case DBG_REBOOT: {
		int	stringLength;
		char	rebootString[100];
		/*
		 * For a reboot command first read the size of the string and
		 * then the string itself.
		 */
		GetRequestBytes(sizeof(int), (Address)&stringLength);
		if (stringLength != 0) {
		    GetRequestBytes(stringLength, (Address)rebootString);
		}
		rebootString[stringLength] = '\0';
		 {
		    int	dummy;

		    PutReplyBytes(4, (Address) &dummy);
		    SendReply();
		}
		Mach_MonReboot(rebootString);
	    }
	    case DBG_INST_WRITE:
	    case DBG_DATA_WRITE: {
		Dbg_WriteMem		writeMem;
		unsigned	char	ch;
		/*
		 * For an instruction or a data write we first have to find out 
		 * which address to write to and how many bytes to write.  Next
		 * we have to make sure that the address is valid.  If it is
		 * then we read the data and write it to the given address.  If
		 * not we just report an error to kdbx.
		 */
		GetRequestBytes(2 * sizeof(int), (Address) &writeMem);
		if (dbgTraceLevel >= 2) {
		    printf("Addr=%x Numbytes=%d ",
				writeMem.address, writeMem.numBytes);
		}

#ifndef FIRST_RUN
		VmMachSetKernelContext(curContext);
#endif	
		if (Dbg_InRange((unsigned int) writeMem.address,
			    writeMem.numBytes, opcode == DBG_DATA_WRITE)) {
#ifndef FIRST_RUN
		    if (opcode == DBG_INST_WRITE) {
			VmMach_SetProtForDbg(TRUE, writeMem.numBytes, 
					     (Address)writeMem.address);
		    }
#endif
		    GetRequestBytes(writeMem.numBytes,
				    (Address) writeMem.address);
#ifndef FIRST_RUN
		    if (opcode == DBG_INST_WRITE) {
			VmMach_SetProtForDbg(FALSE, writeMem.numBytes, 
					     (Address)writeMem.address);
		    }
#endif
		    ch = 1;
		} else {
		    char	buf[100];

		    if (dbgTraceLevel >= 2) {
			printf("FAILURE ");
		    }
		    GetRequestBytes(writeMem.numBytes, buf);
		    ch = 0;
		}
#ifndef FIRST_RUN
		VmMachSetKernelContext(VMMACH_KERN_CONTEXT);
#endif

		PutReplyBytes(1, (char *) &ch);
		SendReply();

		break;
	    }

	    case DBG_WRITE_REG: {                
		Dbg_WriteReg	writeReg;

		/*
		 * First find out which register is being written and
		 * then read the value.
		 */
		GetRequestBytes(sizeof(writeReg), (Address)&writeReg);
		 {
		    int	dummy;

		    PutReplyBytes(4, (Address) &dummy);
		    SendReply();
		}
		if (dbgTraceLevel >= 2) {
		    printf("register %d data %x ", writeReg.regNum, 
				writeReg.regVal);
		}
		((int *) trapStatePtr)[(writeReg.regNum)] = writeReg.regVal;
		break;
	    }

	    case DBG_DIVERT_SYSLOG: 
		GetRequestBytes(sizeof(Boolean), (Address)&syslogDiverted);
		 {
		    int	dummy;

		    PutReplyBytes(4, (Address) &dummy);
		    SendReply();
		}
		break;

	    case DBG_BEGIN_CALL: {
		/*
		 * We are beginning a call command.  Fix up the stack
		 * so that we will be able to continue.  We will put
		 * it back when we are done.
		 */
		int	dummy;
		if (dbgCanUseSyslog) {
		    dbg_UsingSyslog = TRUE;
		}

		PutReplyBytes(4, (Address) &dummy);
		SendReply();

		break;
		}
	    case DBG_END_CALL: {
		char	*buffer;
		int	*firstIndexPtr;
		int	*lastIndexPtr;
		int	bufSize;
		int	length;
		/*
		 * Dump the syslog buffer.
		 */
		Dev_SyslogReturnBuffer(&buffer, &firstIndexPtr,
				       &lastIndexPtr, &bufSize);
/* #ifdef GOOD_SYSLOG */
		if (*firstIndexPtr == -1) {
		    length = 0;
		    PutReplyBytes(4, (Address) &length);
		    dbg_UsingSyslog = FALSE;
		} else if (*firstIndexPtr <= *lastIndexPtr) {
		    length = *lastIndexPtr - *firstIndexPtr + 1;
		    if (length + 4 > PACKET_DATA_SIZE) {
			length = PACKET_DATA_SIZE - 4;
		    }
		    PutReplyBytes(4, (Address) &length);
		    PutReplyBytes(length,
				  (Address)&buffer[*firstIndexPtr]);
		    *firstIndexPtr += length;
		    if (*firstIndexPtr > *lastIndexPtr) {
			*firstIndexPtr = *lastIndexPtr = -1;
		    }
		} else {
		    length = bufSize - *firstIndexPtr;
		    if (length + 4 > PACKET_DATA_SIZE) {
			length = PACKET_DATA_SIZE - 4;
		    }
		    PutReplyBytes(4, (Address) &length);
		    PutReplyBytes(length,
				  (Address)buffer[*firstIndexPtr]);
		    *firstIndexPtr += length;
		    if (*firstIndexPtr == bufSize) {
			*firstIndexPtr = 0;
		    }
		}
#ifdef notdef
		length = 0;
		PutReplyBytes(4, (Address) &length);
		dbg_UsingSyslog = FALSE;
#endif
		SendReply();
		break;
	    }
	    case DBG_CALL_FUNCTION: {
		Dbg_CallFunc		callFunc;
		int			returnVal;
		static int		argBuf[128];
		GetRequestBytes(2 * sizeof(int), (Address) &callFunc);
		if (dbgTraceLevel >= 2) {
		    printf("Addr=%x Numbytes=%d ",
				callFunc.address, callFunc.numBytes);
		}
		VmMachSetKernelContext(curContext);
		if ((callFunc.numBytes >= 0 && callFunc.numBytes < 128) &&
		     Dbg_InRange((unsigned int) callFunc.address,4,FALSE)) {
		    GetRequestBytes(callFunc.numBytes,(Address) argBuf);
		    returnVal = (* ((int (*)()) callFunc.address))(argBuf[0],
		    argBuf[1],argBuf[2],argBuf[3],argBuf[4],argBuf[5],argBuf[6],
		    argBuf[7],argBuf[8],argBuf[9]);
		} else {

		    if (dbgTraceLevel >= 2) {
			printf("FAILURE ");
		    }
		    GetRequestBytes(callFunc.numBytes,(Address)argBuf);
		    returnVal = -1;
		}
		VmMachSetKernelContext(VMMACH_KERN_CONTEXT);
		PutReplyBytes(4, (char *) &returnVal);
		SendReply();

		break;
	    }
	    case DBG_CONTINUE: {
		/*
		 * The client wants to continue execution.
		 */
		int	foo;
		GetRequestBytes(sizeof(int), 
			    (Address) &foo);
		if (dbgTraceLevel >= 2) {
		    printf("Continuing from pc %x ",
				trapStatePtr->pc);
		}
		{
		    int	dummy;

		    PutReplyBytes(4, (Address) &dummy);
		    SendReply();
		}

		dbg_BeingDebugged = TRUE;
		done = TRUE;
		break;
	    }
	    case DBG_SINGLESTEP: {
		/*
		 * The client wants to single step.
		 */
		int	dummy;
		GetRequestBytes(sizeof(int),  (Address) &dummy);
		printf("Sun4's can't single step\n");

	        PutReplyBytes(4, (Address) &dummy);
		SendReply();

		break;
	    }
	    case DBG_DETACH:
		/*
		 * The debugger has terminated and wants to let us go about our
		 * business.
		 */
		if (dbgTraceLevel >= 2) {
		    printf("Detaching at pc %x ",
				trapStatePtr->pc);
		}
	        {
		    int	dummy;

		    PutReplyBytes(4, (Address) &dummy);
		    SendReply();
		}

		dbg_BeingDebugged = FALSE;
		done = TRUE;
		printf("Sprite is now detached from the debugger\r\n");
		break;

	    case DBG_UNKNOWN:
		printf("debugger: unrecognized request\n");
		break;
	}

	if (dbgTraceLevel >= 2) {
	    printf("\r\n");
	}
	if (!done) {
	     short	t_opcode;
	     /*
	      * The kmsg program still sends short opcodes. If the top 16 bits
	      * are zero we assume that it is a long opcode.
	      */
	    (void) ReadRequest(FALSE);
	    GetRequestBytes(2, (Address)&t_opcode);
	    if (t_opcode == 0) {
		GetRequestBytes(2, (Address)&t_opcode);
	    }
	    opcode = (Dbg_Opcode) t_opcode;
	}
    }


#ifndef FIRST_RUN
    VmMachSetKernelContext(oldContext);
#endif
    mach_AtInterruptLevel = atInterruptLevel;
    dbg_UsingNetwork = FALSE;

    /*
     * Don't force system log output to the console.
     */
    if (!syslogDiverted) {
	Dev_SyslogDebug(FALSE);
    }
#ifdef sun3
    /*
     * Turn off non-maskable interrupts.
     */
    Mach_MonStopNmi();
#endif
}
