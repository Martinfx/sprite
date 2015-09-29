/* 
 * devZ8530.c --
 *
 *	This file provides low-level procedures to manipulate the
 *	Zilog 8530 Serial Communications Controller chip.  In particular,
 *	the code in this file is enough to allow a Z8530 chip to be used
 *	with devTty.c for terminals.
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
static char rcsid[] = "$Header: /cdrom/src/kernel/Cvsroot/kernel/dev/sun4.md/devZ8530.c,v 9.7 92/12/13 18:12:58 mgbaker Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#ifndef CLEAN_LOCK
#define CLEAN_LOCK 1
#endif

#include <sprite.h>
#include <stdio.h>
#include <mach.h>
#include <sync.h>
#include <z8530.h>
#include <sgtty.h>

/*
 * Spin-lock used for synchronization on all Z8530 device registers.
 */

static Sync_Semaphore z8530Mutex = Sync_SemInitStatic("Dev:z8530Mutex");

/*
 * Tables mapping sgttyb baud-rate values to actual integers.
 */

static struct {
    int sgttybVal;			/* Baud value from sgtyb. */
    int baud;				/* Integer baud rate. */
} baudMap[] = {
    {B0, 0},
    {B50, 50},
    {B75, 75},
    {B110, 110},
    {B134, 134},
    {B150, 150},
    {B200, 200},
    {B300, 300},
    {B600, 600},
    {B1200, 1200},
    {B2400, 2400},
    {B4800, 4800},
    {B9600, 9600}, 
    {-1, -1}
};

/*
 * Forward declarations to procedures defined in this file:
 */

static int		Read();
static void		Write();

/*
 *----------------------------------------------------------------------
 *
 * DevZ8530Activate --
 *
 *	This procedure is invoked in order to "activate" one half of a
 *	Z8530 chip.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The channel is re-initialized and the receiver is started.
 *
 *----------------------------------------------------------------------
 */

void
DevZ8530Activate(ptr)
    Address ptr;
{
    register DevZ8530 *zPtr;
    int speed;

    MASTER_LOCK(&z8530Mutex);
    zPtr = (DevZ8530 *) ptr;	/* Information about the device. */
    if (zPtr->flags & Z_CHANNEL_B) {
	Write(zPtr->address, 9, WRITE9_RESET_CHAN_B);
    } else {
	Write(zPtr->address, 9, WRITE9_RESET_CHAN_A);
    }
    MACH_DELAY(10);
#ifndef sun4c
    Write(zPtr->address, 2, zPtr->vector);
#endif
    Write(zPtr->address, 4, WRITE4_PARITY_EVEN + WRITE4_1_STOP + 
				WRITE4_X16_CLK);
    zPtr->wr3 &= ~WRITE3_RX_ENABLE;
    Write(zPtr->address, 3, zPtr->wr3);
    zPtr->wr5 &= ~(WRITE5_TX_ENABLE | WRITE5_BREAK);
    Write(zPtr->address, 5, zPtr->wr5);
    Write(zPtr->address, 11, WRITE11_TXCLK_BAUD + WRITE11_RXCLK_BAUD);
    speed = ZILOG_SPEED(zPtr->baud);
    Write(zPtr->address, 12, speed);
    Write(zPtr->address, 13, speed >> 8);
    Write(zPtr->address, 14, WRITE14_BAUD_FROM_PCLK);
    zPtr->wr3 |= WRITE3_RX_ENABLE;
    Write(zPtr->address, 3, zPtr->wr3);
    zPtr->wr5 |= (WRITE5_TX_ENABLE | WRITE5_RTS | WRITE5_DTR);
    Write(zPtr->address, 5, zPtr->wr5);
    Write(zPtr->address, 14, WRITE14_BAUD_ENABLE + 
				     WRITE14_BAUD_FROM_PCLK);
    Write(zPtr->address, 0, WRITE0_RESET_ERRORS);
    Write(zPtr->address, 0, WRITE0_RESET_STATUS);
    Write(zPtr->address, 9, WRITE9_MASTER_IE); 
    Write(zPtr->address, 1, WRITE1_RX_IE | WRITE1_TX_IE | WRITE1_EXT_IE); 
    Write(zPtr->address, 0, WRITE0_CLEAR_INTR);
    zPtr->oldRr0 = 0;
    zPtr->flags &= ~Z_INACTIVE;
    MASTER_UNLOCK(&z8530Mutex);
}

