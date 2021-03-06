/* Memory-access and commands for inferior process, for GDB.
   Copyright (C)  1988 Free Software Foundation, Inc.

GDB is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY.  No author or distributor accepts responsibility to anyone
for the consequences of using it or for whether it serves any
particular purpose or works at all, unless he says so in writing.
Refer to the GDB General Public License for full details.

Everyone is granted permission to copy, modify and redistribute GDB,
but only under the conditions described in the GDB General Public
License.  A copy of this license is supposed to have been given to you
along with GDB so you can know your rights and responsibilities.  It
should be in a file named COPYING.  Among other things, the copyright
notice and this notice must be preserved on all copies.

In other words, go ahead and share GDB, but don't try to stop
anyone else from sharing it farther.  Help stamp out software hoarding!
*/


#include <stdio.h>
#include <sprite.h>
#include <signal.h>
#include <assert.h>

#include "defs.h"
#include "param.h"
#include "frame.h"
#include "inferior.h"
#define HAVE_WAIT_STRUCT
#include "wait.h"
#include "kernel/sun3.md/dbg.h"


#define ERROR_NO_ATTACHED_HOST \
	if (!hostName) error("No machine attached.");

#define MARK_DISCONNECTED  {	 \
	initialized = 0;     	 \
	inferior_pid = 0;   	 \
	hostName = (char *) 0;	 \
	free(dataCache); 	 \
	free(cacheInfo); 	 \
	}


int kiodebug;
    static	int	initialized = 0;

int icache;

/* Descriptor for I/O to remote machine.  */
int remote_desc;

#define	PBUFSIZ	300


/* Maximum number of bytes to read/write at once.  The value here
   is chosen to fill up a packet (the headers account for the 32).  */
#define MAXBUFBYTES ((PBUFSIZ-32)/2)

static void remote_send ();
static void putpkt ();
static void getpkt ();
static void dcache_flush ();


struct sig_mapping_struct {
	char 	*sig_name;	/* Print string for signal. */
	int	 dbgSig; 	/* Boolean - A signal used by the debugger. */
	int 	unixSignal; 	/* Unix signal equalient. */
} sig_mapping[] =  {

/* 0 */     { "Console Interrupt", 0, SIGINT }, 
/* 1 */     { "System Reset", 0, SIGQUIT },
/* 2 */     { "Bus Error", 0, SIGBUS },
/* 3 */     { "Address Error", 0, SIGSEGV },
/* 4 */     { "Illegal Instruction", 0, SIGILL },
/* 5 */     { "Division by zero", 0, SIGFPE } ,
/* 6 */     { "CHK instruction", 0, SIGBUS } ,
/* 7 */     { "Trapv instruction", 0, SIGBUS } ,
/* 8 */     { "Priviledge Violation", 0, SIGSEGV },
/* 9 */     { "Trace Trap", 1,  SIGTRAP },
/* 10 */    { "Emulator 1010 trap", 0,SIGEMT },
/* 11 */    { "Emulator 1111 trap", 0,SIGEMT },
/* 12 */    { "unknown exception", 0, SIGSEGV },
/* 13 */    { "unknown exception", 0, SIGSEGV },
/* 14 */    { "Stack format error", 0,SIGSEGV },
/* 15 */    { "Uninitialized vector", 0,SIGSEGV },
/* 16 */    { "unknown exception", 0, SIGSEGV },
/* 17 */    { "unknown exception", 0, SIGSEGV },
/* 18 */    { "unknown exception", 0, SIGSEGV },
/* 19 */    { "unknown exception", 0, SIGSEGV },
/* 20 */    { "unknown exception", 0, SIGSEGV },
/* 21 */    { "unknown exception", 0, SIGSEGV },
/* 22 */    { "unknown exception", 0, SIGSEGV },
/* 23 */    { "unknown exception", 0, SIGSEGV },
/* 24 */    { "Spurious interrupt", 0, SIGSEGV }, 
/* 25 */    { "Level 1 interrupt", 0, SIGSEGV },
/* 26 */    { "Level 2 interrupt", 0, SIGSEGV },
/* 27 */    { "Level 3 interrupt", 0, SIGSEGV },
/* 28 */    { "Level 4 interrupt", 0, SIGSEGV },
/* 29 */    { "Level 5 interrupt", 0, SIGSEGV },
/* 30 */    { "Level 6 interrupt", 0, SIGSEGV },
/* 31 */    { "Level 7 interrupt", 0, SIGSEGV },
/* 32 */    { "unknown exception", 0, SIGSEGV },
/* 33 */    { "System call trap", 0, SIGSEGV },
/* 34 */    { "Return from signal trap",0, SIGSEGV },
/* 35 */    { "Bad trap",0,SIGSEGV },
/* 36 */    { "unknown exception", 0, SIGSEGV },
/* 37 */    { "unknown exception", 0, SIGSEGV },
/* 38 */    { "unknown exception", 0, SIGSEGV },
/* 39 */    { "unknown exception", 0, SIGSEGV },
/* 40 */    { "unknown exception", 0, SIGSEGV },
/* 41 */    { "unknown exception", 0, SIGSEGV },
/* 42 */    { "unknown exception", 0, SIGSEGV },
/* 43 */    { "unknown exception", 0, SIGSEGV },
/* 44 */    { "unknown exception", 0, SIGSEGV },
/* 45 */    { "unknown exception", 0, SIGSEGV },
/* 46 */    { "unknown exception", 0, SIGSEGV },
/* 47 */    { "Breakpoint trap", 1,SIGTRAP},
/* 48 */    { "Floating point unordered condition", 0, SIGFPE },
/* 49 */    { "Floating point inexact result", 0, SIGFPE },
/* 50 */    { "Floating point divide by zero", 0, SIGFPE },
/* 51 */    { "Floating point underflow", 0, SIGFPE },
/* 52 */    { "Floating point operand error", 0, SIGFPE },
/* 53 */    { "Floating point overflow", 0, SIGFPE },
/* 54 */    { "Floating point not a number", 0, SIGFPE },
/* 55 */    { "unknown exception", 0, SIGSEGV },
};

