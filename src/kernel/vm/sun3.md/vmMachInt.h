/*
 * vmMachInt.h
 *
 *     	Internal machine dependent virtual memory data structures and procedure 
 *	headers.
 *
 * Copyright (C) 1985 Regents of the University of California
 * All rights reserved.
 *
 *
 * $Header: /cdrom/src/kernel/Cvsroot/kernel/vm/sun3.md/vmMachInt.h,v 9.6 91/10/18 01:13:58 dlong Exp $ SPRITE (Berkeley)
 */

#include "vmMach.h"
#ifndef _VMMACHINT
#define _VMMACHINT

extern	Address		vmMachPTESegAddr;
extern	Address		vmMachPMEGSegAddr;

/*
 * Machine-dependent routines internal to the vm module.
 */
extern VmMachPTE VmMachGetPageMap _ARGS_((Address virtualAddress));
extern void VmMachSetPageMap _ARGS_((Address virtualAddress, VmMachPTE pte));
extern int VmMachGetSegMap _ARGS_((Address virtualAddres));
extern void VmMachSetSegMap _ARGS_((Address virtualAddress, int value));
extern VmMachPTE VmMachReadPTE _ARGS_((int pmegNum, Address addr));
extern void VmMachWritePTE _ARGS_((int pmegNum, Address addr, VmMachPTE pte));
extern int VmMachGetUserContext _ARGS_((void));
extern int VmMachGetKernelContext _ARGS_((void));
extern int VmMachGetContextReg _ARGS_((void));
extern void VmMachSetUserContext _ARGS_((int value));
extern void VmMachSetKernelContext _ARGS_((int value));
extern void VmMachSetContextReg _ARGS_((int value));
extern void VmMachPMEGZero _ARGS_((int pmeg));
extern void VmMachClearCacheTags _ARGS_((void));
extern void VmMachInitAddrErrorControlReg _ARGS_((void));
extern void VmMachInitSystemEnableReg _ARGS_((void));
#ifdef sun4
extern void VmMachFlushCurrentContext _ARGS_((void));
extern ReturnStatus VmMachTouchPages _ARGS_((int firstPage, int numPages));
#endif
extern void VmMachFlushSegment _ARGS_((Address segVirtAddr));
extern void VmMachReadAndZeroPMEG _ARGS_((int pmeg, VmMachPTE pteArray[]));
extern void VmMachSetSegMap _ARGS_((Address virtualAddress, int value));
extern void VmMachCopyUserSegMap _ARGS_((unsigned short *tablePtr));
extern void VmMachFlushByteRange _ARGS_((Address virtAddr, int numBytes));
extern void VmMachFlushPage _ARGS_((Address pageVirtAddr));
extern void VmMachSetup32BitDVMA _ARGS_((void));
extern ReturnStatus VmMachQuickNDirtyCopy _ARGS_((register int numBytes,
	Address sourcePtr, Address destPtr, unsigned int sourceContext,
	unsigned int destContext));
extern ReturnStatus VmMachDoCopy _ARGS_((register int numBytes,
	Address sourcePtr, Address destPtr));
extern void VmMachSegMapCopy _ARGS_((char *tablePtr, int startAddr,
	int endAddr));

#endif /* _VMMACHINT */