/*
 *----------------------------------------------------------------------
 *
 * DevZ8530RawProc --
 *
 *	This procedure is called back from the Td module as a raw
 *	control procedure.
 *
 * Results:
 *	The return value is the number of bytes returned to the caller
 *	at outBuffer.
 *
 * Side effects:
 *	Depends on the control operation.  Most likely effect is to
 *	start transferring output data.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
int
DevZ8530RawProc(ptr, operation, inBufSize, inBuffer, outBufSize, outBuffer)
    Address ptr;
    int operation;		/* What to do:  TD_RAW_OUTPUT_READY etc. */
    int inBufSize;		/* Size of input buffer for operation. */
    char *inBuffer;		/* Input buffer. */
    int outBufSize;		/* Size of output buffer for operation. */
    char *outBuffer;		/* Output buffer. */
{
    int result = 0;
    register DevZ8530 *zPtr;

    MASTER_LOCK(&z8530Mutex);
    zPtr = (DevZ8530 *) ptr;	/* Our information about device. */
    switch (operation) {
	case TD_RAW_START_BREAK:
	    zPtr->wr5 |= WRITE5_BREAK;
	    Write(zPtr->address, 5, zPtr->wr5);
	    break;

	case TD_RAW_STOP_BREAK:
	    zPtr->wr5 &= ~WRITE5_BREAK;
	    Write(zPtr->address, 5, zPtr->wr5);
	    break;

	case TD_RAW_SET_DTR:
	    zPtr->wr5 |= WRITE5_DTR|WRITE5_RTS;
	    Write(zPtr->address, 5, zPtr->wr5);
	    break;

	case TD_RAW_CLEAR_DTR:
	    zPtr->wr5 &= ~(WRITE5_DTR|WRITE5_RTS);
	    Write(zPtr->address, 5, zPtr->wr5);
	    break;

	case TD_RAW_SHUTDOWN:
	    Write(zPtr->address, 1, 0);
	    MACH_DELAY(10);
	    zPtr->flags |= Z_INACTIVE;
	    break;

	case TD_RAW_OUTPUT_READY:
	    if (Read(zPtr->address, 0) & READ0_TX_READY) {
		int c;

		c = (*zPtr->outputProc)(zPtr->outputData);
		if (c != -1) {
		    Write(zPtr->address, 8, c);
		}
	    }
	    break;

	case TD_RAW_FLUSH_OUTPUT:
	    while ((*zPtr->outputProc)(zPtr->outputData) != -1) {
		/* do nothing */
	    }
	    break;

	case TD_RAW_FLOW_CHARS:
	    /* Ignore flow-control chars. */
	    break;

	case TD_RAW_SET_BAUD_RATE: {
	    int i, zilogSpeed;
	    Td_BaudRate *brPtr;

	    /*
	     * Map the baud rate from an sgttyb constant to an actual
	     * number.  Return the value we actually set things to.
	     */

	    brPtr = (Td_BaudRate *) inBuffer;
	    for (i = 0; baudMap[i].baud != -1; i++) {
		if (baudMap[i].sgttybVal == brPtr->ospeed) {
		    zPtr->baud = baudMap[i].baud;
		    break;
		}
	    }
	    zilogSpeed = ZILOG_SPEED(zPtr->baud);
	    Write(zPtr->address, 14, WRITE14_BAUD_FROM_PCLK);
	    Write(zPtr->address, 12, zilogSpeed);
	    Write(zPtr->address, 13, zilogSpeed >> 8);
	    Write(zPtr->address, 14, WRITE14_BAUD_ENABLE +
		    WRITE14_BAUD_FROM_PCLK);

	    /*
	     * Fall through to next arm of case to return current
	     * settings.
	     */
	}

	case TD_RAW_GET_BAUD_RATE: {
	    int i;
	    Td_BaudRate *brPtr;

	    brPtr = (Td_BaudRate *) outBuffer;
	    if (outBufSize >= sizeof(Td_BaudRate)) {
		for (i = 0; baudMap[i].baud != -1; i++) {
		    if (baudMap[i].baud == zPtr->baud) {
			brPtr->ispeed = brPtr->ospeed = baudMap[i].sgttybVal;
			result = sizeof(Td_BaudRate);
		    }
		}
	    }
	    break;
	}
    }
    MASTER_UNLOCK(&z8530Mutex);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * DevZ8530Interrupt --
 *
 *	This procedure is invoked at interrupt level to deal with
 *	a channel of a Z5830 chip.
 *
 * Results:
 *      Always returns TRUE.
 *
 * Side effects:
 *	Input characters may be received, output characters may be
 *	transmitted, and special conditions may be handled.
 *
 *----------------------------------------------------------------------
 */

Boolean
DevZ8530Interrupt(clientData)
    ClientData	clientData; /* Information about the device. */
{
    register DevZ8530 *zPtr = (DevZ8530 *) clientData;	
    int rr0, rr1, c;
    int first = 1;

    MASTER_LOCK(&z8530Mutex);

    if (zPtr->flags & Z_INACTIVE) {
	Write(zPtr->address, 1, 0);
	Write(zPtr->address, 0, WRITE0_RESET_STATUS);
	MASTER_UNLOCK(&z8530Mutex);
	return TRUE;
    }

    /*
     * Check the input section of the channel.
     */

    while (TRUE) {
	rr0 = Read(zPtr->address, 0);
	if (rr0 & READ0_BREAK) {
	    zPtr->flags |= Z_BREAK;
	}
	if (!(rr0 & READ0_RX_READY)) {
	    break;
	}
	c = Read(zPtr->address, 8);
	if (first) {
	    first = 0;
	} else {
	}
	if (zPtr->flags & Z_BREAK) {
	    zPtr->flags &= ~Z_BREAK;
	    if (c != 0) {
		printf("Warning:  non-zero break character on %s\n",
			zPtr->name);
	    } else {
		c = DEV_TTY_BREAK;
	    }
	}
	(*zPtr->inputProc)(zPtr->inputData, c);
    }

    rr1 = Read(zPtr->address, 1);
    if (rr1 & (READ1_RX_OVERRUN|READ1_PARITY_ERROR|READ1_FRAMING_ERROR)) {
	if (rr1 & READ1_RX_OVERRUN) {
	    printf("Warning: receiver overrun on %s\n", zPtr->name);
	} else if (rr1 & READ1_PARITY_ERROR) {
	    printf("Warning: receiver parity error on %s\n", zPtr->name);
	} else {
	    printf("Warning: receiver framing error on %s\n", zPtr->name);
	}
	Write(zPtr->address, 0, WRITE0_RESET_ERRORS);
    }

    /*
     * Check the output section of the channel.
     */

    if (rr0 & READ0_TX_READY) {
	c = (*zPtr->outputProc)(zPtr->outputData);
	if (c == -1) {
	    Write(zPtr->address, 0, WRITE0_RESET_TX_INT);
	} else {
	    Write(zPtr->address, 8, c);
	}
    }

    /*
     * Reset status and interrupt bits.
     */

    rr0 &= (READ0_DCD|READ0_CTS|READ0_BREAK);
    if (zPtr->oldRr0 != rr0) {
	Write(zPtr->address, 0, WRITE0_RESET_STATUS);
	zPtr->oldRr0 = rr0;
    }
    Write(zPtr->address, 0, WRITE0_CLEAR_INTR);
    MASTER_UNLOCK(&z8530Mutex);
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * Read --
 *
 *	Read a device register for the Z8530.
 *
 * Results:
 *	The return value is the contents of the given register.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
Read(devPtr, regNumber)
    register volatile DevZ8530Device *devPtr;	/* Location of device
						 * registers. */
    int regNumber;				/* Number of register to
						 * read. */
{
    devPtr->control = regNumber;
    MACH_DELAY(4);
    return devPtr->control;
}

/*
 *----------------------------------------------------------------------
 *
 * Write --
 *
 *	Write a device register for the Z8530.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The given value is stored into the given register.
 *
 *----------------------------------------------------------------------
 */

static void
Write(devPtr, regNumber, value)
    register volatile DevZ8530Device *devPtr;	/* Location of device
						 * registers. */
    int regNumber;				/* Number of register to
						 * read. */
    int value;					/* Value to write in
						 * register. */
{
    devPtr->control = regNumber;
    MACH_DELAY(4);
    devPtr->control = value;
}

/*
 *----------------------------------------------------------------------
 *
 * Dummy routines --
 *
 *	These procedures are needed in the interim while the new
 *	tty driver is being brought up (6/89).  Once it's up, the dbg
 *	module should be changed to make these procedures unnecessary.
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
Dev_ZilogInit()
{
}

int
Dev_ZilogReadReg()
{
    return 0;
}

void
Dev_ZilogWriteReg()
{
}
