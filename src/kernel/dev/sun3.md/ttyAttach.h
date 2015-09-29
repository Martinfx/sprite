/*
 * ttyAttach.h --
 *
 *	Declarations for things exported by devTtyAttach.c to the rest
 *	of the device module.
 *
 * Copyright 1989 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * $Header: /cdrom/src/kernel/Cvsroot/kernel/dev/sun3.md/ttyAttach.h,v 9.1 91/11/04 14:09:03 mgbaker Exp $ SPRITE (Berkeley)
 */

#ifndef _DEVTTYATTACH
#define _DEVTTYATTACH

#ifndef _DEVTTY
#include "tty.h"
#endif
#ifndef _DEVZ8530
#include "z8530.h"
#endif

extern DevZ8530 *DevGrabKeyboard _ARGS_((void (*inputProc)(), ClientData inputData, int (*outputProc)(), ClientData outputData));
extern void DevReleaseKeyboard _ARGS_((void));
extern DevTty *DevTtyAttach _ARGS_((int unit));
extern void DevTtyInit _ARGS_((void));

#endif /* _DEVTTYATTACH */
