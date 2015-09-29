/* 
 * devScsiTapeConfig.c --
 *
 *	Configuration file for SCSI tape drives. This file initializes the
 *	data structure containing the list of available SCSI Tape drive 
 *	drivers available in the system.
 *
 * Copyright 1989 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/kernel/dev/sun4c.md/RCS/devScsiTapeConfig.c,v 1.2 91/06/06 11:19:23 mendel Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include "sprite.h"
#include  "scsiTape.h"
#include "exabyteTape.h"
#include "emulexTape.h"

ReturnStatus ((*devSCSITapeAttachProcs[])()) = {
    DevExabyteAttach,
    DevEmulexAttach,
 };

int devNumSCSITapeTypes = sizeof(devSCSITapeAttachProcs) / 
				sizeof(devSCSITapeAttachProcs[0]);