#define	NUM_SIG_MAPS	(sizeof(sig_mapping)/sizeof(sig_mapping[0]))

static int lastPid = -1;
char *hostName;

/* Open a connection to a remote debugger.
   NAME is the filename used for communication.  */

void
remote_open (name, from_tty)
     char *name;
     int from_tty;
{
  if (name[0] == '/')
      name++;
  hostName = savestring(name,strlen(name));
  if (from_tty)
    printf ("Remote debugging using %s\n", name);
  remote_debugging = 1;
}

char *
remote_version()
{
  static char	version[1024];

  ERROR_NO_ATTACHED_HOST;
  Kdbx_Trace(DBG_GET_VERSION_STRING, 0, version, 1024);
  return version;
}

remote_load() { }



/* Tell the remote machine to resume.  */

int
remote_resume (step, signal)
     int step, signal;
{

  int cur_pc;
  ERROR_NO_ATTACHED_HOST;
  cur_pc = read_pc();
  if (Kdbx_Trace(step ? DBG_SINGLESTEP : DBG_CONTINUE, &cur_pc, 0, 
			sizeof(int)) < 0) {
        error("error trying to continue process\n");
  }

}

PrintStopInfo(stopInfoPtr)
    StopInfo *stopInfoPtr;
{

    assert(NUM_SIG_MAPS == DBG_UNKNOWN_EXC + 1);
    if (stopInfoPtr->trapCode >= NUM_SIG_MAPS) {
		stopInfoPtr->trapCode = NUM_SIG_MAPS-1;
    }
    if (!sig_mapping[stopInfoPtr->trapCode].dbgSig) { 
	printf("Kernel returns with signal (%d) %s\n",stopInfoPtr->trapCode,
			sig_mapping[stopInfoPtr->trapCode].sig_name);
    }
}
/* Wait until the remote machine stops, then return,
   storing status in STATUS just as `wait' would.  */

int
remote_wait (status)
     WAITTYPE *status;
{
    StopInfo    stopInfo;
    int	text_size;
    extern CORE_ADDR text_start, text_end;
  ERROR_NO_ATTACHED_HOST;

  Kdbx_Trace(DBG_GET_STOP_INFO, (char *) 0, (char *)&stopInfo,
               sizeof(stopInfo));

    status->w_status = 0;
    status->w_stopval = WSTOPPED;
    assert(NUM_SIG_MAPS == DBG_UNKNOWN_EXC + 1);
    if (stopInfo.trapCode >= NUM_SIG_MAPS) {
		stopInfo.trapCode = NUM_SIG_MAPS-1;
    }

    status->w_stopsig = sig_mapping[stopInfo.trapCode].unixSignal;
    if (!sig_mapping[stopInfo.trapCode].dbgSig) { 
	printf("Kernel returns with signal (%d) %s\n",stopInfo.trapCode,
			sig_mapping[stopInfo.trapCode].sig_name);
    }

    text_size = text_end - text_start;
    text_size &= ~(8*1024-1);
    text_start = stopInfo.codeStart - 8*1024;
    text_end = text_start+text_size;
    return status->w_stopsig;
}

/* Read the remote registers into the block REGS.  */

void
remote_fetch_registers (regs)
     char *regs;
{
  StopInfo    stopInfo;
  ERROR_NO_ATTACHED_HOST;
  Kdbx_Trace(DBG_GET_STOP_INFO, (char *) 0, (char *)&stopInfo,
               sizeof(stopInfo));
  bcopy(stopInfo.genRegs,regs,16*sizeof(int));
  ((int *) regs)[PC_REGNUM] = stopInfo.pc;
  ((int *) regs)[PS_REGNUM] = stopInfo.statusReg;

}

/* Store the remote registers from the contents of the block REGS.  */

void
remote_store_registers (regs)
     char *regs;
{
	char	old_regs[REGISTER_BYTES];
	int	i;
        remote_fetch_registers (old_regs);
	for (i = 0; i < 16; i++) {
	    if ( ((int *)regs)[i] != ((int *) old_regs)[i]) {
		Kdbx_Trace(DBG_WRITE_GPR, &(((int *)regs)[i]),i, sizeof(int));
 	    }	
	}
		
}

int
remote_attach(pid)
    int	pid;
{
    int	status;
    ERROR_NO_ATTACHED_HOST;
    if (pid != lastPid) {
	lastPid = pid;
	Kdbx_Trace(DBG_SET_PID, &pid, 0,sizeof(int));
    }
    start_remote();
    return 1;
}
int
remote_detach(sig)
    int	sig;
{
  int cur_pc;

  ERROR_NO_ATTACHED_HOST;
  cur_pc = read_pc();
  if (sig) 
      Kdbx_Trace(DBG_DETACH, &cur_pc, 0, sizeof(int));
   remote_clean_up();
   return 0;
}



