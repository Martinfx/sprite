/*
 * devSCSITape.h --
 *
 *	Declarations of device switch routines for SCSI Tape drives.
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
 * $Header: /cdrom/src/kernel/Cvsroot/kernel/dev/devSCSITape.h,v 9.2 91/08/19 13:44:35 jhh Exp $ SPRITE (Berkeley)
 */

#ifndef _DEVSCSITAPE
#define _DEVSCSITAPE

/*
 * Flags encoded in the unit number of the device.
 */

#define DEV_SCSI_TAPE_NO_REWIND	0x1	/* No rewind on close. */
#define DEV_SCSI_TAPE_VAR_BLOCK	0x2	/* Variable block size. */

extern ReturnStatus DevSCSITapeOpen _ARGS_((Fs_Device *devicePtr, int useFlags,
    Fs_NotifyToken token, int *flagsPtr));
extern ReturnStatus DevSCSITapeRead _ARGS_((Fs_Device *devicePtr,
    Fs_IOParam *readPtr, Fs_IOReply *replyPtr));
extern ReturnStatus DevSCSITapeWrite _ARGS_((Fs_Device *devicePtr,
    Fs_IOParam *writePtr, Fs_IOReply *replyPtr));
extern ReturnStatus DevSCSITapeIOControl _ARGS_((Fs_Device *devicePtr,
    Fs_IOCParam *ioctlPtr, Fs_IOReply *replyPtr));
extern ReturnStatus DevSCSITapeClose _ARGS_((Fs_Device *devicePtr,
    int useFlags, int openCount, int writerCount));

#endif /* _DEVSCSITAPE */

