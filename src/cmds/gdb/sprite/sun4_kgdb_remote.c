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
#include <sys/types.h>
#include <sys/file.h>
#include <errno.h>
#include "kernel/machTypes.h"

#include "defs.h"
#include "param.h"
#include "frame.h"
#include "inferior.h"
#include "value.h"
#include "expression.h"
#include "wait.h"
#include "sprite.h"
#include "kernel/sun4.md/vmSunConst.h"
#include "kernel/sun4.md/machConst.h"
#include "kernel/sun4.md/dbg.h"
#define NOGAP

/*
 * Hostname of attached remote hosts.  Error otherwise.
 */

char *hostName;

/*
 * Useful macros
 * ERROR_NO_ATTACHED_HOST - Remote an error and abort if no host is
 *			    currently attached.
 * MARK_DISCONNECTED - Mark a host a disconnected and free up state.
 */

#define ERROR_NO_ATTACHED_HOST \
	if (!hostName) error("No machine attached.");

#define MARK_DISCONNECTED  {	 \
	initialized = 0;     	 \
	inferior_pid = 0;   	 \
	hostName = (char *) 0;	 \
	free(dataCache); 	 \
	free(cacheInfo); 	 \
	}


static	int	initialized = 0; /* Set to true when remote connection is
				  * initialized. */

/*
 * Table mapping kernel exceptions into Unix signals.  
 */

struct sig_mapping_struct {
	char 	*sig_name;	/* Print string for signal. */
	int	 dbgSig; 	/* Boolean - A signal used by the debugger. */
	int 	unixSignal; 	/* Unix signal equalient. */
} sig_mapping[] =  {
/* 0 */ { "Reset Trap", 0, SIGQUIT },
/* 1 */ { "Instruction Fault", 0, SIGSEGV },
/* 2 */ { "Illegal Instruction Fault", 0, SIGILL },
/* 3 */ { "Privilege Instruction Fault", 0, SIGILL },
/* 4 */ { "FPU Disabled Fault", 0, SIGFPE },
/* 5 */ { "Window Overflow Fault", 0, SIGBUS },
/* 6 */ { "Window Underflow Fault", 0, SIGBUS },
/* 7 */ { "Memory Address Fault", 0, SIGSEGV },
/* 8 */ { "FPU Exception Fault", 0, SIGFPE },
/* 9 */ { "Data Fault",  0, SIGSEGV },
/* 10 */ { "Tag Overflow Trap", 0, SIGSEGV },
/* 11 */ { "Unknown Trap 11",  0, SIGSEGV },
/* 12 */ { "Unknown Trap 12",  0, SIGSEGV },
/* 13 */ { "Unknown Trap 13",  0, SIGSEGV },
/* 14 */ { "Unknown Trap 14",  0, SIGSEGV },
/* 15 */ { "Unknown Trap 15",  0, SIGSEGV },
/* 16 */ { "Interrupt Trap",  0, SIGINT },
/* 17 */ { "Level 1 Interrupt", 0, SIGINT },
/* 18 */ { "Level 2 Interrupt", 0, SIGINT },
/* 19 */ { "Level 3 Interrupt", 0, SIGINT },
/* 20 */ { "Level 4 Interrupt", 0, SIGINT },
/* 21 */ { "Level 5 Interrupt", 0, SIGINT },
/* 22 */ { "Level 6 Interrupt", 0, SIGINT },
/* 23 */ { "Level 7 Interrupt", 0, SIGINT },
/* 24 */ { "Level 8 Interrupt", 0, SIGINT },
/* 25 */ { "Level 9 Interrupt", 0, SIGINT },
/* 26 */ { "Level 10 Interrupt", 0, SIGINT },
/* 27 */ { "Level 11 Interrupt", 0, SIGINT },
/* 28 */ { "Level 12 Interrupt", 0, SIGINT },
/* 29 */ { "Level 13 Interrupt", 0, SIGINT },
/* 30 */ { "Level 14 Interrupt", 0, SIGINT },
/* 31 */ { "Level 15 Interrupt", 0, SIGINT },
/* 32 */ { "Breakpoint Trap",  1, SIGTRAP },
/* 33 */ { "Unknown Trap",  0, SIGSEGV },
/* 34 */ { "UNKNOWN EXCEPTION", 0, SIGSEGV },
};

#define	NUM_SIG_MAPS	(sizeof(sig_mapping)/sizeof(sig_mapping[0]))



#define	PBUFSIZ	1024