/* Read a word from remote address ADDR and return it.
   This goes through the data cache.  */

int
remote_fetch_word (addr)
     CORE_ADDR addr;
{

  int buffer;
  extern CORE_ADDR text_start, text_end;

  ERROR_NO_ATTACHED_HOST;
  if (addr >= text_start && addr < text_end)
	{
	 Kdbx_Trace(DBG_INST_READ, addr, &buffer, sizeof(int));
	  return buffer;
	}
  Kdbx_Trace(DBG_DATA_READ, addr, &buffer, sizeof(int));
  return buffer;
}

/* Write a word WORD into remote address ADDR.
   This goes through the data cache.  */

void
remote_store_word (addr, word)
     CORE_ADDR addr;
     int word;
{
  extern CORE_ADDR text_start, text_end;
  ERROR_NO_ATTACHED_HOST;
 if (addr >= text_start && addr < text_end)
	{
	 Kdbx_Trace(DBG_INST_WRITE, &word, addr, sizeof(word));
	  return ;
	}
  Kdbx_Trace(DBG_DATA_WRITE,  &word, addr, sizeof(word));
  return ;
}

/* Write memory data directly to the remote machine.
   This does not inform the data cache; the data cache uses this.
   MEMADDR is the address in the remote memory space.
   MYADDR is the address of the buffer in our space.
   LEN is the number of bytes.  */

void
remote_write_bytes (memaddr, myaddr, len)
     CORE_ADDR memaddr;
     char *myaddr;
     int len;
{
  ERROR_NO_ATTACHED_HOST;
  Kdbx_Trace(DBG_DATA_WRITE, myaddr, memaddr, len);
}

/* Read memory data directly from the remote machine.
   This does not use the data cache; the data cache uses this.
   MEMADDR is the address in the remote memory space.
   MYADDR is the address of the buffer in our space.
   LEN is the number of bytes.  */

void
remote_read_bytes (memaddr, myaddr, len)
     CORE_ADDR memaddr;
     char *myaddr;
     int len;
{

  ERROR_NO_ATTACHED_HOST;
   Kdbx_Trace(DBG_DATA_READ, memaddr, myaddr, len);
}


/* Read LEN bytes from inferior memory at MEMADDR.  Put the result
   at debugger address MYADDR.  Returns errno value.  */
int
remote_read_inferior_memory(memaddr, myaddr, len)
     CORE_ADDR memaddr;
     char *myaddr;
     int len;
{
  int xfersize;
  while (len > 0)
    {
      if (len > MAXBUFBYTES)
	xfersize = MAXBUFBYTES;
      else
	xfersize = len;

      remote_read_bytes (memaddr, myaddr, xfersize);
      memaddr += xfersize;
      myaddr  += xfersize;
      len     -= xfersize;
    }
  return 0; /* no error */
}

/* Copy LEN bytes of data from debugger memory at MYADDR
   to inferior's memory at MEMADDR.  Returns errno value.  */
int
remote_write_inferior_memory (memaddr, myaddr, len)
     CORE_ADDR memaddr;
     char *myaddr;
     int len;
{
  int xfersize;
  while (len > 0)
    {
      if (len > MAXBUFBYTES)
	xfersize = MAXBUFBYTES;
      else
	xfersize = len;

      remote_write_bytes(memaddr, myaddr, xfersize);

      memaddr += xfersize;
      myaddr  += xfersize;
      len     -= xfersize;
    }
  return 0; /* no error */
}

/* 
 * Call a remote function.
 */
call_remote_function(funaddr,nargs,numBytes,argBuffer)
    CORE_ADDR funaddr;
    int		nargs;
    int		numBytes;
    char	*argBuffer;
{
  int	returnValue;
  ERROR_NO_ATTACHED_HOST;
  Kdbx_Trace(DBG_BEGIN_CALL, (char *)0, (char *)0, 0);  
  returnValue = Kdbx_Trace(DBG_CALL_FUNCTION,argBuffer,funaddr,numBytes);
  Kdbx_Trace(DBG_END_CALL, (char *)0, (char *)0, 0);       
  return returnValue;
}

void
remote_reboot (args)
     char *args;
{

  ERROR_NO_ATTACHED_HOST;
  if (!args)
	args = "";

  Kdbx_Trace(DBG_REBOOT, args, NULL, strlen(args));
}


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sgtty.h>

/*
 * Direct mapped cache of data and code.   The cache is flushed after every
 * step and continue by incrementing the version number.  Flushing code
 * isn't necessary but since kdbx already has an internal code cache it
 * doesn't hurt and makes life easier.
 */
static	int	cacheBlockSize = -1;
static	int	cacheBlockShift = -1;
static	int	cacheSize = 128 * 1024;
#define	CACHE_BLOCK_MASK 	(cacheSize / cacheBlockSize - 1)
#define	CACHE_BLOCK_OFFSET_MASK	(cacheBlockSize - 1)
#define	NUM_CACHE_BLOCKS	(cacheSize >> cacheBlockShift)
#define	CACHE_OFFSET_MASK	(cacheSize - 1)
#define	GET_CACHE_BLOCK(address) (((unsigned int) address) >> cacheBlockShift)
/*
 * The data cache is just an array of characters.
 */
static	char	*dataCache;
/*
 * There is information about kept about each cache block.
 */
