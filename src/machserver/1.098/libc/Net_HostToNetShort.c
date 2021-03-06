/* 
 * Net_HostToNetShort.c --
 *
 *	Convert a short integer from host to network byte ordering.
 *
 * Copyright 1988 Regents of the University of California
 * All rights reserved.
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/lib/net/RCS/Net_HostToNetShort.c,v 1.2 89/01/27 16:37:18 mendel Exp $ SPRITE (Berkeley)";
#endif not lint


#include "machparam.h"

/* 
 *----------------------------------------------------------------------
 *
 * Net_HostToNetShort --
 *
 *	Convert a short integer in host byte order to an short integer in 
 *	network byte order.
 *
 * Results:
 *	The short integer in network byte order.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned short 
Net_HostToNetShort(shortInt)
	unsigned short shortInt; 	/* A short int in Host byte order. */
{

#if BYTE_ORDER == LITTLE_ENDIAN
	union swab {
		unsigned short s;
		unsigned char  c[2];
	} in, out;

	in.s = shortInt;
	out.c[0] = in.c[1];
	out.c[1] = in.c[0];

        return (out.s);
#else
	return(shortInt);
#endif
}

