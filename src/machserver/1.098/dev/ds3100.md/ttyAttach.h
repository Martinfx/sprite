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
 * $Header: /sprite/src/kernel/dev/ds3100.md/RCS/ttyAttach.h,v 1.1 89/07/26 20:03:02 nelson Exp $ SPRITE (Berkeley)
 */

#ifndef _DEVTTYATTACH
#define _DEVTTYATTACH

#include "tty.h"

extern DevTty *		DevTtyAttach();
extern void		DevTtyInit();

#endif /* _DEVTTYATTACH */