typedef	struct {
    int		version;	/* Version number of this cache block. */
    char	*realAddr;	/* Actual address of data stored in the block.*/
} CacheInfo;
static CacheInfo	*cacheInfo;
static int	currentVersion = 1;

/*
 * Stuff for the serial port.
 */
static	int	kernChannel = 0;
#ifndef KDBX_SERIAL_PORT
#define KDBX_SERIAL_PORT "/dev/ttya"
#endif
static	char kdbxSerialPort[] = KDBX_SERIAL_PORT;
static	int	rs232Debug = 0;


/*
 * Message buffers.
 */
static Dbg_Msg	msg;
static int	msgSize;
#define	REPLY_BUFFER_SIZE	16384
static	char	replyBuffer[REPLY_BUFFER_SIZE];
static	char	requestBuffer[DBG_MAX_REQUEST_SIZE];
static	int	msgNum = 0;

static void	RecvReply();
static int ReadBytes(), WriteBytes();

static	struct sockaddr_in	remote;
static	int			kdbxTimeout = 1;
static	int			netSocket;


/*
 *----------------------------------------------------------------------
 *
 * CreateSocket --
 *
 *	Creates a UDP socket connected to the Sprite host's kernel 
 *	debugger port.
 *
 * Results:
 *	The stream ID of the socket.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
CreateSocket(spriteHostName)
    char	*spriteHostName;
{
    int			socketID;
    struct hostent 	*hostPtr;

    hostPtr = gethostbyname(spriteHostName);
    if (hostPtr == (struct hostent *) NULL) {
	error("CreateSocket: unknown host %s\n", spriteHostName);
    }
    if (hostPtr->h_addrtype != AF_INET) {
	error("CreateSocket: bad address type for host %s\n", 
		spriteHostName);
    }

    socketID = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketID < 0) {
	perror_with_name("CreateSocket: socket");
    }

    bzero((Address)&remote, sizeof(remote));
    bcopy(hostPtr->h_addr, (Address)&remote.sin_addr, hostPtr->h_length);
    remote.sin_port = htons(DBG_UDP_PORT);
    remote.sin_family = AF_INET;

    if (connect(socketID, (struct sockaddr *) &remote, sizeof(remote)) < 0) {
	perror_with_name("CreateSocket: connect");
    }

    return(socketID);
}


/*
 * ----------------------------------------------------------------------------
 *
 *  StartDebugger --
 *
 *     Start off a new conversation with the debugger.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Setup rs232 channel or network socket.
 * ----------------------------------------------------------------------------
 */
