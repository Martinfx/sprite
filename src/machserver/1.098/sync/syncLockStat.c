/* 
 * syncLockStat.c --
 *
 * 	Maintain statistics about lock usage and lock dependencies.
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
static char rcsid[] = "$Header: /sprite/src/kernel/sync/RCS/syncLockStat.c,v 9.1 90/10/05 17:50:34 mendel Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include <sync.h>
#include <syncInt.h>
#include <stdlib.h>
#include <dbg.h>
#include <stdio.h>

/*
 * Lock registration semaphore.
 */
Sync_Semaphore regMutex = Sync_SemInitStatic("regMutex");
Sync_Semaphore *regMutexPtr = &regMutex;

#ifdef LOCKREG
/*
 * Number of types registered.
 */
static 	int	syncTypeCount = 0;
/*
 * Information on each type of lock registered.
 */
static  Sync_RegElement		regInfo[SYNC_MAX_LOCK_TYPES];
#endif
static 	Boolean	initialized = FALSE;

/*
 * Keep track of locks we see with bad types. This is for debugging purposes
 * only.
 */
#define  MAX_BAD_TYPES	100
struct  BadLockType {
	Address		lockPtr;
	Address		pc;
	int		type;
}  badType[MAX_BAD_TYPES];

int		badTypeCount = 0;



/*
 *----------------------------------------------------------------------
 *
 * Sync_LockStatInit --
 *
 *	Initializes the lock statistics routines. Must be called before
 *	any kernel processes are created, and after the proc module
 *	is initialized.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Sync_LockStatInit()
{
    initialized = TRUE;
    /*
     * regMutex can't be treated like a normal lock otherwise we have a
     * chicken and the egg problem.
     */
#ifdef LOCKREG
    regMutex.type = -1; 
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * SyncAddPriorInt --
 *
 *	Adds the prior lock  to the list of prior locks  in the 
 *	current lock. Adds the current lock to the stack of 
 *	locks in the pcb. The current lock is registered if it hasn't
 *	been already.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The lock stack in the pcb is changed.
 *
 *----------------------------------------------------------------------
 */

/*ARGSUSED*/

