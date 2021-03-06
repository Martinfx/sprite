/* 
 * Net_StringToEtherAddr.c --
 *
 *	Convert a string to an ethernet address.
 *
 * Copyright 1987 Regents of the University of California
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
static char rcsid[] = "$Header: /sprite/src/lib/net/RCS/Net_StringToEtherAddr.c,v 1.2 88/11/21 09:28:33 mendel Exp $ SPRITE (Berkeley)";
#endif not lint


#include "sprite.h"
#include "net.h"

/*
 *----------------------------------------------------------------------
 *
 * Net_StringToEtherAddr --
 *
 *	This routine takes a string form of an Ethernet address and
 *	converts it to the Net_EtherAddress form. The string must be
 *	null-terminated.
 *
 * Results:
 *	The Ethernet address in the Net_EtherAddress form.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Net_StringToEtherAddr(buffer, etherAddressPtr)
    register char *buffer;
    Net_EtherAddress *etherAddressPtr;
{
    unsigned int byte[6];

    if (sscanf(buffer, "%2x:%2x:%2x:%2x:%2x:%2x",
	    &byte[0], &byte[1], &byte[2], &byte[3], &byte[4], &byte[5]) != 6) {
	bzero((Address)etherAddressPtr, sizeof(Net_EtherAddress) );
    } else {
	NET_ETHER_ADDR_BYTE1(*etherAddressPtr) = byte[0];
	NET_ETHER_ADDR_BYTE2(*etherAddressPtr) = byte[1];
	NET_ETHER_ADDR_BYTE3(*etherAddressPtr) = byte[2];
	NET_ETHER_ADDR_BYTE4(*etherAddressPtr) = byte[3];
	NET_ETHER_ADDR_BYTE5(*etherAddressPtr) = byte[4];
	NET_ETHER_ADDR_BYTE6(*etherAddressPtr) = byte[5];
    }
}

