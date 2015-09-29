/*
 * tty.h --
 *
 *	This file contains declarations of the facilities provided by
 *	devTty.c for the rest of the dev module.  This consists of glue
 *	that holds together a generic terminal driver (ttyDriver), a
 *	device-specific interface to a serial line, and the generic
 *	Sprite kernel-call procedures for terminal devices.
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
 * $Header: /user5/kupfer/spriteserver/src/sprited/dev/RCS/tty.h,v 1.3 92/04/02 21:17:24 kupfer Exp $ SPRITE (Berkeley)
 */

#ifndef _DEVTTY
#define _DEVTTY

#include <sprite.h>
#include <td.h>
#include <sync.h>
#include <fs.h>

/*
 * For each terminal or terminal-like device supported by the kernel,
 * there exists a record with the following structure.  The structure
 * (and the code in devTty.c) is complicated in native Sprite by the fact 
 * that the Td library calls malloc, which means that some of its
 * procedures can't be invoked by interrupt handlers (or with interrupts
 * disabled).  To get around this problem, devTty.c introduces an extra
 * level of buffering between the interrupt handler and Td.  The Td
 * structures are locked using a sleep lock, and are ONLY accessed from
 * process level.  The extra buffers are handled without locks by using
 * pointers carefully.  Transfers between the Td buffers and the DevTty
 * buffers are made only by procedures running at process level.
 * 
 * None of this hair is probably necessary for the Sprite server, but it's 
 * probably not worth taking out right now.
 */

#define TTY_IN_BUF_SIZE 100
#define TTY_OUT_BUF_SIZE 100

typedef struct DevTty {
    /*
     * Intermediate buffers for terminal.  Note that the input buffer
     * contains int's, not chars, in order to allow for the special
     * non-ASCII values defined below, such as DEV_TTY_BREAK.  Both buffers
     * use circular pointers.
     */

    volatile int inBuffer[TTY_IN_BUF_SIZE];
    volatile int insertInput;		/* Index at which to place next value
					 * in inBuffer. */
    volatile int extractInput;		/* Index at which to remove next value
					 * from inBuffer.  If insert ==
					 * extract, buffer is empty. */
    volatile unsigned char outBuffer[TTY_OUT_BUF_SIZE];
    volatile int insertOutput;		/* Index at which to place next byte
					 * in outBuffer. */
    volatile int extractOutput;		/* Index at which to remove next byte
					 * from outputBuffer.  If insert ==
					 * extract, buffer is empty. */

    /*
     * Information used to communicate with the device-specific driver and
     * other device-specific parameterization.  This information is filled
     * in by the machine-specific initialization procedure DevTtyAttach.
     */

    int (*rawProc) _ARGS_ ((void *clientDataPtr, int operation, int inBufSize,
	char *inBuffer, int outBufSize, char *outBuffer));
                                        /* Used as "rawProc" for the
					 * terminal by Td library. */
    void (*activateProc) _ARGS_((void *clientDataPtr));
                                        /* Called to activate terminal
					 * (initialize, enable interrupts,
					 * etc.) after initialization is
					 * complete. */
    ClientData rawData;			/* Arbitrary value associated with
					 * the device driver;  passed to
					 * rawProc by Td library. */
    void (*inputProc) _ARGS_ ((ClientData data, int value));
                                        /* For most terminal-like devices
					 * this is NIL.  If non-NIL, it
					 * is a procedure to invoke to process
					 * each input character (e.g. to map
					 * keystroke identifiers to ASCII
					 * characters). */
    ClientData inputData;		/* Arbitrary value to pass to
					 * inputProc. */
    int consoleFlags;			/* Used to manage console device;
					 * see below for definitions. */

    /*
     * Information about the terminal and how to hook its cooked side up
     * to Sprite kernel calls.
     */

    Td_Terminal term;			/* Token returned by Td_Create. */
    int selectState;			/* Current select state for terminal:
					 * OR'ed combination of FS_READABLE,
					 * FS_WRITABLE, and FS_EXCEPTION. */
    Fs_NotifyToken notifyToken;		/* Token identifying device;  used to
					 * notify Fs when terminal becomes
					 * readable or writable. */
    int openCount;			/* Number of active opens of
					 * terminal. */
} DevTty;

/*
 * Definitions for consoleFlags bits:
 *
 * DEV_TTY_IS_CONSOLE:	1 means this terminal is the console, so handle
 *			special console commands like L1-A.
 * DEV_TTY_GOT_BREAK:	For consoles that are just serial lines, this bit
 *			means a break was just received on input, so the
 *			next character is a console command.
 * DEV_TTY_OVERFLOWED:	The input buffer overflowed and a message has been
 *			printed.  
 */

#define DEV_TTY_IS_CONSOLE	1
#define DEV_TTY_GOT_BREAK	2
#define DEV_TTY_OVERFLOWED	4

/*
 * Special values for "characters" placed in the buffer of a DevTty:
 *
 * DEV_TTY_BREAK:	A break condition just occurred on the device.
 * DEV_TTY_HANGUP:	The terminal just got hung up.
 */

#define DEV_TTY_BREAK	1000
#define DEV_TTY_HANGUP	1001

#define DEV_TTY_IS_CONTROL(x) ((x) & ~0xff)

/*
 * Variables exported by devTty.c:
 */

extern Sync_Lock	devTtyLock;

/*
 * Procedures exported by devTty.c:
 */

extern ReturnStatus DevTtyClose _ARGS_((Fs_Device *devicePtr, int useFlags,
    int openCount, int writerCount));
extern void DevTtyInputChar _ARGS_((DevTty *ttyPtr, int value));
extern int DevTtyOutputChar _ARGS_((DevTty *ttyPtr));
extern ReturnStatus DevTtyIOControl _ARGS_((Fs_Device *devicePtr,
    Fs_IOCParam *iocPtr, Fs_IOReply *replyPtr));
extern ReturnStatus DevTtyOpen _ARGS_((Fs_Device *devicePtr, int useFlags,
    Fs_NotifyToken notifyToken, int *flagsPtr));
extern ReturnStatus DevTtyRead _ARGS_((Fs_Device *devicePtr,
    Fs_IOParam *readPtr, Fs_IOReply *replyPtr));
extern ReturnStatus DevTtyWrite _ARGS_((Fs_Device *devicePtr,
    Fs_IOParam *writePtr, Fs_IOReply *replyPtr));
extern ReturnStatus DevTtySelect _ARGS_((Fs_Device *devicePtr, int *readPtr,
    int *writePtr, int *exceptPtr));

#endif /* _DEVTTY */
