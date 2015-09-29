/*
 * exabyteTape.h
 *
 * Definitions for attach procedure for Exabyte tape drives.  
 *
 * Copyright 1986 Regents of the University of California
 * All rights reserved.
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 *
 * $Header: /cdrom/src/kernel/Cvsroot/kernel/dev/exabyteTape.h,v 9.1 91/06/06 10:14:39 mendel Exp $ SPRITE (Berkeley)
 */

#ifndef _DEVEXABYTE
#define _DEVEXABYTE


extern ReturnStatus DevExabyteAttach _ARGS_ ((Fs_Device *devicePtr,
    ScsiDevice *devPtr, ScsiTape *tapePtr));

#endif /* _DEVSCSIEXABYTE */