/* Maximum number of bytes to read/write at once.  The value here
   is chosen to fill up a packet (the headers account for the 32).  */
#define MAXBUFBYTES ((PBUFSIZ-32)/2)


/*
 *----------------------------------------------------------------------
 *
 * Regnum_to_index --
 *
 *	Function mapping a gdb register number into an index.
 *
 * Results:
 *	Index to Mach_State structure when treated as an array on ints.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int 
Regnum_to_index(r) 
    int	r;
{ 
#define	O(f)	((int)&(((Mach_RegState *) 0)->f)/sizeof(int))
  if (r < 8) return O(globals[r]);
  if (r < 16) return  O(ins[r-8]);
  if (r < 32) return (r - 16);
  if (r == Y_REGNUM) return O(y);
  if (r == PS_REGNUM) return O(curPsr);
  if (r == PC_REGNUM) return O(pc);
  if (r == NPC_REGNUM) return O(nextPc);
  return -1;
#undef O
};
static int lastPid = -1;	/* Process ID of process being examined. -1
				 * means process causing trap. */

/*
 * The following variables are sued when using the core memory interface
 * rather than active ethernet debugging.
 */
static int remoteCoreChan = -1;	/* Open file descriptor of core file. -1 
				 * means no file open. */
static char *remoteCoreFile;    /* Malloced name of core file. */
static int remoteOffset;	/* Offset used to convert addresses to 
				 * offsets into core file. */
static StopInfo	remoteStopInfo;    /* Current Dbg_StopInfo from core file. */
static Dbg_DumpBounds remoteBounds;  /* Dump bounds from core file. */

/*
 * IN_CORE_FILE - Returns TRUE if address is in the corefile.
 */
#define	IN_CORE_FILE(addr) ((addr) >= (CORE_ADDR) remoteBounds.kernelCodeStart\
	  && ((addr) < (CORE_ADDR) remoteBounds.fileCacheStart + \
				          remoteBounds.fileCacheSize))

/*
 *----------------------------------------------------------------------
 *
 * coreSeek -
 *
 *	Finds address in core file and seeks to it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
coreSeek(file, addr, whence)
     int file;
     CORE_ADDR addr;
     int whence;
{
  if ((int)addr > (remoteBounds.kernelCodeStart + remoteBounds.kernelCodeSize)) {
    if ((int)addr < remoteBounds.kernelDataStart) {
      return -1;
    }
    else if ((int)addr > (remoteBounds.kernelDataStart + remoteBounds.kernelDataSize)) {
      if ((int)addr < remoteBounds.kernelStacksStart) {
	return -1;
      }
      else if ((int)addr > (remoteBounds.kernelStacksStart + remoteBounds.kernelStacksSize)) {
	if ((int)addr < remoteBounds.fileCacheStart) {
	  return -1;
	}
	else {
	  lseek(file, addr - remoteBounds.fileCacheStart 
		+ sizeof(remoteStopInfo) + sizeof(remoteBounds)
		+ remoteBounds.kernelCodeSize + remoteBounds.kernelDataSize
		+ remoteBounds.kernelStacksSize, whence);
	}
      }
      else {
	lseek(file, addr - remoteBounds.kernelStacksStart
	      + sizeof(remoteStopInfo) + sizeof(remoteBounds)
	      + remoteBounds.kernelCodeSize + remoteBounds.kernelDataSize,
	      whence);
      }
    }
    else {
      lseek(file, addr - remoteBounds.kernelDataStart
	    + sizeof(remoteStopInfo) + sizeof(remoteBounds)
	    + remoteBounds.kernelCodeSize, whence);
    }
  }
  else {
    lseek(file, addr - remoteOffset, whence);
  }
}


/*
 *----------------------------------------------------------------------
 *
 * remote_core_file_command --
 *
 *	kgdb.sun4 core_file command.
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
remote_core_file_command (filename, from_tty)
     char *filename;
     int from_tty;
{
  /* Discard all vestiges of any previous core file
     and mark data and stack spaces as empty.  */

  if (remoteCoreFile)
    free (remoteCoreFile);
  remoteCoreFile = 0;

  if (remoteCoreChan >= 0)
    close (remoteCoreChan);
  remoteCoreChan = -1;
  if (filename == 0) {
    printf ("Corefile cleared.\n");
    return;
  }

  filename = tilde_expand (filename);
  make_cleanup (free, filename);

  remoteCoreChan = open (filename, O_RDONLY, 0);
  if (remoteCoreChan < 0)
    perror_with_name (filename);

  {
    /*
     * Read the StopInfo and bounds and start the debugging session.
     */
    int val;
    val = myread (remoteCoreChan, &remoteStopInfo, sizeof remoteStopInfo);
    if (val < 0)
      perror_with_name (filename);
    val = myread (remoteCoreChan, &remoteBounds, sizeof remoteBounds);
    if (val < 0)
      perror_with_name (filename);
    remote_debugging = 1;
    remoteOffset = remoteBounds.kernelCodeStart - sizeof(remoteStopInfo) -
		    sizeof(remoteBounds);
    start_remote();
  }
}

