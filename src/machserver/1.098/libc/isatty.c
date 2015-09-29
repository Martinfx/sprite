/* 
 * isatty.c --
 *
 *	Procedure to map from Unix isatty system call to Sprite.
 *
 * Copyright 1986 Regents of the University of California
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/kernel/libc/RCS/isatty.c,v 1.2 90/09/11 12:49:01 kupfer Exp $ SPRITE (Berkeley)";
#endif not lint

#include <sgtty.h>
#include <stdio.h>
#include <unistd.h>

/*
 *----------------------------------------------------------------------
 *
 * isatty --
 *
 *	This function is currently unsupped from the kernel.
 *	Returns non-zero if the given file descriptor refers to a device
 *	with terminal-like characteristics.
 *
 * Results:
 *	Non-zero means fd has terminal-like behavior, zero means it
 *	doesn't.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
isatty(fd)
    int fd;                             /* stream identifier */
{
#if 0 
   struct sgttyb sgttyb;
#endif

    printf("isatty not supported in kernel\n");
    return 0;
#if 0
    if (ioctl(fd, TIOCGETP, (char *) &sgttyb) == -1) {
	return 0;
    }
    return 1;
#endif
}
