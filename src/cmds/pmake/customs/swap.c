/*-
 * rpc.c --
 *	Remote Procedure Call byte-swapping functions
 *
 * Copyright (c) 1988, 1989 by the Regents of the University of California
 * Copyright (c) 1988, 1989 by Adam de Boor
 * Copyright (c) 1989 by Berkeley Softworks
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any non-commercial purpose
 * and without fee is hereby granted, provided that the above copyright
 * notice appears in all copies.  The University of California,
 * Berkeley Softworks and Adam de Boor make no representations about
 * the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 */
#ifndef lint
static char *rcsid =
"$Id: swap.c,v 1.4 89/11/14 13:46:22 adam Exp $ SPRITE (Berkeley)";
#endif lint

#include    "customsInt.h"

/*-
 *-----------------------------------------------------------------------
 * Swap_Timeval --
 *	Swap the two elements of a struct timeval.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The elements are swapped.
 *
 *-----------------------------------------------------------------------
 */
void
Swap_Timeval (len, data)
    int	    	  	len;
    struct timeval	*data;
{
    Rpc_SwapLong(sizeof(data->tv_sec), &data->tv_sec);
    Rpc_SwapLong(sizeof(data->tv_usec), &data->tv_usec);
}

/*-
 *-----------------------------------------------------------------------
 * Swap_Avail --
 *	Byte-swap all fields of an Avail_Data structure.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Ditto.
 *
 *-----------------------------------------------------------------------
 */
void
Swap_Avail (len, data)
    int	    	  len;
    Avail_Data	  *data;
{
    Rpc_SwapLong(sizeof(data->changeMask), &data->changeMask);
    Rpc_SwapLong(sizeof(data->idleTime), &data->idleTime);
    Rpc_SwapLong(sizeof(data->swapPct), &data->swapPct);
    Rpc_SwapLong(sizeof(data->loadAvg), &data->loadAvg);
    Rpc_SwapLong(sizeof(data->imports), &data->imports);
}

/*-
 *-----------------------------------------------------------------------
 * Swap_Host --
 *	Byte-swap a Host_Data structure.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Ditto.
 *
 *-----------------------------------------------------------------------
 */
void
Swap_Host (len, data)
    int	    	  len;
    Host_Data	  *data;
{
    Rpc_SwapShort(sizeof(data->uid), &data->uid);
    Rpc_SwapShort(sizeof(data->flags), &data->flags);
}

/*-
 *-----------------------------------------------------------------------
 * Swap_ExportPermit --
 *	Byte-swap an ExportPermit structure.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Ditto.
 *
 *-----------------------------------------------------------------------
 */
void
Swap_ExportPermit (len, data)
    int	    	  len;
    ExportPermit  *data;
{
    Rpc_SwapLong(sizeof(data->addr), (long *)&data->addr);
    Rpc_SwapLong(sizeof(data->id), &data->id);
}

/*-
 *-----------------------------------------------------------------------
 * Swap_WayBill --
 *	Swap a WayBill structure and its associated data.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Ditto.
 *
 *-----------------------------------------------------------------------
 */
void
Swap_WayBill (len, data)
    int	    	  len;
    WayBill 	  *data;
{
    register char *cp;
    register int  i;
    register long *lp;

    Rpc_SwapLong(sizeof(data->id), &data->id);
    Rpc_SwapShort(sizeof(data->port), &data->port);
    Rpc_SwapShort(sizeof(data->ruid), &data->ruid);
    Rpc_SwapShort(sizeof(data->euid), &data->euid);
    Rpc_SwapShort(sizeof(data->rgid), &data->rgid);
    Rpc_SwapShort(sizeof(data->egid ), &data->egid);
    Rpc_SwapLong(sizeof(data->umask), &data->umask);
    Rpc_SwapLong(sizeof(data->ngroups), &data->ngroups);
    for (i = data->ngroups, lp = data->groups; i >= 0; i--, lp++) {
	Rpc_SwapLong(sizeof(*lp), lp);
    }
    /*
     * Skip the cwd and the file to execute
     */
    cp = (char *)&data[1];
    cp += strlen(cp) + 1;
    cp += strlen(cp) + 1;
    lp = Customs_Align(cp, long *);
    /*
     * Swap the number of arguments
     */
    Rpc_SwapLong(sizeof(*lp), lp);
    for (cp = (char *)&lp[1], i = *lp; i >= 0; i--) {
	cp += strlen(cp) + 1;
    }

    /*
     * Swap the number of environment variables
     */
    lp = Customs_Align(cp, long *);
    Rpc_SwapLong(sizeof(*lp), lp);
}

/*-
 *-----------------------------------------------------------------------
 * Swap_Kill --
 *	Byte-swap a Kill_Data structure.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Ditto.
 *
 *-----------------------------------------------------------------------
 */
void
Swap_Kill (len, data)
    int	    	  len;
    Kill_Data	  *data;
{
    Rpc_SwapLong(sizeof(data->id), &data->id);
    Rpc_SwapLong(sizeof(data->signo), &data->signo);
}

/*-
 *-----------------------------------------------------------------------
 * Swap_RegPacket --
 *
 * Results:
 *
 * Side Effects:
 *
 *-----------------------------------------------------------------------
 */
void
Swap_RegPacket (len, data)
    int	    	  len;
    char    	  *data;
{
    register long *lp;

    /*
     * Skip hostname
     */
    data += strlen(data)+1;
    lp = Customs_Align(data, long *);

    Rpc_SwapLong(sizeof(*lp), lp);  /* Machine architecture */
    lp++;
    Rpc_SwapLong(sizeof(*lp), lp);  /* Number of clients */
}

/*-
 *-----------------------------------------------------------------------
 * Swap_Info --
 *
 * Results:
 *
 * Side Effects:
 *
 *-----------------------------------------------------------------------
 */
void
Swap_Info (len, data)
    int	    	  len;
    register long *data;
{
    register int  i, j;

    /*
     * Get number of hosts in i first, then swap it.
     */
    i = *data;
    Rpc_SwapLong(sizeof(*data), data);

    data++;

    while (i > 0) {
	data += strlen((char *)data) + 1;
	data = Customs_Align(data, long *);
	Rpc_SwapLong(sizeof(*data), data);  	/* Availability */
	data++;
	Rpc_SwapLong(sizeof(*data), data);  	/* Availability rating */
	data++;
	Rpc_SwapLong(sizeof(*data), data);  	/* Architecture */
	data++;
	j = *data;
	Rpc_SwapLong(sizeof(*data), data);  	/* Number of clients */
	data++;

	while (j > 0) {
	    Rpc_SwapLong(sizeof(*data), data);
	    data++;
	    j--;
	}
	i--;
    }
    Rpc_SwapLong(sizeof(*data), data);	/* Last Allocated */
}

/*-
 *-----------------------------------------------------------------------
 * Swap_AvailInt --
 *	Byte-swap an internal Avail packet.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Ditto.
 *
 *-----------------------------------------------------------------------
 */
void
Swap_AvailInt (len, data)
    int	    	  len;
    Avail   	  *data;
{
    Rpc_SwapLong(sizeof(data->addr), &data->addr);
    Swap_Timeval(sizeof(data->interval), &data->interval);
    Rpc_SwapLong(sizeof(data->avail), &data->avail);
    Rpc_SwapLong(sizeof(data->rating), &data->rating);
}
