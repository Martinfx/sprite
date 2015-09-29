/*
 * bootSysAsm.s -
 *
 *     Contains code that is the first executed at boot time.
 *
 * Copyright (C) 1985 Regents of the University of California
 * All rights reserved.
 */

.seg	"data"
.asciz "$Header: /cdrom/src/kernel/Cvsroot/kernel/mach/sun4.md/bootSysAsm.s,v 9.4 92/08/10 17:58:47 mgbaker Exp $ SPRITE (Berkeley)"
.align 8
.seg	"text"

#include "machConst.h"
#include "machAsmDefs.h"

/*
 * "Start" is used for the -e option to the loader.  "SpriteStart" is
 * used for the prof module, which prepends an underscore to the name of
 * global variables and therefore can't find "_start".
 *
 * I use a lot global registers here for start up.  Elsewhere I'm careful.
 */

.align 8
.seg	"text"
.globl	start
.globl	_spriteStart
start:
_spriteStart:
       /* 
        * Believe it or not, we're running in the middle of our stack. 
	* We are placed here by the bootstrap loading which starts us
	* running at 0x4000. We quickly jump out of the stack.
	*/
	ba	realStart
	nop
.skip	(MACH_KERN_STACK_SIZE-0x4000)
realStart:
#ifdef sun4c
	mov	%o0, %g7			/* save romvec pointer */
#endif
	mov	%psr, %g1
	or	%g1, MACH_DISABLE_INTR, %g1	/* lock out interrupts */
	andn	%g1, MACH_CWP_BITS, %g1		/* set cwp to 0 */
	set	MACH_ENABLE_FPP, %g2
	andn	%g1, %g2, %g1			/* disable fp unit */
	mov	%g1, %psr
	mov	0x2, %wim	/* set wim to window right behind us */

	/*
	 * The kernel has been loaded into the wrong location.
	 * We copy it to the right location by copying up 8 Meg worth of pmegs.
	 * NOT done in all contexts!  8 Meg should be enough for the whole
	 * kernel.  We copy to the correct address, MACH_KERN_START which is
	 * before MACH_CODE_START, which is where we told the linker that the
	 * kernel would be loaded.  In this code, %g1 is the destination
	 * segment, %g2 is the last source segment, %g3 is the source segment,
	 * and %g5 contains seg size.  %g6 is used to hold the pmeg pointer.
	 */
	set	VMMACH_SEG_SIZE, %g5		/* for additions */
#ifdef sun4c
	set	0x400000, %g2			/* last source addr */
#else
	set	0x800000, %g2			/* last source addr */
#endif
	clr	%g3				/* start with 0th segment */
	set	MACH_KERN_START, %g1		/* pick starting segment */
loopStart:
#ifdef sun4c
	lduba	[%g3] VMMACH_SEG_MAP_SPACE, %g6
#else
	lduha	[%g3] VMMACH_SEG_MAP_SPACE, %g6
#endif
					/* set segment to point to new pmeg */
#ifdef sun4c
	stba	%g6, [%g1] VMMACH_SEG_MAP_SPACE
#else
	stha	%g6, [%g1] VMMACH_SEG_MAP_SPACE
#endif
	add	%g3, %g5, %g3			/* increment source segment */
	add	%g1, %g5, %g1			/* increment dest segment */
	cmp	%g3, %g2			/* last segment? */
	bne	loopStart			/* if not, continue */
	nop

/*
 * Force a non-PC-relative jump to the real start of the kernel.
 */
	set	begin, %g1
	jmp	%g1				/* jump to "begin" */
	nop
begin:
	/*
	 * Zero out the bss segment.
	 */
	set	_edata, %g2
	set	_end, %g3
	cmp	%g2, %g3	/* if _edata == _end, don't zero stuff. */
	be	doneZeroing
	nop
	clr	%g1
zeroing:
	/*
	 * Use store doubles for speed.  Both %g0 and %g1 are zeroes.
	 */
	std	%g0, [%g2]
	add	%g2, 0x8, %g2
	cmp	%g2, %g3
	bne	zeroing
	nop
