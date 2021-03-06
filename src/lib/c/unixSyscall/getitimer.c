/* 
 * getitimer.c --
 *
 *	Procedure to map the Unix getitimer and setitimer system calls 
 *	to Sprite.
 *
 * Copyright 1986 Regents of the University of California
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Header: getitimer.c,v 1.1 88/06/19 14:31:24 ouster Exp $ SPRITE (Berkeley)";
#endif not lint

#include "sprite.h"
#include "fs.h"
#include "proc.h"

#include <sys/time.h>

#include "compatInt.h"


/*
 *----------------------------------------------------------------------
 *
 * getitimer --
 *
 *	Procedure for the Unix getitimer call. 
 *
 * Results:
 *	UNIX_SUCCESS if the Sprite return returns SUCCESS.
 *	Otherwise, UNIX_ERROR and errno is set to the Unix equivalent
 *	status.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
getitimer(which, value)
    int which;
    struct itimerval *value;
{
    ReturnStatus status;

    /*
     * The Sprite and Unix timer values have the same layout.
     */
    status = Proc_GetIntervalTimer(which, (Proc_TimerInterval *) value);
    if (status != SUCCESS) {
	errno = Compat_MapCode(status);
	return(UNIX_ERROR);
    } else {
	return(UNIX_SUCCESS);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * setitimer --
 *
 *	Procedure for the Unix setitimer call. 
 *
 * Results:
 *	UNIX_SUCCESS if the Sprite return returns SUCCESS.
 *	Otherwise, UNIX_ERROR and errno is set to the Unix equivalent
 *	status.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
setitimer(which, value, ovalue)
    int which;
    struct itimerval *value;
    struct itimerval *ovalue;
{
    ReturnStatus status;

    /*
     * The Sprite and Unix timer values have the same layout.
     */
    status = Proc_SetIntervalTimer(which, 
		(Proc_TimerInterval *) value,
		(Proc_TimerInterval *) ovalue);
    if (status != SUCCESS) {
	errno = Compat_MapCode(status);
	return(UNIX_ERROR);
    } else {
	return(UNIX_SUCCESS);
    }
}
