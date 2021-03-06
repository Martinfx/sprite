/* 
 * abs.c --
 *
 *	Contains the source code for the "abs" library procedure.
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
static char rcsid[] = "$Header: /sprite/src/lib/c/stdlib/RCS/abs.c,v 1.2 89/03/22 00:46:50 rab Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include <stdlib.h>

/*
 *----------------------------------------------------------------------
 *
 * abs --
 *
 *	Compute the absolute value of an integer.
 *
 * Results:
 *	The return value is j, unless j is negative, in which case
 *	the return value is -j.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
abs(j)
    int j;			/* Value to take the absolute value of. */
{
    if (j >= 0) {
	return j;
    }
    return -j;
}
