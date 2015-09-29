/* 
 * devTMR.c --
 *
 *	The devTMR is a mirosecond resolution timer installable in the
 *	DES chip sockets on some sun3 and sun4 computers. It uses
 *	the AMD Am9513 timer chip. It is used to provide high resolution
 *	timings for the Sprite kernel and user programs.  
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
static char rcsid[] = "$Header: /sprite/src/kernel/dev/sun3.md/RCS/devTMR.c,v 9.2 90/09/11 12:41:06 rab Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include "sprite.h"
#include "mach.h"
#include "devInt.h"
#include "vm.h"
#include "vmMach.h"
#include "devTMR.h"

volatile DevTimerChip *dev_TimerAddr;
int devTimerJunkVar;

static void TimerInit _ARGS_((register volatile DevTimerChip *tmrp));


/*
 *----------------------------------------------------------------------
 *
 * Dev_TimerProbe --
 *
 *	Check for the existant of the timer chip controller. 
 *
 * Results:
 *	DEV_NO_CONTROLLER if chip not present, the address of the 
 *	chip otherwise.
 *
 * Side effects:
 *	dev_TimerAddr is initialized to the address of the time if it 
 *	is present and the timer is started running in one microsecond
 *	mode.
 *
 *----------------------------------------------------------------------
 */
ClientData
Dev_TimerProbe(ctrlLocPtr)
     DevConfigController	*ctrlLocPtr;	/* Controller location. */
{
    register volatile DevTimerChip *tmrbase = 
	(DevTimerChip *) ctrlLocPtr->address;
    ReturnStatus status;
    char	byte;

    byte = DEV_TIMER_RESET;
    status = Mach_Probe(1,&byte, (char *)&(tmrbase->cmdPort));
    if (status != SUCCESS) {
	return DEV_NO_CONTROLLER;
    }

    MACH_DELAY(4); 
    status = Mach_Probe(1,(char *)&(tmrbase->d_reg), &byte);
    if ((status != SUCCESS) || (byte == (char)0xff)) {
	return DEV_NO_CONTROLLER;
    }
    dev_TimerAddr = tmrbase;
    return ((ClientData) dev_TimerAddr);
}


/*
 * Initialize device.  
 */
static struct TimerSetup {
   unsigned char model;
   unsigned char modeh;
   unsigned char loadl;
   unsigned char loadh;
   unsigned char holdl;
   unsigned char holdh;
   } TimerSetup[] = { {DEV_TIMER_CNT, DEV_TIMER_F2, 0,0,0,0},
				  {DEV_TIMER_CNT, DEV_TIMER_TC, 0,0,0,0},
				  {DEV_TIMER_CNT, DEV_TIMER_TC, 0,0,0,0},
				  {DEV_TIMER_CNT, DEV_TIMER_TC, 0,0,0,0},
				  {DEV_TIMER_CNT, DEV_TIMER_TC, 0,0,0,0} };


/*
 *----------------------------------------------------------------------
 *
 * TimerInit --
 *
 * The Am9513 counter's 5 stages is concatenated to form an 80 bit
 * counter.   
 *
 * Results:
 *	None
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
static void
TimerInit(tmrp)
    register volatile DevTimerChip *tmrp;
{
    int i;  /* cntr pointer */

    DISABLE_INTR();
    DEV_TIMER_CMD_PORT;
    tmrp->d_reg = DEV_TIMER_RESET; 
    MACH_DELAY(2);
    tmrp->d_reg = DEV_TIMER_LDALL; 
    MACH_DELAY(2);
    tmrp->d_reg = DEV_TIMER_LDDPTR|DEV_TIMER_ELEMCYC; 
    MACH_DELAY(2);
    DEV_TIMER_DATA_PORT;
    for (i = 0; i < 5; ++i) {
       tmrp->d_reg = TimerSetup[i].model; MACH_DELAY(2);
       tmrp->d_reg = TimerSetup[i].modeh; MACH_DELAY(2);
       tmrp->d_reg = TimerSetup[i].loadl; MACH_DELAY(2);
       tmrp->d_reg = TimerSetup[i].loadh; MACH_DELAY(2);
       tmrp->d_reg = TimerSetup[i].holdl; MACH_DELAY(2);
       tmrp->d_reg = TimerSetup[i].holdh; MACH_DELAY(2);
    }
    DEV_TIMER_CMD_PORT;
    tmrp->d_reg = DEV_TIMER_ARM;
    ENABLE_INTR();
}

