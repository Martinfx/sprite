/* 
 * Host_Stat.c --
 *
 *	Does a "stat" on the host database and returns the stat structure.
 *
 * Copyright 1990 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/lib/c/host/RCS/Host_Stat.c,v 1.1 90/10/31 22:35:06 jhh Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include <stdio.h>
#include <host.h>
#include <hostInt.h>
#include <sys/types.h>
#include <sys/stat.h>


/*
 *----------------------------------------------------------------------
 *
 * Host_Stat --
 *
 *	Stat the host database.
 *
 * Results:
 *	0 if successful, -1 otherwise and errno is set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Host_Stat(statPtr)
    struct stat	*statPtr;	/* Place to store stat results. */
{
    return stat(hostFileName, statPtr);
}

