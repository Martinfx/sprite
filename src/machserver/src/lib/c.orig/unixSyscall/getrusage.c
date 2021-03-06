/* 
 * getrusage.c --
 *
 *	Procedure to map from Unix getrusage system call to Sprite.
 *
 * Copyright (C) 1986 Regents of the University of California
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Header: getrusage.c,v 1.1 88/06/19 14:31:28 ouster Exp $ SPRITE (Berkeley)";
#endif not lint

#include "sprite.h"
#include "proc.h"

#include <sys/time.h>
#include <sys/resource.h>

#include "compatInt.h"

/*
 * Convert from a Sprite Time to a Unix struct timeval.  The structures
 * really are compatible, but C won't permit a cast from one to the other,
 * so use a macro instead.
 */

#define COPYTIME(TO,FROM) { \
	    (TO).tv_sec = (FROM).seconds; \
	    (TO).tv_usec = (FROM).microseconds; \
	  }


/*
 *----------------------------------------------------------------------
 *
 * getrusage --
 *
 *	Procedure to map from Unix getrusage system call to Sprite
 *	Proc_GetResUsage.  Note that getrusage normally returns more
 *	information than Proc_GetResUsage, so many fields must be set
 *	to zero.
 *
 * Results:
 *	UNIX_ERROR is returned upon error, with the actual error code
 *	stored in errno.  Upon success, UNIX_SUCCESS is returned and the
 *	rusage struct is filled with the available information.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
getrusage(who, rusage)
     int who;
     struct rusage *rusage;
{
    ReturnStatus status;	    /* result returned by Proc_GetResUsage */
    Proc_ResUsage spriteUsage; 	    /* sprite resource usage buffer */

    status = Proc_GetResUsage(PROC_MY_PID, &spriteUsage);
    if (status != SUCCESS) {
	errno = Compat_MapCode(status);
	return(UNIX_ERROR);
    } else {
	if (who == RUSAGE_SELF) {
	    COPYTIME(rusage->ru_utime, spriteUsage.userCpuUsage);
	    COPYTIME(rusage->ru_stime, spriteUsage.kernelCpuUsage);
	} else {
	    COPYTIME(rusage->ru_utime, spriteUsage.childUserCpuUsage);
	    COPYTIME(rusage->ru_stime, spriteUsage.childKernelCpuUsage);
	}
	rusage->ru_maxrss = 0;
	rusage->ru_ixrss = 0;	/* integral shared memory size */
	rusage->ru_idrss = 0;	/* integral unshared data size */
	rusage->ru_isrss = 0;	/* integral unshared stack size */
	rusage->ru_minflt = 0;	/* page reclaims */
	rusage->ru_majflt = 0;	/* page faults */
	rusage->ru_nswap = 0;	/* swaps */
	rusage->ru_inblock = 0;	/* block input operations */
	rusage->ru_oublock = 0;	/* block output operations */
	rusage->ru_msgsnd = 0;	/* messages sent */
	rusage->ru_msgrcv = 0;	/* messages received */
	rusage->ru_nsignals = 0;	/* signals received */
	rusage->ru_nvcsw =
	        spriteUsage.numWaitEvents;  /* voluntary context switches */
	rusage->ru_nivcsw =
	        spriteUsage.numQuantumEnds;  /* involuntary context switches */

	return(UNIX_SUCCESS);
    }
}