/* Open a connection to a remote debugger.
   NAME is the filename used for communication.  */

void
remote_open (name, from_tty)
     char *name;
     int from_tty;
{
  if (remoteCoreChan >= 0) {
      error("Can't attach a machine when using a corefile\n");
      return;
  }

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



/* Tell the remote machine to resume.  */

int     step_addr, step_addr_contents[2];

int
remote_resume (step, signal)
     int step, signal;
{

  ERROR_NO_ATTACHED_HOST;
  step_addr = -2;
  if (step) {
         static char break_insn[] = BREAKPOINT;
         step_addr = read_register(NPC_REGNUM);
         if ((remote_read_bytes(step_addr,step_addr_contents,8) != 0) ||
		 (remote_write_bytes(step_addr,break_insn, 4) != 0) ||
		 (remote_write_bytes(step_addr+4,break_insn, 4) != 0)) {
	     error("Can't set single step breakpoint at 0x%x\n", step_addr);
	  }

  }
  if (Kdbx_Trace( DBG_CONTINUE, 0, 0, sizeof(int)) < 0) {
        error("error trying to continue process\n");
  }

}

/* Wait until the remote machine stops, then return,
   storing status in STATUS just as `wait' would.  */

int
remote_wait (status)
     WAITTYPE *status;
{
    StopInfo    stopInfo;
    int		trap;
    int	text_size;
    extern CORE_ADDR text_start, text_end;
    if (remoteCoreChan >= 0) {
	/*
	 * Debugging using core file, just set text_{start, end} and
	 * returned stopped signal.
	 */
	status->w_status = 0;
	status->w_stopval = WSTOPPED;
	trap = DBG_CVT_MACH_TRAP(remoteStopInfo.trapType);
	status->w_stopsig = sig_mapping[trap].unixSignal;
	if ((lastPid == -1) && !sig_mapping[trap].dbgSig) { 
	    printf("Kernel returns with signal (%d) %s\n",
		    remoteStopInfo.trapType,
		    sig_mapping[trap].sig_name);
	}
	text_start = remoteBounds.kernelCodeStart;
	text_end = remoteBounds.kernelCodeStart + remoteBounds.kernelCodeSize;
	return status->w_stopsig;
    }
    ERROR_NO_ATTACHED_HOST;

    if (Kdbx_Trace(DBG_GET_STOP_INFO, (char *) 0, (char *)&stopInfo,
               sizeof(stopInfo)) != 0) {
	error("Can't get stop info\n");
    }

    if (stopInfo.regs.pc == step_addr ||
	stopInfo.regs.pc == step_addr+4) {
         if (remote_write_bytes(step_addr,step_addr_contents, 8) != 0) 
	     error("Can't restore single step address\n");
    }
    status->w_status = 0;
    status->w_stopval = WSTOPPED;

    trap = DBG_CVT_MACH_TRAP(stopInfo.trapType);
    status->w_stopsig = sig_mapping[trap].unixSignal;
    if ((lastPid != -1) && !sig_mapping[trap].dbgSig) { 
	printf("Kernel returns with signal (%d) %s\n",stopInfo.trapType,
		sig_mapping[trap].sig_name);
    }

    text_size = text_end - text_start;
    text_size &= ~(8*1024-1);
    text_start = stopInfo.codeStart - 8*1024;
    text_end = text_start+text_size;
    return status->w_stopsig;
}

/* Read the remote registers into the block REGS.  */
#define FIRST_LOCAL_REGNUM	16
void
remote_fetch_registers (regs)
     char *regs;
{
  StopInfo    stopInfo;
  if (remoteCoreChan >= 0) {

      bcopy(remoteStopInfo.regs.globals,regs,4*8);
      bcopy(remoteStopInfo.regs.ins,regs+4*8,4*8);
      ((int *)regs)[Y_REGNUM] = remoteStopInfo.regs.y; 
      ((int *)regs)[PS_REGNUM] = remoteStopInfo.regs.curPsr; 
      ((int *)regs)[PC_REGNUM] = remoteStopInfo.regs.pc; 
      ((int *)regs)[NPC_REGNUM] = remoteStopInfo.regs.nextPc; 
      remote_read_bytes(remoteStopInfo.regs.ins[6], 
		    ((int *) regs) + FIRST_LOCAL_REGNUM,4*16);
      return;
  }
  ERROR_NO_ATTACHED_HOST;
  Kdbx_Trace(DBG_GET_STOP_INFO, (char *) 0, (char *)&stopInfo,
               sizeof(stopInfo));
  bcopy(stopInfo.regs.globals,regs,4*8);
  bcopy(stopInfo.regs.ins,regs+4*8,4*8);
  ((int *)regs)[Y_REGNUM] = stopInfo.regs.y; 
  ((int *)regs)[PS_REGNUM] = stopInfo.regs.curPsr; 
  ((int *)regs)[PC_REGNUM] = stopInfo.regs.pc; 
  ((int *)regs)[NPC_REGNUM] = stopInfo.regs.nextPc; 
  remote_read_bytes(stopInfo.regs.ins[6], 
		((int *) regs) + FIRST_LOCAL_REGNUM,4*16);

}


int
remote_attach(pid)
    int	pid;
{
    int	status;
    struct expression *expr;
    register struct cleanup *old_chain;
    register value val;
    int	machRegStateAddr;
    Proc_ControlBlock	*procPtr;
    Mach_RegState machRegState;
    char	exp[128];

    if (remoteCoreChan >= 0) {
	if (pid != lastPid) {
	  /*
	   * Switching to a new process. 
	   */
	  if (pid == -1) {
	      /*
	       * Process -1 is the trap process. Whose stop info is 
	       * in the beginning of the corefile.
	       */
	      if (lseek(remoteCoreChan, 0, L_SET) < 0) {
		  error("lseek corefile");
		  return 0;
	       }
	      if (myread(remoteCoreChan, &remoteStopInfo, 
			sizeof(remoteStopInfo)) < 0) {
		  error("read corefile");
		  return 0;
	      }
	      lastPid = pid;
	      start_remote();
	      return 1;
	  }
	  /*
	   * Lookup the switch regs of the specified process.
	   * They are in proc_PCBTable[pidSlot]->machStatePtr->switchRegs.
	   */
	  sprintf(exp,"proc_PCBTable[%d]", pid & 0xff);
	  expr = parse_c_expression (exp);
	  old_chain = make_cleanup (free_current_contents, &expr);

	  val = evaluate_expression (expr);
	  do_cleanups (old_chain);
	  if ((value_as_long(val) == NIL) || (value_as_long(val) == 0)) {
	      error("Pid 0x%x does not have a control block\n", pid);
	  }
	  sprintf(exp,"proc_PCBTable[%d]->machStatePtr->switchRegs", pid & 0xff);
	  expr = parse_c_expression (exp);
	  old_chain = make_cleanup (free_current_contents, &expr);
	  val = evaluate_expression (expr);
	  do_cleanups (old_chain);
	  machRegStateAddr = value_as_long(val);
	  if (remote_read_bytes(machRegStateAddr, &machRegState, 
			sizeof(Mach_RegState)) != 0) {
		error("Can't read  regs from address 0x%x\n", machRegStateAddr);
	  }
	  remoteStopInfo.regs = machRegState;
	  /*
	   * The PC that is stored for a switch registers from on the
	   * sun4 is bogus.  We set the PC to be inside Mach_ContextSwitch.
	   */
	  strcpy(exp,"&Mach_ContextSwitch");
	  expr = parse_c_expression (exp);
	  old_chain = make_cleanup (free_current_contents, &expr);
	  val = evaluate_expression (expr);
	  do_cleanups (old_chain);
	  remoteStopInfo.regs.pc = value_as_long(val)+16;
	  remoteStopInfo.regs.nextPc = remoteStopInfo.regs.pc+4;
	  lastPid = pid;
	}
	start_remote();
	return 1;
    }
    ERROR_NO_ATTACHED_HOST;
    if (pid != lastPid) {
	Kdbx_Trace(DBG_SET_PID, &pid, 0,sizeof(int));
	lastPid = pid;
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

  if (remoteCoreChan >= 0) {
      if (IN_CORE_FILE(addr)) {
#ifndef NOGAP
	  if (lseek(remoteCoreChan, addr - remoteOffset, L_SET) < 0) {
#else
	  if (coreSeek(remoteCoreChan, addr, L_SET) < 0) {
#endif
	      perror("lseek corefile");
	      errno = EIO;
	      return 0;
	   }
	  if (myread(remoteCoreChan, &buffer, sizeof(buffer)) < 0) {
	      perror("read corefile");
	      errno = EIO;
	      return 0;
	  }
      } else {
	  /* fprintf(stderr, "Address 0x%x out of range\n", addr); */
	  errno = EIO;
	  return 0;
      }
      return buffer;
  }

  ERROR_NO_ATTACHED_HOST;
  if (addr >= text_start && addr < text_end)
	{
	 if (Kdbx_Trace(DBG_INST_READ, addr, &buffer, sizeof(int)) != 0) {
		errno = EIO;
		return 0;
	 }
	 return buffer;
	}
  if (Kdbx_Trace(DBG_DATA_READ, addr, &buffer, sizeof(int)) != 0) {
	errno = EIO;
	return 0;
  }
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
	  if (Kdbx_Trace(DBG_INST_WRITE, &word, addr, sizeof(word))!= 0) {
		errno = EIO;
	    }
	    return;
	}
  if ( Kdbx_Trace(DBG_DATA_WRITE,  &word, addr, sizeof(word)) != 0) {
     errno = EIO;
  }
  return;
}

/* Write memory data directly to the remote machine.
   This does not inform the data cache; the data cache uses this.
   MEMADDR is the address in the remote memory space.
   MYADDR is the address of the buffer in our space.
   LEN is the number of bytes.  */

int
remote_write_bytes (memaddr, myaddr, len)
     CORE_ADDR memaddr;
     char *myaddr;
     int len;
{
  extern CORE_ADDR text_start, text_end;
  ERROR_NO_ATTACHED_HOST;
  if (memaddr >= text_start && memaddr < text_end)
	{
	  return Kdbx_Trace(DBG_INST_WRITE, myaddr, memaddr, len);
	}
  return Kdbx_Trace(DBG_DATA_WRITE,  myaddr, memaddr, len);
}

/* Read memory data directly from the remote machine.
   This does not use the data cache; the data cache uses this.
   MEMADDR is the address in the remote memory space.
   MYADDR is the address of the buffer in our space.
   LEN is the number of bytes.  */

int
remote_read_bytes (memaddr, myaddr, len)
     CORE_ADDR memaddr;
     char *myaddr;
     int len;
{
    int err;

  if (remoteCoreChan >= 0) {
      if (IN_CORE_FILE(memaddr)) {
#ifndef NOGAP
	  if (lseek(remoteCoreChan, memaddr - remoteOffset, L_SET) < 0) {
#else
	  if (coreSeek(remoteCoreChan, memaddr, L_SET) < 0) {
#endif
	      err = errno;
	      perror("lseek corefile");
	      return err;
	   }
	  if (myread(remoteCoreChan,myaddr, len) != len) {
	      err = errno;
	      perror("read corefile");
	      return (err > 0) ? err : 1;
	  }
    } else {
	  /* fprintf(stderr, "Address 0x%x out of range\n", memaddr); */
	  return EIO;
    }
    return 0;
  }
  ERROR_NO_ATTACHED_HOST;
  return Kdbx_Trace(DBG_DATA_READ, memaddr, myaddr, len);
}

/* Read LEN bytes from inferior memory at MEMADDR.  Put the result
   at debugger address MYADDR.  Returns errno value.  */
int
remote_read_inferior_memory(memaddr, myaddr, len)
     CORE_ADDR memaddr;
     char *myaddr;
     int len;
{
  int xfersize, err;
  while (len > 0)
    {
      if (len > MAXBUFBYTES)
	xfersize = MAXBUFBYTES;
      else
	xfersize = len;

      err = remote_read_bytes (memaddr, myaddr, xfersize);
      if (err != 0) 
	  return err;
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
  int err;
  while (len > 0)
    {
      if (len > MAXBUFBYTES)
	xfersize = MAXBUFBYTES;
      else
	xfersize = len;

      err = remote_write_bytes(memaddr, myaddr, xfersize);
      if (err != 0) {
	return err;
      }

      memaddr += xfersize;
      myaddr  += xfersize;
      len     -= xfersize;
    }
  return 0; /* no error */
}

/* Store the remote registers from the contents of the block REGS.  */

void
remote_store_registers (regs,regno)
     char *regs;
     int	regno;
{
	int	i;

  ERROR_NO_ATTACHED_HOST;
    if (regno < 0) { 
	remote_write_bytes(((int *) regs)[SP_REGNUM], 
			    ((int *) regs) + FIRST_LOCAL_REGNUM,4*16);
	for (i = 0; i < 16; i++) {
	    Kdbx_Trace(DBG_WRITE_REG, &(((int *)regs)[i]),
				    Regnum_to_index(i),sizeof(int));
	}	
       Kdbx_Trace(DBG_WRITE_REG, ((int *)regs)+Y_REGNUM,
		    Regnum_to_index(Y_REGNUM),sizeof(int));
       Kdbx_Trace(DBG_WRITE_REG, ((int *)regs)+PC_REGNUM,
		    Regnum_to_index(PC_REGNUM),sizeof(int));
       Kdbx_Trace(DBG_WRITE_REG, ((int *)regs)+NPC_REGNUM,
			Regnum_to_index(NPC_REGNUM),sizeof(int));
       Kdbx_Trace(DBG_WRITE_REG, ((int *)regs)+PS_REGNUM,
			Regnum_to_index(PS_REGNUM),sizeof(int));

    } else {
       int	ind = Regnum_to_index(regno);
       if (ind >= 0) 
	   Kdbx_Trace(DBG_WRITE_REG, ((int *)regs)+regno,ind,sizeof(int));
       else
	    remote_write_bytes(((int *) regs)[SP_REGNUM], 
		    ((int *) regs) + regno,4);
    }
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
 *     Setup r network socket.
 * ----------------------------------------------------------------------------
 */
static void
StartDebugger()
{
        char	*host = hostName;
	hostName = (char *) 0;
	netSocket = CreateSocket(host);
	hostName = host;
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
     {
	Dbg_Opcode	opcode;

	msgSize = numBytes;
	if (newRequest) {
	    msgNum++;
	}
	*(int *)requestBuffer = msgNum;
	bcopy(&msg, requestBuffer + 4, numBytes);
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
		opcode == DBG_WRITE_REG || opcode == DBG_SET_PID) {
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
     {
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
	int	dummy;

	RecvReply(DBG_CONTINUE, 4, &dummy, NULL, 0);
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
    msg.opcode = opcode;
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
int 
Kdbx_Trace(opcode, srcAddr, destAddr, numBytes)
    Dbg_Opcode	opcode;		/* Which command */
    char	*srcAddr;	/* Where to read data from */
    char	*destAddr;	/* Where to write data to */
    int		numBytes;	/* The number of bytes to read or write */
{
    int			(*intrHandler)();
    int			i;
    int			retVal = 0;

    if (!initialized) {
	int	moreData;
	/*
	 * Setup the cache and initiate a conversation with the other kernel.
	 */
	if (cacheBlockSize == -1) {
	     {
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
		return EIO;
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
	case DBG_READ_ALL_REGS:
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
	case DBG_WRITE_REG:
	    msg.data.writeReg.regNum = (int) destAddr;
	    msg.data.writeReg.regVal = *(int *) srcAddr;
	    SendRequest(sizeof(msg.opcode) + sizeof(Dbg_WriteReg), 1);
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
		retVal = EIO;
		fprintf(stderr, "ERROR: invalid write address 0x%x\n",destAddr);
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
    return(retVal);
}
remote_close()
{
}
remote_clean_up()
{
	MARK_DISCONNECTED;
}


read_kmem(memaddr, myaddr, len)
    char *memaddr;
    char *myaddr;
    int	  len;
{
    static int fd = -1;
    int count;

    if (fd < 0) { 
	char template[100];
	/*
	 * Open a temp file to write counters to. We unlink the file so it will
	 * disappear when we exit.
	 */
	strcpy(template, "/tmp/kgdbXXXXXXXX");
	fd = mkstemp(template);
	if (fd < 0) {
	    error("open kmem tmp file");
	}
	(void) unlink(template);
    }
    if (lseek(fd, 0, L_SET) < 0) {
	error("lseek kmem file");
    }
    count = write(fd, memaddr, len);
    if (count != len) {
	return EIO;
    }

    /*
     * Rewind the file and read the counters from it.
     */
    count = lseek(fd, 0, L_SET);
    if (count >= 0) {
        count = read(fd, myaddr, len);
    }
    if (count != len) {
	return EIO;
    }
    return 0;
}