void
SyncAddPriorInt(type, priorCountPtr, priorTypes, lockPtr, pcbPtr)
    int				type;
    int 			*priorCountPtr;
    int				*priorTypes;
    Address			lockPtr;
    Proc_ControlBlock		*pcbPtr;
{
#ifdef LOCKDEP
    int			priorType;
    int			i;
    Address		priorLockPtr;
    static Boolean	firstOverflow = TRUE;

    if (pcbPtr == (Proc_ControlBlock *) NIL || !initialized) {
	return;
    }

    Proc_GetCurrentLock(pcbPtr, &priorType, &priorLockPtr);
    if (priorType > syncTypeCount) {
	if (badTypeCount < MAX_BAD_TYPES) {
	    badType[badTypeCount].lockPtr = priorLockPtr;
	    badType[badTypeCount].pc = FIELD(priorLockPtr, holderPC);
	    badType[badTypeCount].type = priorType;
	    badTypeCount++;
	}
    }
    if (priorType >= 0) {
	for (i = 0; i < *priorCountPtr; i++) {
	    if (priorType == priorTypes[i]) {
		break;
	    }
	}
	if (i == *priorCountPtr && i < SYNC_MAX_PRIOR) {
	    priorTypes[i] = priorType;
	    *priorCountPtr += 1;
	} else if (i >= SYNC_MAX_PRIOR && firstOverflow) {
	    printf("SyncAddPrior: too many prior types.\n");
	    firstOverflow = FALSE;
	}
    }
    if (type == 0) {
	Sync_LockRegister(lockPtr);
/* this routine never gets called if LOCKREG is not defined, but lint 
 * will complain about this assignment anyway.
 */
	type = FIELD(lockPtr, type);
    }
    Proc_PushLockStack(pcbPtr, type, lockPtr);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * SyncDeleteCurrentInt --
 *
 *	Removes a prior lock from the lock stack.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The lock stack in the pcb is changed.
 *
 *----------------------------------------------------------------------
 */

void
SyncDeleteCurrentInt(lockPtr, pcbPtr)
    Address			lockPtr;
    Proc_ControlBlock		*pcbPtr;
{
    if (pcbPtr == (Proc_ControlBlock *) NIL || !initialized) {
	return;
    }
    Proc_RemoveFromLockStack(pcbPtr, lockPtr); 
}

/*
 *----------------------------------------------------------------------
 *
 *  SyncMergePriorInt --
 *
 *	Merge the prior entries for a given lock with the prior entries
 *	for the type. If an entry is a duplicate it is discarded, and
 *	if the prior entry database overflows an error message is printed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stuff might be printed to the screen.
 *
 *----------------------------------------------------------------------
 */
void
SyncMergePriorInt(priorCount, priorTypes, regPtr)
    int 		priorCount;
    int			*priorTypes;
    Sync_RegElement	*regPtr;
{
    int		i;
    int		j;

    if (!initialized) {
	return;
    }
    for (i = 0; i < priorCount; i++) {
	for (j = 0; j < regPtr->priorCount; j++ ) {
	    if (regPtr->priorTypes[j] == priorTypes[i]) {
		break;
	    }
	}
	if (j == regPtr->priorCount) {
	    if (j == SYNC_MAX_PRIOR) {
		break;
	    }
	    regPtr->priorTypes[j] = priorTypes[i];
	    regPtr->priorCount++;
	}
    }
    if (i < priorCount) {
	printf("SyncMergePriorInt: %d too many prior types.\n",
	       priorCount - i);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Sync_RegisterInt --
 *
 *	Registers a lock of either class (semaphore or lock). If an element
 *	of the type exists then the lock is added to the linked list of 
 *	active locks of that type. Type equality is determined by comparing
 *	the ascii name of the locks. If the lock is of a new type then an
 *	element for the type is added to the list and the new lock is added
 *	to the element.
 *
 *	This routine should be called prior to using the lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A new element may be added to regQueue.
 *
 *----------------------------------------------------------------------
 */

#ifndef LOCKREG
/* ARGSUSED */
#endif

void
Sync_RegisterInt(lock)
    Address		lock;		/*lock to be registered */
{
#ifdef LOCKREG
    List_Links		*lockQueuePtr;
    Sync_RegElement	*regPtr;
    char		*name;
    static int		newTypeGenerator = 1;
    int			*typePtr;
    Sync_ListInfo	*listInfoPtr;
    int			i;

    typePtr = &(FIELD(lock,type)) ;
    if (*typePtr != 0) {
	return;
    }
    name = FIELD(lock,name);
    if (name == (char *) 0) {
	return;
    }
    if (initialized) {
	MASTER_LOCK(regMutexPtr);
    }
    if (*typePtr != 0 || FIELD(lock, name) == (char *) 0) {
	goto exit;
    }
    listInfoPtr = &(FIELD(lock,listInfo));
    regPtr = (Sync_RegElement *) NIL;
    for (i = 0; i < syncTypeCount; i++) {
	if (!strcmp(name, regInfo[i].name)) {
	    regPtr = &regInfo[i];
	    regPtr->activeLockCount++;
	    break;
	}
    }
    if (regPtr == (Sync_RegElement *) NIL) {
	if (syncTypeCount >= SYNC_MAX_LOCK_TYPES) {
	    printf("Sync_RegisterAnyLock: too many lock types.\n");
	    goto exit;
	}
	regPtr = &regInfo[syncTypeCount];
	regPtr->name = name;
	regPtr->type = newTypeGenerator++;
	regPtr->activeLockCount = 1;
	regPtr->deadLockCount = 0;
	regPtr->hit = 0;
	regPtr->miss = 0;
	regPtr->priorCount = 0;
	List_Init((List_Links *) &(regPtr->activeLocks));
	syncTypeCount++;
    }
    lockQueuePtr = (List_Links *) &(regPtr->activeLocks);
    if (listInfoPtr == (Sync_ListInfo *) lockQueuePtr->prevPtr) {
	panic("Trying to reregister a lock.\n");
    }
    regPtr->class = ((Sync_Lock *) lock)->class;
    *typePtr = regPtr->type;
    listInfoPtr->lock = lock;
    List_InitElement((List_Links *) listInfoPtr);
    List_Insert((List_Links *) listInfoPtr, 
		LIST_ATREAR(lockQueuePtr));
exit:
    if (initialized) {
	MASTER_UNLOCK(regMutexPtr);
    }
#endif /* LOCKREG */
}

/*
 *----------------------------------------------------------------------
 *
 * Sync_CheckoutInt --
 *
 *	Used to de-register ("checkout") a lock when it is being
 *	deallocated. It is removed from the linked list of active locks for
 *	the type, and its statistics are merged with the running total in
 *	the type element.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#ifndef LOCKREG
/*ARGSUSED*/
#endif

void
Sync_CheckoutInt(lock)
    Address		lock;		/*lock to be registered */
{
#ifdef LOCKREG
    List_Links		*lockQueuePtr;
    List_Links		*itemPtr;
    Sync_RegElement	*regPtr;
    int			type;

    if (initialized) {
	MASTER_LOCK(regMutexPtr);
    }
    type = FIELD(lock,type);
    if (type <= 0) {
	goto exit;
    }
    regPtr = &regInfo[type-1];
    lockQueuePtr = (List_Links *) &(regPtr->activeLocks);
    LIST_FORALL(lockQueuePtr, itemPtr) {
	if (((Sync_ListInfo *) itemPtr)->lock == lock) {
	    List_Remove(itemPtr);
	    regPtr->activeLockCount--;
	    regPtr->deadLockCount++;
	    SyncAddLockStats(regPtr, ((Sync_ListInfo *) itemPtr)->lock);
	    goto exit;
	}
    }
exit:
    if (initialized) {
	MASTER_UNLOCK(regMutexPtr);
    }
#endif /* LOCKREG */
}

/*
 *----------------------------------------------------------------------
 *
 * Sync_GetLockStats --
 *
 *	Prints out the locking statistics.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#ifndef LOCKREG
/*ARGSUSED*/
#endif

ReturnStatus
Sync_GetLockStats(size, argPtr)
    int	 	size;
    Address	argPtr;

{

#ifdef LOCKREG
    List_Links		*lockQueuePtr;
    List_Links		*itemPtr;
    Sync_RegElement	*regPtr;
    Sync_RegElement	tempReg;
    int			i;
    int			j;
    Sync_LockStat	*statArray;
    int			index;
    ReturnStatus	status;

    if (size <= 0) {
	return SUCCESS;
    }
    if (size < syncTypeCount) {
	return FAILURE;
    }
    statArray = (Sync_LockStat *) malloc(size * sizeof(Sync_LockStat));
    MASTER_LOCK(regMutexPtr);
    bzero((char *) statArray, size * sizeof(Sync_LockStat));
    for (i = 0; i < syncTypeCount; i++) {
	regPtr = &regInfo[i];
	bcopy((char *) regPtr, (char *) &tempReg, sizeof(Sync_RegElement));
	lockQueuePtr = (List_Links *) &(regPtr->activeLocks);
	LIST_FORALL(lockQueuePtr, itemPtr) {
	    SyncAddLockStats(&tempReg, ((Sync_ListInfo *) itemPtr)->lock);
	}
	index = regPtr->type -1;
	statArray[index].inUse = 1;
	statArray[index].class = (regPtr->class == SYNC_SEMAPHORE) ? 0 : 1;
	statArray[index].hit = tempReg.hit;
	statArray[index].miss = tempReg.miss;
	statArray[index].activeCount = regPtr->activeLockCount;
	statArray[index].deadCount = regPtr->deadLockCount;
	strncpy(statArray[index].name, regPtr->name, 30);
	statArray[index].name[29] = '\0';
	statArray[index].priorCount = tempReg.priorCount;
	for (j = 0; j < tempReg.priorCount; j++) {
	    statArray[index].priorTypes[j] = tempReg.priorTypes[j];
	}
	for (j = 0; j < mach_NumProcessors; j++) {
	    statArray[index].spinCount += sync_Instrument[j].spinCount[index+1];
	}
    }
    Vm_CopyOut(sizeof(Sync_LockStat) * size, (Address)statArray, argPtr);
    MASTER_UNLOCK(regMutexPtr);
    free((Address) statArray);
    return (SUCCESS);
#else  /* LOCKREG */
    return (FAILURE);
#endif /* LOCKREG */
}

/*
 *----------------------------------------------------------------------
 *
 * Sync_ResetLockStats --
 *
 *	Resets all the locking statistics.
 *
 * Results:
 *	FAILURE if an error occurred, SUCCESS otherwise.
 *
 * Side effects:
 *	Hit and miss counts are reset, count of prior lock types is
 *	reset.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Sync_ResetLockStats()
{
#ifdef LOCKREG
    int 		i;
    int			j;
    List_Links		*lockQueuePtr;
    List_Links		*itemPtr;
    Sync_RegElement	*regPtr;

    MASTER_LOCK(regMutexPtr);
    for (i = 0; i < syncTypeCount; i++) {
	regPtr = &regInfo[i];
	lockQueuePtr = (List_Links *) &(regPtr->activeLocks);
	LIST_FORALL(lockQueuePtr, itemPtr) {
	    *(&FIELD(((Sync_ListInfo *) itemPtr)->lock, hit)) = 0;
	    *(&FIELD(((Sync_ListInfo *) itemPtr)->lock, miss)) = 0;
#ifdef LOCKDEP
	    *(&FIELD(((Sync_ListInfo *) itemPtr)->lock, priorCount)) = 0;
#endif
	}
	regPtr->hit = 0;
	regPtr->miss = 0;
	regPtr->priorCount = 0;
    }
    for (i = 0; i < mach_NumProcessors; i++) {
	for (j = 0; j < syncTypeCount+1; j++) {
	    sync_Instrument[i].spinCount[j] = 0;
	}
	sync_Instrument[i].sched_MutexMiss = 0;
    }
    MASTER_UNLOCK(regMutexPtr);
    return SUCCESS;
#else  /* LOCKREG */
    return (FAILURE);
#endif /* LOCKREG */

}
