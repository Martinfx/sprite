/* 
 * vmList.c --
 *
 *	Routines for maintaining lists
 *
 * Copyright 1991 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that this copyright
 * notice appears in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/kernel/vm/RCS/vmList.c,v 9.3 91/06/21 12:07:11 jhh Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include <sprite.h>
#include <list.h>
#include <sys.h>
#include <vm.h>
#include <vmInt.h>


/*
 * ----------------------------------------------------------------------------
 *
 * VmListInsert --
 *
 *	Insert the list element pointed to by itemPtr into a List after 
 *	destPtr.  Perform a primitive test for self-looping by returning
 *	failure if the list element is being inserted next to itself.
 *
 * Results:
 *	If neither List_Links structure is NIL, they are assumed to be valid
 *	and SUCCESS is returned.  If either one is NIL then FAILURE is
 *	returned.  
 *
 * Side effects:
 *	The list containing destPtr is modified to contain itemPtr.
 *
 * ----------------------------------------------------------------------------
 */
void
VmListInsert(itemPtr, destPtr)
    register	List_Links *itemPtr;	/* structure to insert */
    register	List_Links *destPtr;	/* structure after which to insert it */
{
    if (itemPtr->nextPtr != (List_Links *) NIL ||
	itemPtr->prevPtr != (List_Links *) NIL) {
	panic("VmListInsert: Inserting element twice.\n");
    }

    if (itemPtr == (List_Links *) NIL || destPtr == (List_Links *) NIL
	    || !itemPtr || !destPtr || (itemPtr == destPtr)) {
	panic("VmListInsert: Bad item or dest ptr.\n");
    }
    itemPtr->nextPtr = destPtr->nextPtr;
    itemPtr->prevPtr = destPtr;
    destPtr->nextPtr->prevPtr = itemPtr;
    destPtr->nextPtr = itemPtr;
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmListRemove --
 *
 *	Remove a list element from the list in which it is contained.
 *
 * Results:
 *	If the list element is invalid or is the list header, FAILURE is 
 *	returned.  Otherwise SUCCESS is returned.
 *
 * Side effects:
 *	The given structure is removed from its containing list.
 *
 * ----------------------------------------------------------------------------
 */
void
VmListRemove(itemPtr)
    register	List_Links *itemPtr;	/* list element to remove */
{
    if (itemPtr->nextPtr == (List_Links *) NIL ||
	itemPtr->prevPtr == (List_Links *) NIL) {
	panic("VmListRemove: Item not on list.\n");
    }
    if (itemPtr == (List_Links *) NIL || itemPtr == itemPtr->nextPtr
	    || !itemPtr) {
	panic("VmListRemove: Bad itemPtr.\n");
    }
    itemPtr->prevPtr->nextPtr = itemPtr->nextPtr;
    itemPtr->nextPtr->prevPtr = itemPtr->prevPtr;
    itemPtr->prevPtr = (List_Links *) NIL;
    itemPtr->nextPtr = (List_Links *) NIL;
}


/*
 * ----------------------------------------------------------------------------
 *
 * VmListMove --
 *
 *	Move the list element referenced by itemPtr to follow destPtr.
 *
 * Results:
 *	If either list element is invalid, FAILURE is returned.
 *	Otherwise SUCCESS is returned.
 *
 * Side effects:
 *	List ordering is modified.
 *
 * ----------------------------------------------------------------------------
 */
void
VmListMove(itemPtr, destPtr)
    register List_Links *itemPtr; /* list element to be moved */
    register List_Links *destPtr; /* element after which it is to be placed */
{
    if (itemPtr == (List_Links *) NIL || destPtr == (List_Links *) NIL
	    || !itemPtr || !destPtr) {
	panic("VmListMove: Bad item or dest ptr.\n");
    }
    /*
     * It is conceivable that someone will try to move a list element to
     * be after itself.
     */
    if (itemPtr != destPtr) {
	VmListRemove(itemPtr);
	VmListInsert(itemPtr, destPtr);
    }    
}

