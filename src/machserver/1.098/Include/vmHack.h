/*
 * vmHack.h --
 *
 *	Temporary debugging defs to check bstring references to user memory.
 *
 * Copyright 1991 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that this copyright
 * notice appears in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * $Header: /sprite/src/kernel/vm/RCS/vmHack.h,v 1.1 91/03/23 00:12:13 kupfer Exp $ SPRITE (Berkeley)
 */

#ifndef _VMHACK
#define _VMHACK

#include <cfuncproto.h>
#include <sprite.h>

#define VM_CHECK_BSTRING_ACCESS	1

/* 
 * If this is off, don't do the checks.  This is to avoid annoying 
 * other kernel hackers and to avoid deadlock after we panic.
 */
extern Boolean vmDoAccessChecks;

extern void Vm_CheckAccessible _ARGS_ ((Address startAddr,
					int numBytes));
extern void VmMapInit _ARGS_ ((void));

#endif /* _HEADER */
