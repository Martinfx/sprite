/*
 * mouse.h --
 *
 *	Declarations for things exported by devMouse.c to the rest
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
 * $Header: /sprite/src/kernel/dev/sun3.md/RCS/mouse.h,v 9.1 90/10/05 18:11:11 mendel Exp $ SPRITE (Berkeley)
 */

#ifndef _DEVMOUSE
#define _DEVMOUSE

extern ReturnStatus	DevMouseClose _ARGS_((Fs_Device *devicePtr,
    int useFlags, int openCount, int writerCount));
extern void		DevMouseInit _ARGS_((void));
extern void		DevMouseInterrupt _ARGS_((void));
extern ReturnStatus	DevMouseIOControl _ARGS_((Fs_Device *devicePtr,
    Fs_IOCParam *ioctlPtr, Fs_IOReply *replyPtr));
extern ReturnStatus	DevMouseOpen _ARGS_((Fs_Device *devicePtr,
    int useFlags, Fs_NotifyToken token, int *flagsPtr));
extern ReturnStatus	DevMouseRead _ARGS_((Fs_Device *devicePtr,
    Fs_IOParam *readPtr, Fs_IOReply *replyPtr));
extern ReturnStatus	DevMouseSelect _ARGS_((Fs_Device *devicePtr, 
    int *readPtr, int *writePtr, int *exceptPtr));
extern ReturnStatus	DevMouseWrite _ARGS_((Fs_Device *devicePtr,
    Fs_IOParam *writePtr, Fs_IOReply *replyPtr));

#endif /* _DEVMOUSE */