doneZeroing:
	/*
	 * Find out how many register windows we have.
	 */
	mov	%g0, %wim
	save
	mov	%psr, %g1
	restore
	mov	0x2, %wim	/* set wim to window right behind us */
	and	%g1, MACH_CWP_BITS, %g1
	sethi	%hi(_machWimShift), %g2
	st	%g1, [%g2 + %lo(_machWimShift)]
	add	%g1, 1, %g1
	sethi	%hi(_machNumWindows), %g2
	st	%g1, [%g2 + %lo(_machNumWindows)]
	/*
	 * Now set the stack pointer to my own stack for the first kernel
	 * process.  The stack grows towards low memory.  I start it at
	 * the beginning of the text segment (CAREFUL: if loading demand-paged,
	 * then the beginning of the text segment is 32 bytes before the
	 * first code.  Set it really at the beginning of the text segment and
	 * not at the beginning of the code.), and it can grow up to
	 * MACH_KERN_START.
	 *
	 * The %fp points to top word on stack of one's caller, so it points
	 * to the base of our stack.  %sp points to the top word on the
	 * stack for our current stack frame.   This must be set at least
	 * to enough room to save our in registers and local registers upon
	 * window overflow (and for main to store it's arguments, although it
	 * doesn't have any...).
	 */
	set	MACH_STACK_START, %fp
	set	(MACH_STACK_START - MACH_FULL_STACK_FRAME), %sp
	andn	%sp, 0x7, %sp			/* double-word aligned */

	/*
	 * Now set up initial trap table by copying machProtoVectorTable
	 * into reserved space at the correct alignment.  The table must
	 * be aligned on a MACH_TRAP_ADDR_MASK boundary, and it contains
	 * ~MACH_TRAP_ADDR_MASK + 1 bytes.  We copy doubles (8 bytes at
	 * a time) for speed.  %g1 is source for copy, %g2 is destination,
	 * %g3 is the counter copy, %g4 and %g5 are the registers used for
	 * the double-word copy, %l1 is for holding the size of the table,
	 * and %l2 contains the number of bytes to copy.  %g6 stores the
	 * original destination, so that we can do some further copies, and
	 * so that we can put it into the tbr..
	 */
	set	machProtoVectorTable, %g1		/* g1 contains src */
	set	reserveSpace, %g2			/* g2 to contain dest */
	set	(1 + ~MACH_TRAP_ADDR_MASK), %l1
	set	((1 + ~MACH_TRAP_ADDR_MASK) / 8), %l2	/* # bytes to copy */
	add	%g2, %l1, %g2				/* add size of table */
	and	%g2, MACH_TRAP_ADDR_MASK, %g2		/* align to 4k bound. */
	mov	%g2, %g6				/* keep value of dest */
	clr	%g3					/* clear counter */
copyingTable:
	ldd	[%g1], %g4				/* copy first 2 words */
	std	%g4, [%g2]
	ldd	[%g1 + 8], %g4				/* next 2 words */
	std	%g4, [%g2 + 8]
	add	%g2, 16, %g2				/* incr. destination */
	add	%g3, 2, %g3				/* incr. counter */
	cmp	%g3, %l2				/* how many copies */
	bne	copyingTable
	nop

	/*
	 * Now copy in the overflow and underflow trap code.  These traps
	 * bypass the regular preamble and postamble for speed, and because
	 * they are coded so that the only state they need save is the psr.
	 * %g6 was the trap table address saved from above.
	 */
	set	machProtoWindowOverflow, %g1		/* new src */
	set	MACH_WINDOW_OVERFLOW, %g2		/* get trap offset */
	add	%g6, %g2, %g2				/* offset in table */
	ldd	[%g1], %g4				/* copy first 2 words */
	std	%g4, [%g2]
	ldd	[%g1 + 8], %g4				/* copy next 2 words */
	std	%g4, [%g2 + 8]

	set	machProtoWindowUnderflow, %g1		/* new src */
	set	MACH_WINDOW_UNDERFLOW, %g2		/* get trap type */
	add	%g6, %g2, %g2				/* offset in table */
	ldd	[%g1], %g4				/* copy first 2 words */
	std	%g4, [%g2]
	ldd	[%g1 + 8], %g4				/* copy next 2 words */
	std	%g4, [%g2 + 8]
	/*
	 * Now copy the handler for non-maskable asynchronous memory error
	 * interrupts.  We're going to die anyway on these errors, so all
	 * we want to do is grab information and put it into registers.
	 */
	set	machProtoLevel15Intr, %g1		/* new src */
	set	MACH_LEVEL15_INT, %g2			/* get trap offset */
	add	%g6, %g2, %g2				/* offset in table */
	ldd	[%g1], %g4				/* copy first 2 words */
	std	%g4, [%g2]
	ldd	[%g1 + 8], %g4				/* copy next 2 words */
	std	%g4, [%g2 + 8]

	mov	%g6, %tbr			/* switch in my trap address */
	sethi	%hi(_machTBRAddr), %g2
	st	%g6, [%g2 + %lo(_machTBRAddr)]	/* save tbr addr in C var */
	MACH_WAIT_FOR_STATE_REGISTER()			/* let it settle for
							 * the necessary
							 * amount of time.  Note
							 * that during this
							 * wait period, we
							 * may get an interrupt
							 * to the old tbr if
							 * interrupts are
							 * disabled.  */
