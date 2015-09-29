/* 
 * _mcount.c --
 *
 *	This is the code for the routine mcount.  mcount is the routine
 *	called at the beginning of each procedure if it the code has
 *	been compiled with the -p option to cc.
 *
 *	NB: The compiler compiles this procedure into something called
 *	"_mcount" which we massage back into "mcount" (see the Makefile).
 *
 * Copyright 1985 Regents of the University of California
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Header: /cdrom/src/kernel/Cvsroot/kernel/prof/sun4.md/_mcount.c,v 1.4 90/11/08 10:35:07 rab Exp $ SPRITE (Berkeley)";
#endif

#include <sprite.h>
#include <prof.h>
#include <profInt.h>
#include <sync.h>
#include <sys.h>
#include <dbg.h>
#include <stdio.h>

#define MCOUNT

/*
 * Boolean to prevent recursion in mcount.  This only works
 * on a uniprocessor.  This is needed in case we call printf
 * or a Sync_ routine from within mcount and those routines
 * have been instrumented with calls to mcount.
 */
static Boolean inMcount = FALSE;

/*
 * There is a critical section when mcount does a pseudo-alloc
 * of the storage for its arcs.
 */
#ifdef MCOUNT 
#ifndef lint
static Sync_Semaphore	mcountMutex = Sync_SemInitStatic("mcountMutex");
#endif
#endif


/*
 *----------------------------------------------------------------------
 *
 * mcount --
 *
 *	A call to this routine is inserted by the compiler at the
 *	beginning of every routine. (Use the -p option to cc.)
 *	This looks up the call stack a bit to determine which arc
 *	of the call graph is being executed.  A call graph arc represents
 *	one routine calling another.  The routine with the call to mcount
 *	is the callee of the arc, its caller (mcount's "grandparent")
 *	is the caller of the arc.  An execution count is kept for each
 *	arc.  The counts are dumped out and analyzed by the gprof program.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Increment a counter corresponding to the call graph arc.
 *
 *----------------------------------------------------------------------
 */
void
__mcount(callerPC, calleePC)
    unsigned int callerPC;	/* PC of instr. that called mcount's caller */
    unsigned int calleePC;	/* PC of instr. that called mcount */
{
#ifdef MCOUNT
    register unsigned int instructionNumber;	/* Index into profArcIndex */
    register ProfRawArc *arcPtr;	/* Pointer to arc data storage */

    if (!profEnabled) {
	return;
    }

    if (inMcount) {
	return;
    } else {
	inMcount = TRUE;
    }

    /*
     * Use the PC of the caller as an index into the
     * index of stored arcs.  There should only be one call instruction
     * that corresponds to the index.
     *
     * Go from PC to instruction number by subracting off the base
     * PC and dividing by the instruction size (4 bytes).
     */

    if (callerPC < (unsigned int)&spriteStart) {
	/*
	 * The PC from the caller's frame is bad. This happens when a 
	 * new process is started. 
	 */
	goto exit;
    }

    instructionNumber = 
	    (callerPC - (unsigned int) &spriteStart) >> PROF_ARC_SHIFT;
    if (instructionNumber > profArcIndexSize) {
	printf("_mcount: PC %x: Index (%d) exceeds bounds (%d)\n",
		  callerPC, instructionNumber, profArcIndexSize);
	goto exit;
    }

    /*
     * Check to see if arcPtr equals an unused value (which is 0 because
     * profArcIndex is initialized with bzero in Prof_Start).
     */

    arcPtr = profArcIndex[instructionNumber];
    if (arcPtr == (ProfRawArc *) 0) {

#ifdef DEBUG
	printf("mcount: 1 callerPC = %x(%d), calleePC = %x\n",
			    callerPC, instructionNumber, calleePC);
	/* DBG_CALL; */
#endif

	/*
	 * First time call graph arc has been traversed.  Allocate arc
	 * storage from the arcList and initialize it.  This is locked
	 * to prevent the scheduler from interrupting the allocation
	 * and initialization.
	 */

	if (profArcListFreePtr >= profArcListEndPtr) {
	    profEnabled = FALSE;
	    printf("_mcount: No more arcs, stopping profiling\n");
	} else {

	    MASTER_LOCK(&mcountMutex);
	    Sync_SemRegister(&mcountMutex);

	    arcPtr = profArcListFreePtr;
	    profArcListFreePtr++;
	    profArcIndex[instructionNumber] = arcPtr;
	    arcPtr->calleePC = calleePC;
	    arcPtr->count    = 1;
	    arcPtr->link     = (ProfRawArc *)NIL;

	    MASTER_UNLOCK(&mcountMutex);
	}
	goto exit;
    }

    while (arcPtr->calleePC != calleePC) {
	/*
	 * Loop through the list of callee's for this caller.
	 */

	if (arcPtr->link == (ProfRawArc *)NIL) {

	    /*
	     *  Allocate, link, and initialize another arc storage unit.
	     */
#ifdef DEBUG
	    printf("mcount 2 callerPC = %x(%d), calleePC = %x\n",
			callerPC, instructionNumber, calleePC);
	/* DBG_CALL; */
#endif

	    if (profArcListFreePtr >= profArcListEndPtr) {
		printf("_mcount: No more arcs\n");
	    } else {
		MASTER_LOCK(&mcountMutex);

		arcPtr->link = profArcListFreePtr;
		profArcListFreePtr++;

		arcPtr = arcPtr->link;
		arcPtr->calleePC	= calleePC;
		arcPtr->count		= 1;
		arcPtr->link		= (ProfRawArc *) NIL;

		MASTER_UNLOCK(&mcountMutex);
	    }
	    goto exit;
	}
	arcPtr = arcPtr->link;
    }
    arcPtr->count++;

exit:
#endif /* MCOUNT */
    inMcount = FALSE;
    return;
}