/*
 *----------------------------------------------------------------------
 *
 * Dev_TimerIOControl --
 *
 *	Perform device-specific functions with the timer chip.
 *
 * Results:
 *	SUCCESS		-
 *
 * Side effects:
 *	Variable.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
Dev_TimerIOControl(devicePtr, ioctlPtr, replyPtr)
    Fs_Device	        *devicePtr;
    Fs_IOCParam		*ioctlPtr;
    Fs_IOReply		*replyPtr;
{
   register volatile DevTimerChip *tmrp = dev_TimerAddr;
   register DevTimerVal *set;
   unsigned int t0, t1;
   DevTimerTest *tare;

    replyPtr->length = ioctlPtr->outBufSize;
    switch (ioctlPtr->command) {
       case IOC_DEV_TIMER_INIT: 
		    TimerInit(tmrp); 
		    return (SUCCESS);
       case IOC_DEV_TIMER_CMDWR:   
		    if ( ioctlPtr->inBufSize != 1) {
			return(GEN_INVALID_ARG);
		    } 
		    DEV_TIMER_CMD_PORT;
		    tmrp->d_reg = *(unsigned char *) ioctlPtr->inBuffer;
		    return(SUCCESS); 
       case IOC_DEV_TIMER_DATAWR:  
		    if ( ioctlPtr->inBufSize != 1) {
			return(GEN_INVALID_ARG);
		    } 
		    DEV_TIMER_DATA_PORT;
		    tmrp->d_reg = *(unsigned char *) ioctlPtr->inBuffer;
		    return(SUCCESS); 
      case IOC_DEV_TIMER_CMDRD:   
		    if ( ioctlPtr->outBufSize != 1) {
			return(GEN_INVALID_ARG);
		    } 
		    DEV_TIMER_CMD_PORT;
		    *( unsigned char *) ioctlPtr->outBuffer = tmrp->d_reg;
		    return(SUCCESS);
       case IOC_DEV_TIMER_DATARD:  
		    if ( ioctlPtr->outBufSize != 1) {
			return(GEN_INVALID_ARG);
		    } 
		    DEV_TIMER_DATA_PORT;
		    *( unsigned char *) ioctlPtr->outBuffer = tmrp->d_reg;
		    return(SUCCESS);
       case IOC_DEV_TIMER_TEST:
		    if (ioctlPtr->outBufSize != sizeof(DevTimerTest)) {
			return(GEN_INVALID_ARG);
		    } 
		    tare = (DevTimerTest *) ioctlPtr->outBuffer;
		    Dev_TimerReadReg( (unsigned char *) &t0, 4); 
		    Dev_TimerReadReg( (unsigned char *) &t1, 4);
		    tare->proc = t1-t0;
		    Dev_TimerReadRegInline(&t0); Dev_TimerReadRegInline(&t1);
		    tare->inLine = t1-t0;
		    return(SUCCESS);
       case IOC_DEV_TIMER_SET:
		    if (ioctlPtr->inBufSize != sizeof(DevTimerVal)) {
			return(GEN_INVALID_ARG);
		    } 
		    DEV_TIMER_CMD_PORT;
		    set = (DevTimerVal *) ioctlPtr->inBuffer;
		    DISABLE_INTR();
		    tmrp->d_reg = set->cmd; 
		    MACH_DELAY(2);
		    DEV_TIMER_DATA_PORT;
		    tmrp->d_reg = set->datalo;
		    MACH_DELAY(2);
		    tmrp->d_reg = set->datahi;
		    ENABLE_INTR();
		    return(SUCCESS);
       default :  
	   return (GEN_INVALID_ARG);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Dev_TimerRead --
 *
 *	Read data from the Timer registers.  
 *
 * Results:
 *	SUCCESS		- the data was obtained.
 *	FAILURE		- an invalid argument.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
ReturnStatus
Dev_TimerRead(devicePtr, readPtr, replyPtr)
    Fs_Device	*devicePtr;
    Fs_IOParam	*readPtr;	/* Read parameter block */
    Fs_IOReply	*replyPtr;	/* Return length and signal */ 
{

   int		bufSize;	/* Size of buffer. */

   bufSize = readPtr->length;
   if (bufSize > 10) {
       bufSize  = 10;
   }
   Dev_TimerReadReg( (unsigned char *) readPtr->buffer, bufSize);
   replyPtr->length = bufSize;

   return (SUCCESS);
}

/*
 *----------------------------------------------------------------------
 *
 * Dev_TimerReadReg --
 *
 *	Read a value from the timer's regiser
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
Dev_TimerReadReg( cntrs, cnt)
  register unsigned char *cntrs;
  register int cnt;
{
   register volatile DevTimerChip *tmrp = dev_TimerAddr;

   DISABLE_INTR();
   DEV_TIMER_CMD_PORT;
   tmrp->d_reg = DEV_TIMER_SAVE; MACH_DELAY(1);
   tmrp->d_reg = DEV_TIMER_LDDPTR|DEV_TIMER_HOLDCYC;
   DEV_TIMER_DATA_PORT;
   for (; cnt > 0; cnt--) {
	  MACH_DELAY(1);
      cntrs[cnt] = tmrp->d_reg;
   }
   ENABLE_INTR();
}

/*
 *----------------------------------------------------------------------
 *
 * Dev_TimerOpen --
 *
 *	Open the /dev/timer device.  
 *
 * Results:
 *	SUCCESS		- the device was opened.
 *
 * Side effects:
 *	The cc is "opened" and static variables are initialized.
 *
 *----------------------------------------------------------------------
 */


/*ARGSUSED*/
ReturnStatus
Dev_TimerOpen(devicePtr, useFlags, notifyToken, flagsPtr)
    Fs_Device *devicePtr;	/* Specifies type and unit number. */
    int useFlags;		/* Flags from the stream being opened */
    Fs_NotifyToken notifyToken;	/* Used for Fs call-back to notify waiting
				 * processes that the console device is ready.*/
    int *flagsPtr;              /* OUT: Device IO flags */
{
    if (dev_TimerAddr == (volatile DevTimerChip *) 0) {
	return DEV_NO_DEVICE;
    }
    return SUCCESS;
}
