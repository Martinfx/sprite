/* 
 * Time_Multiply.c --
 *
 *	Source code for the Time_Multiply library procedure.
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
static char rcsid[] = "$Header: Time_Multiply.c,v 1.2 88/06/27 17:23:33 ouster Exp $ SPRITE (Berkeley)";
#endif not lint

#include <sprite.h>
#include <spriteTime.h>

/*
 * OVERFLOW is used to see if multiple precision multiplication
 * and division are required in the Time_Multiply  and Time_Divide routines.
 * The number is equal to (2 ** 31 - 1) / 1000000.
 */

#define OVERFLOW 2147

/*
 *----------------------------------------------------------------------
 *
 * Time_Multiply --
 *
 *      Computes a multiple of a time value.
 *	E.g. computes a time of 7 seconds given the 
 *	constant time_OneSecond and a factor of 7.
 *
 * Results:
 *     A time.
 *
 * Side effects:
 *     None.
 *
 *----------------------------------------------------------------------
 */

void
Time_Multiply(time, factor, resultPtr)
    Time time;
    int	 factor;
    Time *resultPtr;
{
    register int	micro;		/* partial result */
    Boolean	 	normalize;	/* true if normalization necessary. */

    /*
     * Since floating point operations are expensive, first check if the 
     * calculation can be done using cheaper integer multiplication. 
     * If there is a possibility of an overflow, then resort to floating point.
     *
     * The test for overflow used below only tests the size of the multiplier.
     * To be fair, it should test the size of time.microseconds too
     * because overflow will not occur if time.microseconds is small.
     * Overflow occurs when numBits(factor) + numBits(time.microseconds) > 31
     * Use the easy test now because at some point in time, this section 
     * should be replaced with double precision integer routines.
     *
     */

    normalize = FALSE;

    if (factor > OVERFLOW) {

	double realProd;

	realProd = ((double) time.seconds +
			((double) time.microseconds / (double) 1000000.0)) *
			(double) factor;
	resultPtr->seconds	= realProd;
	resultPtr->microseconds	= ((realProd - ((double) resultPtr->seconds)) * 
				     1000000.0);

	if (resultPtr->microseconds < 0) {
	    normalize = TRUE;
	}
    } else {

	/*
	 *  The microseconds portion is normalized such that it is < 1,000,000.
	 */
	micro			= time.microseconds * factor;
	resultPtr->seconds	= (time.seconds * factor) + (micro/ ONE_SECOND);
	resultPtr->microseconds = micro % ONE_SECOND;

	if (factor < 0) {
	    normalize = TRUE;
	} 
    }

    /*
     * Convert to normalized form.
     *
     */
    if (normalize) {
	resultPtr->seconds	-= 1;
	resultPtr->microseconds	+= ONE_SECOND;
    } 
}
