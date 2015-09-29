/* 
 * rpcDelay.c --
 *
 *	Get preferred machine dependent inter-packet delays for rpcs.
 *
 * Copyright 1988 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /r3/kupfer/spriteserver/src/sprited/rpc/ds3100.md/RCS/rpcDelays.c,v 1.2 91/11/14 10:14:42 kupfer Exp $ SPRITE (Berkeley)";
#endif not lint


#include <sprite.h>
#include <sys.h>


/*
 *----------------------------------------------------------------------
 *
 * RpcGetMachineDelay --
 *
 *	Get preferred inter-fragment delay for input and output packets.
 *	This is approximately a microsecond value for how long this
 *	machine-type takes to turn a packet around.
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
RpcGetMachineDelay(myDelayPtr, outputRatePtr)
	unsigned short	*myDelayPtr;	/* Preferred delay in microseconds
					 * between successive input packets.
					 */
	unsigned short	*outputRatePtr;	/* Delay in microseconds between
					 * successive output packets.
					 */
{
    *myDelayPtr = 500;			/* Same as sun3 value */
    *outputRatePtr = 500;
    return;
}

