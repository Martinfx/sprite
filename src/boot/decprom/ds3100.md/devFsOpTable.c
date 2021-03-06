/* 
 * devFsOpTable.c --
 *
 *	The operation tables for the file system devices.  
 *
 * Copyright 1987, 1988 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifdef notdef
static char rcsid[] = "$Header: /sprite/src/boot/decprom/ds3100.md/RCS/devFsOpTable.c,v 1.1 90/02/16 16:19:06 shirriff Exp $ SPRITE (Berkeley)";
#endif not lint


#include "sprite.h"
#include <kernel/dev.h>
#include <kernel/devFsOpTable.h>

extern ReturnStatus DecPromDevOpen();
extern ReturnStatus DecPromDevRead();
static ReturnStatus NullProc();


/*
 * Device type specific routine table:
 *	This is for the file-like operations as they apply to devices.
 *	DeviceOpen
 *	DeviceRead
 *	DeviceWrite
 *	DeviceIOControl
 *	DeviceClose
 *	DeviceSelect
 *	BlockDeviceAttach
 */


DevFsTypeOps devFsOpTable[] = {
    /*
     * Simple interface to the routines in the Dec PROM.
     */
    {0, DecPromDevOpen, DecPromDevRead,
		    NullProc, NullProc, 
		    NullProc, NullProc, NullProc},

};

int devNumDevices = sizeof(devFsOpTable) / sizeof(DevFsTypeOps);


static ReturnStatus
NullProc()
{
    return(SUCCESS);
}

