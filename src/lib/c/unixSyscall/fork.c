/* 
 * fork.c --
 *
 *	Procedure to map from Unix fork system call to Sprite.
 *
 * Copyright (C) 1986 Regents of the University of California
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Header: fork.c,v 1.2 88/07/29 17:39:25 ouster Exp $ SPRITE (Berkeley)";
#endif not lint

#include "sprite.h"
#include "status.h"
#include "proc.h"

#include "compatInt.h"


/*
 *----------------------------------------------------------------------
 *
 * fork --
 *
 *	Procedure to map from Unix fork system call to Sprite Proc_Fork.
 *
 * Results:
 *	UNIX_ERROR is returned upon error, with the actual error code
 *	stored in errno.  Upon success, the value of pid is returned, 
 *	where pid is different for child and parent.  The parent receives
 *	the process id of the child in pid, and the child receives the
 *	value 0.
 *
 * Side effects:
 *	A new process is created.
 *
 *----------------------------------------------------------------------
 */

int
fork()
{
    ReturnStatus status;  /* result returned by Proc_Fork */
    int pid;		  /* process id of child, or 0, set by Proc_Fork */

    /* Fork without sharing the heap. */
    status = Proc_Fork(FALSE, &pid);
    if (status == PROC_CHILD_PROC) {
	return(0);
    }
    if (status != SUCCESS) {
	errno = Compat_MapCode(status);
	return(UNIX_ERROR);
    }
    return((int) pid);
}
