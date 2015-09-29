/*
 * sema.h --
 *
 * Copyright 1990 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * $Header: /cdrom/src/kernel/Cvsroot/kernel/raid/semaphore.h,v 1.2 90/11/09 13:49:46 eklee Exp $ SPRITE (Berkeley)
 */

#ifndef _SEMA
#define _SEMA

#include "sync.h"
#include "syncLock.h"

typedef struct {
    Sync_Semaphore	mutex;
    int			val;
    Sync_Condition	wait;
} Sema;

#define LockSema(sema) (DownSema(sema))
#define UnlockSema(sema) (UpSema(sema))

extern void InitSema _ARGS_((Sema *semaPtr, char *name, int val));
extern void DownSema _ARGS_((Sema *semaPtr));
extern void UpSema _ARGS_((Sema *semaPtr));

#endif /* _SEMA */