#ifdef sun4c
	sethi	%hi(_machRomVectorPtr), %g6
	st	%g7, [%g6 + %lo(_machRomVectorPtr)] /* save romvec pointer */
#endif
        /*
         * Initialize first word of restart table to show it hasn't yet
         * been set up (this is a hard reboot).
         */
        set     reservedSpace2, %g1
        add     %g1, VMMACH_PAGE_SIZE, %g1
        and     %g1, ~(VMMACH_PAGE_SIZE - 1), %g1
        set     _mach_RestartTablePtr, %g2
        st      %g1, [%g2]
        st      %g0, [%g1]
#ifndef RECOV_NOCOPY
        /*
         * Now copy initialized data segment to storedData and set
         * storedDataSize in both places to correct value.  Is etext the right
         * place to start copying?  I really want the beginning of the data area
         * instead.  %g1 is the src, %g2 is the destination, and %g3 is the
         * amount to copy.
         */
CopyInitData:
        set     _etext, %g1
        set     _edata, %g2
        sub     %g2, %g1, %g3
        set     _storedDataSize, %g4
        st      %g3, [%g4]
        set     _storedData, %g2
MoreCopying:
        ldd     [%g1], %g4
        std     %g4, [%g2]
        add     %g1, 8, %g1
        add     %g2, 8, %g2
        subcc   %g3, 8, %g3
        bg      MoreCopying
        nop
#endif /* RECOV_NOCOPY */

	call	_main
	nop

	
.globl	_MachTrap
/*
 * Reserve twice the amount of space we need for the trap table.
 * Then copy machProtoVectorTable into it repeatedly, starting at
 * a 4k-byte alignment.  This is dumb, but the assembler doesn't allow
 * me to do much else.
 *
 * Note that this filler cannot use l1 or l2 since that's where pc and npc
 * are written in a trap.
 */
.align	8
machProtoVectorTable:
	sethi	%hi(_MachTrap), %VOL_TEMP1	/* set _MachTrap, %VOL_TEMP1 */
	or	%VOL_TEMP1, %lo(_MachTrap), %VOL_TEMP1
	jmp	%VOL_TEMP1		/* must use non-pc-relative jump here */
	rd	%psr, %CUR_PSR_REG

machProtoWindowOverflow:
	sethi	%hi(MachHandleWindowOverflowTrap), %VOL_TEMP1
	or	%VOL_TEMP1, %lo(MachHandleWindowOverflowTrap), %VOL_TEMP1
	jmp	%VOL_TEMP1
	rd	%psr, %CUR_PSR_REG

machProtoWindowUnderflow:
	sethi	%hi(MachHandleWindowUnderflowTrap), %VOL_TEMP1
	or	%VOL_TEMP1, %lo(MachHandleWindowUnderflowTrap), %VOL_TEMP1
	jmp	%VOL_TEMP1
	rd	%psr, %CUR_PSR_REG

machProtoLevel15Intr:
	sethi	%hi(MachHandleLevel15Intr), %VOL_TEMP1
	or	%VOL_TEMP1, %lo(MachHandleLevel15Intr), %VOL_TEMP1
	jmp	%VOL_TEMP1
	rd	%psr, %CUR_PSR_REG

.align	8
reserveSpace:	.skip	0x2000

/*
 * The actual space for the fast restart copy of the kernel's initialized data.
 */
.align 8
.globl  _storedData
_storedData:    .skip   MACH_RESTART_DATA_SIZE
reservedSpace2: .skip   MACH_RESTART_TABLE_SIZE + VMMACH_PAGE_SIZE
