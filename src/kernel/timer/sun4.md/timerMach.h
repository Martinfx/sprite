/*
 * timerMach.h --
 *
 *	Machine dependent declarations for the timer module.
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
 * $Header: /cdrom/src/kernel/Cvsroot/kernel/timer/sun4.md/timerMach.h,v 1.1 90/09/04 23:29:56 jhh Exp $ SPRITE (Berkeley)
 */

#ifndef _TIMERMACH
#define _TIMERMACH

/*
 * The timer callback interval, in microseconds.  This is an approximate
 * value for those machines whose interval is not a whole number of 
 * microseconds.
 */

#define TIMER_CALLBACK_INTERVAL_APPROX	20000

#endif /* _TIMERMACH */