static void
StartDebugger()
{
    if (rs232Debug) {
	unsigned	char	ch;
	struct 		sgttyb 	modes;
	extern		int promptAfterLoading;
	char		dummyBuffer[BUFSIZ];
	extern		char	*gets();
	int			i;

	kernChannel = open(kdbxSerialPort, 2);
	if (kernChannel < 0) {
	    perror_with_name("Could not open kernTty\r\n");
	    exit(1);
	}
    
	/*
	 * Flush the line.
	 */
	i = 0;
	ioctl(kernChannel, TIOCFLUSH, &i);

	/*
	 * Turn off parity and echoing.
	 */
	if (ioctl(kernChannel,TIOCGETP,&modes) < 0) {
	    perror("ioctl get modes");
	    abort();
	}
	modes.sg_flags |= RAW|ANYP;
	modes.sg_flags &= ~ECHO;
	if (ioctl(kernChannel,TIOCSETP,&modes) < 0) {
	    perror("ioctl put modes");
	    abort();
	}

	/*
	 * Send the other half a magic sequence of characters that it is
	 * waiting for. 
	 */
	ch = 127;
	WriteBytes(&ch, 1, 0);
	ch = 27;
	WriteBytes(&ch, 1, 0);
	ch = 7;
	WriteBytes(&ch, 1, 0);
	sleep(5);
    } else {
        char	*host = hostName;
	hostName = (char *) 0;
	netSocket = CreateSocket(host);
	hostName = host;
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 *  SendRequest --
 *
 *     Send a request message to the kernel.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 * ----------------------------------------------------------------------------
 */
static void
SendRequest(numBytes, newRequest)
    int		numBytes;
    Boolean	newRequest;
{
    if (rs232Debug) {
	/*
	 * First send the opcode.
	 */
	WriteBytes(&msg.opcode, sizeof(msg.opcode), 1);
	if ((Dbg_Opcode) msg.opcode != DBG_DATA_WRITE && 
	    (Dbg_Opcode) msg.opcode != DBG_INST_WRITE) {
	    /*
	     * If not a write send the rest.
	     */
	    WriteBytes(&msg.data, numBytes - sizeof(msg.opcode), 1);
	} else {
	    /*
	     * Send the size and numBytes first and then the data.
	     */
	    WriteBytes(&msg.data.writeMem, 2 * sizeof(int), 1);
	    WriteBytes(msg.data.writeMem.buffer,
	               msg.data.writeMem.numBytes, 1);
	}
    } else {
	Dbg_Opcode	opcode;

	msgSize = numBytes;
	if (newRequest) {
	    msgNum++;
	}
	*(int *)requestBuffer = msgNum;
#ifdef sparc
	bcopy(&msg, requestBuffer + 4, 2);
	bcopy(((char *) &msg)+4, requestBuffer + 6, numBytes-2);
#else
	bcopy(&msg, requestBuffer + 4, numBytes);
#endif
	if (write(netSocket, requestBuffer, numBytes + 4) < numBytes + 4) {
	     MARK_DISCONNECTED;
	    perror_with_name("SendRequest: Couldn't write to the kernel socket\n");
	    return;
	}
	if (newRequest) {
	    opcode = (Dbg_Opcode) msg.opcode;
	    if (opcode == DBG_DETACH || opcode == DBG_CONTINUE ||
		opcode == DBG_SINGLESTEP || opcode == DBG_DIVERT_SYSLOG || 
		opcode == DBG_BEGIN_CALL || 
		opcode == DBG_WRITE_GPR || opcode == DBG_SET_PID) {
		int	dummy;
		/*
		 * Wait for explicit acknowledgments of these packets.
		 */
		RecvReply(opcode, 4, &dummy, NULL, 1);
	    }
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 *  RecvReply --
 *
 *     Receive a reply from the kernel.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 * ----------------------------------------------------------------------------
 */
static void
RecvReply(opcode, numBytes, destAddr, readStatusPtr, timeout)
    Dbg_Opcode	opcode;
    int		numBytes;
    char	*destAddr;
    int	*readStatusPtr;
    int	timeout;
{
    int		status;
    int 	resendRequest = 0;

    if (numBytes + 8 > REPLY_BUFFER_SIZE) {
	fprintf(stderr,"numBytes <%d> > REPLY_BUFFER_SIZE <%d>\n",
		    numBytes + 8, REPLY_BUFFER_SIZE);
	abort();
    }
    if (rs232Debug) {
	if (opcode == DBG_DATA_READ || opcode == DBG_INST_READ) {
	    ReadBytes(&status, sizeof(status));
	    if (status == 0) {
		*readStatusPtr = 0;
		return;
	    }
	    *readStatusPtr = 1;
	    ReadBytes(destAddr, numBytes);
	} else if (opcode == DBG_END_CALL) {
	    /*
	     * End call returns the length and then the data.  First read
	     * then length, then the data, then dump the log.
	     */
	    ReadBytes(&numBytes, sizeof(numBytes));
	    if (numBytes == 0) {
		*readStatusPtr = 0;
	    } else {
		ReadBytes(replyBuffer, numBytes);
		write(1, replyBuffer, numBytes);
		*readStatusPtr = 1;
	    }
	} else {
	    ReadBytes(destAddr, numBytes);
	}
    } else {
	int		readMask;
	struct	timeval	interval;
	int		bytesRead;

	interval.tv_sec = kdbxTimeout;
	interval.tv_usec = 0;
	do {
	    if (timeout) {
		int	numTimeouts;

		numTimeouts = 0;
		/*
		 * Loop timing out and sending packets until a new packet
		 * has arrived.
		 */
		do {
		    if (!resendRequest) { 
			readMask = 1 << netSocket;
			status = select(32, &readMask, NULL, NULL, &interval);
		    } else {
			status = 0;
			resendRequest = 0;
		    }
		    if (status == 1) {
			break;
		    } else if (status == -1) {
		        MARK_DISCONNECTED;
			perror_with_name("RecvReply: Couldn't select on socket.\n");
		    } else if (status == 0) {
			SendRequest(msgSize, 0);
			numTimeouts++;
			if (numTimeouts % 10 == 0) {
			    fprintf(stderr, 
				    "Timing out and resending to host %s\n",
				    hostName);
			    fflush(stderr);
			    QUIT;
			}
		    }
		} while (1);
	    }
	    if (opcode == DBG_DATA_READ || opcode == DBG_INST_READ ||
		opcode == DBG_GET_VERSION_STRING) {
		/*
		 * Data and instruction reads return variable size packets.
		 * The first two ints are message number and status.  If
		 * the status is OK then the data follows.
		 */
		immediate_quit++;
		bytesRead = read(netSocket, replyBuffer, numBytes + 8);
		immediate_quit--;
		if (bytesRead < 0) {
		    MARK_DISCONNECTED;
		    perror_with_name("RecvReply: Error reading socket.");
		}
		/*
		 * Check message number before the size because this could
		 * be an old packet.
		 */
		if (*(int *)replyBuffer != msgNum) {
		    printf("RecvReply: Old message number = %d, expecting %d\n",
			    *(int *)replyBuffer, msgNum);
		    fflush(stdout);
		    resendRequest = 1;
		    continue;
		}
		if (bytesRead == 8) {
		    /*
		     * Only 8 bytes so the read failed and there is no data.
		     */
		    *readStatusPtr = 0;
		    return;
		}
	        if (opcode == DBG_GET_VERSION_STRING) {
		     strncpy(destAddr, (char *)(replyBuffer + 4),numBytes);
		     return;
		}
		if (bytesRead != numBytes + 8) {
		    printf("RecvReply: Short read (1): op=%d exp=%d read=%d",
			    opcode, numBytes + 4, bytesRead);
		    continue;
		}
		*readStatusPtr = 1;
		bcopy(replyBuffer + 8, destAddr, numBytes);
		return;
	    } else if (opcode == DBG_END_CALL) {
		int	length;
		/*
		 * End call returns a variable size packet that contains
		 * the result of the call. The format of the message is 
		 * message number, length, data.
		 */
		immediate_quit++;
		bytesRead = read(netSocket, replyBuffer, REPLY_BUFFER_SIZE);
		immediate_quit--;
		if (bytesRead < 0) {
		    MARK_DISCONNECTED;
		    perror_with_name("RecvReply: Error reading socket.");
		}
		/*
		 * Check message number before the size because this could
		 * be an old packet.
		 */
		if (*(int *)replyBuffer != msgNum) {
		    printf("RecvReply: Old message number = %d, expecting %d\n",
			    *(int *)replyBuffer, msgNum);
		    fflush(stdout);
		    resendRequest = 1;
		    continue;
		}
		length = *( int *)(replyBuffer + 4);
		if (bytesRead - 8 != length) {
		    fprintf(stderr, "RecyReply: Short read for syslog data\n");
		    fflush(stderr);
		    length = bytesRead - 8;
		}
		if (length == 0) {
		    /*
		     * No data.
		     */
		    *readStatusPtr = 0;
		    return;
		}
		/*
		 * Dump out the buffer.
		 */
		write(1, replyBuffer + 8, length);
		*readStatusPtr = 1;
		return;
	    } else {
		/*
		 * Normal request so just read in the message which includes
		 * the message number.
		 */
		immediate_quit++;
		bytesRead = read(netSocket, replyBuffer, numBytes + 4);
		immediate_quit--;
		if (bytesRead < 0) {
		    MARK_DISCONNECTED;
		    perror_with_name("RecvReply: Error reading socket (2).");
		}
		/*
		 * Check message number before size because it could be
		 * an old packet.
		 */
		if (*(int *)replyBuffer != msgNum) {
		    printf("RecvReply: Old message number = %d, expecting %d\n",
			    *(int *)replyBuffer, msgNum);
		    fflush(stdout);
		    resendRequest = 1;
		    continue;
		}
		if (bytesRead != numBytes + 4) {
		    printf("RecvReply: Short read (2): op=%d exp=%d read=%d",
			    opcode, numBytes + 4, bytesRead);
		}
		if (*(int *)replyBuffer != msgNum) {
		    continue;
		}
		bcopy(replyBuffer + 4, destAddr, numBytes);
		return;
	    }
	} while (1);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 *  WaitForKernel --
 *
 *      Wait for the kernel to send us a message to indicate that it is waiting
 *	to be debugged.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 * ----------------------------------------------------------------------------
 */
static void
WaitForKernel()
{
    if (rs232Debug) {
	unsigned char ch;

	if (read(kernChannel, &ch, 1) != 1) {
	    fprintf(stderr,"Read from kernChannel (2)");
	    abort();
	}
    } else {
	int	dummy;

	RecvReply(DBG_CONTINUE, 4, &dummy, NULL, 0);
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * BlockInCache --
 *
 *     See if the given block at the given address is in the cache.
 *
 * Results:
 *     1 if found block in cache, 0 if didn't.
 *
 * Side effects:
 *     None.
 */
static int
BlockInCache(blockNum, addr)
    int		blockNum;
    char	*addr;
{
    blockNum = blockNum & CACHE_BLOCK_MASK;
    return((int) (cacheInfo[blockNum].version == currentVersion &&
	   (unsigned int) cacheInfo[blockNum].realAddr == 
		    ((unsigned int) (addr) & ~CACHE_BLOCK_OFFSET_MASK)));
}


/*
 * ----------------------------------------------------------------------------
 *
 * FetchBlock --
 *
 *     Fetch the given data block from the cache or the kernel if necessary.
 *
 * Results:
 *     1 if could fetch block into cache, 0 if couldn't.
 *
 * Side effects:
 *     Data cache modified.
 */
static int
FetchBlock(blockNum, srcAddr, opcode)
    int		blockNum;
    char	*srcAddr;
    Dbg_Opcode	opcode;
{
    int	successfulRead;

    blockNum = blockNum & CACHE_BLOCK_MASK;
    srcAddr = (char *) ((unsigned int) (srcAddr) & ~CACHE_BLOCK_OFFSET_MASK);

    if (BlockInCache(blockNum, srcAddr)) {
	return(1);
    }
    msg.opcode = (short)opcode;
    msg.data.readMem.address = (int) srcAddr;
    msg.data.readMem.numBytes = cacheBlockSize;
    SendRequest(sizeof(msg.opcode) + sizeof(Dbg_ReadMem), 1);
    RecvReply(opcode, cacheBlockSize, 
		&dataCache[(unsigned int) (srcAddr) & CACHE_OFFSET_MASK],
		&successfulRead, 1);
    if (successfulRead) {
	cacheInfo[blockNum].version = currentVersion;
	cacheInfo[blockNum].realAddr = srcAddr;
    }
    return(successfulRead);
}


/*
 * ----------------------------------------------------------------------------
 *
 * WriteBytes --
 *
 *      Write the bytes over to the kernel across the serial line.  Every third
 *	character that we write is acknowledged by a character that is sent by
 *	the other kernel.  This is so we don't get ahead of the other kernel.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 */
static int
WriteBytes(buf, numBytes, ack)
    char *buf;		/* Pointer to buffer to write */
    int  numBytes;	/* Number of bytes to write */
    Boolean	ack;	/* Acknowledge when done and every 3 characters. */
{
    unsigned char input;
    int	i;
    int	toWrite;

    if (numBytes == 0) {
	return(0);
    }
    if (!ack) {
	if (write(kernChannel, buf, numBytes) < numBytes) {
	    MARK_DISCONNECTED;
	    perror_with_name("WriteBytes: Couldn't write to the kernel channel\n");
	    return(-1);
	}
	return(0);
    }

    do {
	if (numBytes > 3) {
	    toWrite = 3;
	} else {
	    toWrite = numBytes;
	}
	if (write(kernChannel, buf, toWrite) < toWrite) {
	    MARK_DISCONNECTED;
	    perror_with_name("WriteBytes: Couldn't write to the kernel channel\n");
	    return(-1);
	}
	buf += toWrite;
	numBytes -= toWrite;

	if (read(kernChannel, &input, 1) < 1) {
	    MARK_DISCONNECTED;
	    perror_with_name("WriteBytes: Couldn't read from the kernel channel\n");
	    return(-1);
	}
    } while (numBytes > 0);

    return(0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * ReadBytes --
 *
 *     Read the given number of bytes over from the kernel on the serial line. 
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 */
static int
ReadBytes(buf, numBytes)
    char *buf;		/* Buffer to read into */
    int  numBytes;	/* Number of bytes to read */
{
    int readSoFar;	/* The total number of bytes that we have read */
    int bytesRead;	/* The number of bytes read in the last read */
    int ackCount;	/* The number of bytes that have been acknowledgment */

    ackCount = 0;
    readSoFar = 0;
    while (readSoFar < numBytes) {
	bytesRead = read(kernChannel, &buf[readSoFar], numBytes - readSoFar);
	if (bytesRead < 0) {
	    MARK_DISCONNECTED;
	    perror_with_name("ReadBytes: Couldn't read from the kernel channel\n");
	    return(-1);
	}

	readSoFar += bytesRead;
	if (readSoFar - ackCount == DBG_ACK_SIZE) {
	    if (write(kernChannel, &readSoFar, 1) < 1) {
		MARK_DISCONNECTED;
		perror_with_name("ReadBytes: Couldn't write to kernel channel\n");
		return(-1);
	    }
	    ackCount += DBG_ACK_SIZE;
	}
    }

    return(0);
}


/*
 * ----------------------------------------------------------------------------
 *
 * Kdbx_Trace --
 *
 *     Write the trace command over to the kernel.  
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 * ----------------------------------------------------------------------------
 */
int Kdbx_Trace(opcode, srcAddr, destAddr, numBytes)
    Dbg_Opcode	opcode;		/* Which command */
    char	*srcAddr;	/* Where to read data from */
    char	*destAddr;	/* Where to write data to */
    int		numBytes;	/* The number of bytes to read or write */
{
    int			(*intrHandler)();
    int			i;

    if (!initialized) {
	int	moreData;
	/*
	 * Setup the cache and initiate a conversation with the other kernel.
	 */
	if (cacheBlockSize == -1) {
	    if (rs232Debug) {
		cacheBlockSize = 32;
		cacheBlockShift = 5;
	    } else {
		cacheBlockSize = 256;
		cacheBlockShift = 8;
	    }
	}
	dataCache = (char *) malloc(cacheSize);
	cacheInfo = (CacheInfo *) malloc(NUM_CACHE_BLOCKS * sizeof(CacheInfo));
	for (i = 0; i < NUM_CACHE_BLOCKS; i++) {
	    cacheInfo[i].version = 0;
	}
	StartDebugger();
	/*
	 * Dump the system log by faking a call command.
	 */
	printf("Dumping system log ...\n");
	fflush(stdout);
	msg.opcode = (short)DBG_BEGIN_CALL;
	SendRequest(sizeof(msg.opcode), 1);
	msg.opcode = (short)DBG_END_CALL;
	do {
	    SendRequest(sizeof(msg.opcode), 1);
	    RecvReply(msg.opcode, 0, NULL, &moreData, 1);
	} while (moreData);
	initialized = 1;
    }


    if (opcode == DBG_DATA_READ || opcode == DBG_INST_READ) {
	int			firstBlock;
	int			lastBlock;
	unsigned	int	cacheOffset;
	int			toRead;

	/*
	 * Read using the cache.
	 */
	firstBlock = GET_CACHE_BLOCK(srcAddr); 
	lastBlock = GET_CACHE_BLOCK(srcAddr + numBytes - 1);
	for (i = firstBlock; i <= lastBlock; i++) {
	    cacheOffset = ((unsigned int) srcAddr) & CACHE_OFFSET_MASK;
	    if (i == lastBlock) {
		toRead = numBytes;
	    } else if (i == firstBlock) {
		toRead = cacheBlockSize - 
			    (cacheOffset & CACHE_BLOCK_OFFSET_MASK);
	    } else {
		toRead = cacheBlockSize;
	    }
	    if (!FetchBlock(i, srcAddr, opcode)) {
		printf("ERROR: invalid read address 0x%x\n",srcAddr);
	    }
	    bcopy(&dataCache[cacheOffset], destAddr, toRead);
	    srcAddr += toRead;
	    destAddr += toRead;
	    numBytes -= toRead;
	}
	return(0);
    }

    if (opcode == DBG_DATA_WRITE || opcode == DBG_INST_WRITE) {
	int	firstBlock;
	int	lastBlock;
	int	cacheOffset;
	int	toWrite;
	char	*tSrcAddr;
	char	*tDestAddr;
	int	tNumBytes;

	/*
	 * If the block that is being fetched is in the cache then write the
	 * data there first before sending it over to the kernel.
	 */
	tSrcAddr = srcAddr;
	tDestAddr = destAddr;
	tNumBytes = numBytes;

	firstBlock = GET_CACHE_BLOCK(destAddr); 
	lastBlock = GET_CACHE_BLOCK(destAddr + numBytes - 1);
	for (i = firstBlock; i <= lastBlock; i++) {
	    cacheOffset = ((int) tDestAddr) & CACHE_OFFSET_MASK;
	    if (i == lastBlock) {
		toWrite = tNumBytes;
	    } else if (i == firstBlock) {
		toWrite = cacheBlockSize - 
			    (cacheOffset & CACHE_BLOCK_OFFSET_MASK);
	    } else {
		toWrite = cacheBlockSize;
	    }
	    if (BlockInCache(i, tDestAddr)) {
		bcopy(tSrcAddr, &dataCache[cacheOffset], tNumBytes);
	    }
	    tSrcAddr += toWrite;
	    tDestAddr += toWrite;
	    tNumBytes -= toWrite;
	}
    }

    msg.opcode = (short) opcode;

    /*
     * Do the rest of the work for the desired operation.
     */

    switch (opcode) {

	/*
	 * For these operations the desired data is read from the other
	 * kernel and stored at destAddr.
	 */
	case DBG_READ_ALL_GPRS:
	case DBG_GET_STOP_INFO:
	    SendRequest(sizeof(msg.opcode), 1);
	    RecvReply(opcode, numBytes, destAddr, NULL, 1);
	    break;

	/*
	 * For this operation the desired data is read from srcAddr
	 * and written to the other kernel.
	 */
	case DBG_SET_PID:
	    msg.data.pid = *(int *)srcAddr;
	    SendRequest(sizeof(msg.opcode) + sizeof(msg.data.pid), 1);
	    break;

	/*
	 * When writing a general purpose register first the address to write
	 * that is stored in destAddr must be given to the other kernel.
	 * Then the data itself which is stored at srcAddr can be written over.
	 */
	case DBG_WRITE_GPR:
	    msg.data.writeGPR.regNum = (int) destAddr;
	    msg.data.writeGPR.regVal = *(int *) srcAddr;
	    SendRequest(sizeof(msg.opcode) + sizeof(Dbg_WriteGPR), 1);
	    break;

	/*
	 * When writing to the kernels instruction or data space first the
	 * address of where to write to (destAddr) and then the number of
	 * bytes to write (numBytes) must be sent over.  Finally all of
	 * the data is read from srcAddr and written over.
	 */

	case DBG_INST_WRITE:
	case DBG_DATA_WRITE: {
	    char	writeStatus;

	    msg.data.writeMem.address = (int) destAddr;
	    msg.data.writeMem.numBytes = numBytes;
	    bcopy(srcAddr, msg.data.writeMem.buffer, numBytes);
	    SendRequest(sizeof(msg.opcode) + 2 * sizeof(int) + numBytes, 1);
	    RecvReply(opcode, 1, &writeStatus, NULL, 1);
	    if (writeStatus == 0) {
		error("ERROR: invalid write address 0x%x\n",destAddr);
	    } 
	    break;
	}
	case DBG_DIVERT_SYSLOG:
	    msg.data.syslogCmd = (Dbg_SyslogCmd)srcAddr;
	    SendRequest(sizeof(msg.opcode) + sizeof(Dbg_SyslogCmd), 1);
	    break;
	case DBG_BEGIN_CALL:
	    SendRequest(sizeof(msg.opcode), 1);
	    break;
	case DBG_END_CALL: {
	    Boolean	moreData;
	    do {
		SendRequest(sizeof(msg.opcode), 1);
		RecvReply(opcode, 0, NULL, &moreData, 1);
	    } while (moreData);
	    break;
	}

	case DBG_DETACH: {
	    msg.opcode = (short) DBG_DIVERT_SYSLOG;
	    msg.data.syslogCmd = DBG_SYSLOG_TO_ORIG;
	    SendRequest(sizeof(msg.opcode) + sizeof(Dbg_SyslogCmd), 1);

	    msg.opcode = (short) DBG_DETACH;
	    msg.data.pc = *(int *) srcAddr;
	    SendRequest(sizeof(msg.opcode) + sizeof(msg.data.pc), 1);
	    break;
	}

	case DBG_CONTINUE:
	case DBG_SINGLESTEP:
	    msg.data.pc = *(int *) srcAddr;
	    SendRequest(sizeof(msg.opcode) + sizeof(msg.data.pc), 1);
	    currentVersion++;
	    WaitForKernel();
	    break;

	case DBG_CALL_FUNCTION: {
	    int		returnValue;

	    msg.data.callFunc.address = (int) destAddr;
	    msg.data.callFunc.numBytes = numBytes;
	    bcopy(srcAddr, msg.data.callFunc.buffer, numBytes);
	    SendRequest(sizeof(msg.opcode) + 2 * sizeof(int) + numBytes, 1);
	    RecvReply(opcode, sizeof(returnValue), &returnValue, NULL, 1);
	    return (returnValue);
	}
	case DBG_REBOOT: {
	    msg.data.reboot.stringLength = numBytes;
	    bcopy(srcAddr, msg.data.reboot.string, numBytes);
	    SendRequest(sizeof(msg.opcode) + sizeof(int) + numBytes, 1);
	    return (0);
	}
	case DBG_GET_VERSION_STRING: {
	    SendRequest(sizeof(msg.opcode), 1);
	    RecvReply(opcode,numBytes , destAddr, NULL, 1);
	    return (0);
	}
	default:
	    printf("Unknown opcode %d\n", opcode);
	    return(-1);
    }
    return(0);
}
remote_clean_up()
{
	MARK_DISCONNECTED;
}
remote_close()
{
}
static __initialize_remote ()
{
}

