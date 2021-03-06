/* 
 * fsDispatch.c --
 *
 *	This file contains routines that implement a dispatcher for events
 *	on streams and timeouts. The dispatcher handles the details of
 *	waiting for events to occur on streams. When an event occurs, the
 *	dispatcher calls a routine supplied by the clients to handle the
 *	event. Also, timeout handlers can be created so that a client-supplied
 *	routine can be called at a specific time or at regular intervals.
 *
 * Copyright 1987 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/lib/c/etc/RCS/fsDispatch.c,v 1.7 89/11/22 16:34:29 ouster Exp $ SPRITE (Berkeley)";
#endif not lint

#include <sprite.h>
#include <errno.h>
#include <fs.h>
#include <list.h>
#include <bit.h>
#include <spriteTime.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 * The data structure used by Fs_EventHandler{Create,Destroy} and Fs_Dispatch 
 * to manage event handlers for streams. The information is kept in an
 * array that is indexed by the stream ID.
 *
 */

typedef struct {
    void	(*proc)();	/* Routine to be called. */
    ClientData	data;		/* Data passed to proc. */
    int		eventMask;	/* Mask of events cause proc to be called. */
    Boolean	inUse;		/* Consistency check. */
} StreamInfo;

#define MAX_NUM_STREAMS		256
static StreamInfo infoArray[MAX_NUM_STREAMS];


/*
 * Bit arrays for use with select. ReadMask, writeMask and exceptMask
 * are the master copies of the masks -- they are updated by 
 * Fs_EventHandlerCreate and Fs_EventHandlerDestroy.
 */

#define MASK_SIZE	(Bit_NumInts(MAX_NUM_STREAMS))
static int	readMask[MASK_SIZE];
static int	writeMask[MASK_SIZE];
static int	exceptMask[MASK_SIZE];

/*
 * MaxPossNumStreams is the maximum value for the number of active streams. 
 * For example if streams 0, 1, and 5 are the only active streams, 
 * maxPossNumStreams should equal to 6 because streams 0-5 could active.
 */
static int	maxPossNumStreams = -1;


/*
 * The data structure used by Fs_TimeoutHandler{Create,Destroy},
 * CallTimeoutHandler and ComputeTimeoutValue to manage timeout handlers.
 * The information is kept in a doubly-linked list that is sorted on 
 * the time field.
 */

typedef struct {
    List_Links	links;
    void	(*proc)();	/* Routine to be called. */
    ClientData	data;		/* Data passed to proc. */
    Time	time;		/* Absolute time when proc should be called. */
    Time	interval;	/* Relative time when proc should be called. */
    Boolean	resched;	/* If TRUE, reschedule the proc to be called
				 * again at the next interval. */
} TimeoutInfo;
static List_Links	timeoutList;

/*
 * Forever is the amount of time to wait until the next timeout
 * (a very, very long time).
 */
static Time forever = { 0x7fffffff, 0 };

/*
 * MIN_TIMEOUT is the # of microseconds that must past before the next
 * call to a timeout handler, i.e. the minimum interval between calls to
 * timeout handler. This is used to make sure the dispatcher doesn't
 * spend all its time handling timeouts.
 *
 * 40000 microseconds = 40 milliseconds.
 */

#define MIN_TIMEOUT  40000

/*
 * A flag to indicate if DispatcherInit() has been called or not.
 */
static Boolean initialized = FALSE;

/*
 * Statistics about what type of event (stream or timeout) occurs
 * during each call to Fs_Dispatch().
 */
unsigned int fsNumTimeoutEvents = 0;
unsigned int fsNumStreamEvents = 0;

/*
 * Forward references.
 */

static void	SearchMask();
static void	CallTimeoutHandler();
static Boolean	ComputeTimeoutValue();
static void	DispatcherInit();


/*
 *----------------------------------------------------------------------
 *
 * DispatcherInit --
 *
 *	Initializes the data structures necessary to manage the event handlers,
 *	the timer queue of procedures and the select bitmasks. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The infoArray and timer queue list are initialized.
 *	The select bitmasks are set to 0.
 *
 *----------------------------------------------------------------------
 */

static void
DispatcherInit()
{
    initialized = TRUE;

    bzero((char *) infoArray, sizeof(StreamInfo) * MAX_NUM_STREAMS);
    List_Init(&timeoutList);

    Bit_Zero(MAX_NUM_STREAMS, readMask);
    Bit_Zero(MAX_NUM_STREAMS, writeMask);
    Bit_Zero(MAX_NUM_STREAMS, exceptMask);
}


/*
 *----------------------------------------------------------------------
 *
 * Fs_Dispatch --
 *
 *      This routine calls select to wait for a timeout or for a stream
 *      to become readable, writable and/or has an exception condition
 *      pending.  It looks at the results returned by select, and calls
 *      client routines to handle the events.  Client routines must have
 *      been pre-registered by calling Fs_EventHandlerCreate or
 *      Fs_TimeoutHandlerCreate.
 *
 *	The readiness event handlers are called in ascending order 
 *	based on the stream ID, regardless of the order of calls to 
 *	Fs_EventHandlerCreate. However, the timeout handlers are
 *      called in the order determined by the ordering of calls
 *      to Fs_TimeoutHandlerCreate.
 *
 *	The routine will return after handling the events from a
 *	single select call.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The called routines may cause side-effects.
 *
 *----------------------------------------------------------------------
 */

void
Fs_Dispatch()
{
    register int	streamID;
    Time		timeout;
    Time		*timeoutPtr;
    int			numReady;
    int			tempReadMask[MASK_SIZE];
    int			tempWriteMask[MASK_SIZE];
    int			tempExceptMask[MASK_SIZE];

    if (!initialized) {
	panic("Fs_Dispatch: not initialized yet.\n");
    }

    /*
     * First compute how time must elapse before the next routine
     * on the timeout queue should be called. If ComputeTimeoutValue()
     * determines that a routine should be called immediately, it
     * returns TRUE and we go to CallTimeoutHandler() to call the routine.
     */
    
    if (ComputeTimeoutValue(&timeout)) {
	CallTimeoutHandler();
	fsNumTimeoutEvents++;
	return;
    }

    if (Time_EQ(timeout, forever)) {
	/*
	 * Nothing on the timeout queue.
	 */

	if (maxPossNumStreams == 0) {
	    panic("Fs_Dispatch: nothing to do.\n");
	    return;
	} else {
	    timeoutPtr = (Time *) NULL;
	}
    } else {
	if (maxPossNumStreams == 0) {
	    /*
	     * No streams to select so just wait until the next routine
	     * on the timeout queue needs to be called.
	     */

	    select(0, (int *) NULL, (int *) NULL, (int *) NULL,
		    (struct timeval *) &timeout);
	    CallTimeoutHandler();
	    fsNumTimeoutEvents++;
	    return;
	} else {
	    timeoutPtr = &timeout;
	}
    }


    /*
     * Wait for an event on 1 or more streams or until a timeout
     * period has expired.
     */

    Bit_Zero(MAX_NUM_STREAMS, tempReadMask);
    Bit_Zero(MAX_NUM_STREAMS, tempWriteMask);
    Bit_Zero(MAX_NUM_STREAMS, tempExceptMask);

    Bit_Copy(maxPossNumStreams, readMask, tempReadMask);
    Bit_Copy(maxPossNumStreams, writeMask, tempWriteMask);
    Bit_Copy(maxPossNumStreams, exceptMask, tempExceptMask);


    numReady = select(maxPossNumStreams, tempReadMask, tempWriteMask,
	    tempExceptMask, (struct timeval *) timeoutPtr);

    if (numReady == 0) {
	/*
	 * Nothing happened on the streams but a routine in the timeout
	 * queue needs to be called now.
	 */
	CallTimeoutHandler();
	fsNumTimeoutEvents++;

    } else if (numReady < 0) {
	if (errno != EINTR) {
	    fprintf(stderr, "Fs_Dispatch select error: %s\n", strerror(errno));
	    exit(1);
	}

    } else {
	register int	event;
	register StreamInfo	*infoPtr;

	fsNumStreamEvents++;

	/*
	 * Something happened on a stream (or streams). Go through
	 * the masks to find out which streams need attention.
	 * Call the routine to handle the event on the stream.
	 *
	 * We want to call the handler just once so when searching through
	 * the read mask, see if the stream is also writable and has an
	 * exception condition pending, and likewise, in the writable mask
	 * search see if the stream also has an exception condition pending.
	 * This ensures that a handler is called only once, with the
	 * eventMask containing 1 to 3 events. A simpler search technique
	 * is to search the 3 masks independently but then a handler could
	 * potentially be called up to 3 times. The first technique is
	 * better because the handler can determine the order of how it
	 * handles the events.
	 */

	streamID = Bit_FindFirstSet(maxPossNumStreams, tempReadMask);
	while (streamID != -1) {
	    if (streamID < 0 || streamID > MAX_NUM_STREAMS) {
		panic("Fs_Dispatch: stream ID %d out of range\n", streamID);
	    }

	    infoPtr = &(infoArray[streamID]);

	    /*
	     * There used to be code here to check inUse and panic
	     * if not set.  However, it's possible that one event
	     * handler could delete another event handler, causing
	     * inUse to be turned off in the second one before we
	     * get to this point.  Thus, don't check inUse here.
	     */

	    event = FS_READABLE;
	    Bit_Clear(streamID, tempReadMask);

	    if (Bit_IsSet(streamID, tempWriteMask)) {
		event |= FS_WRITABLE;
		Bit_Clear(streamID, tempWriteMask);
	    }
	    if (Bit_IsSet(streamID, tempExceptMask)) {
		event |= FS_EXCEPTION;
		Bit_Clear(streamID, tempExceptMask);
	    }

	    if (infoPtr->eventMask & event) {
		(*infoPtr->proc) (infoPtr->data, streamID, event);
	    }

	    streamID = Bit_FindFirstSet(maxPossNumStreams, tempReadMask);
	}

	streamID = Bit_FindFirstSet(maxPossNumStreams, tempWriteMask);
	while (streamID != -1) {
	    if (streamID < 0 || streamID > MAX_NUM_STREAMS) {
		panic("Fs_Dispatch: stream ID %d out of range\n", streamID);
	    }

	    infoPtr = &(infoArray[streamID]);
	    event = FS_WRITABLE;
	    Bit_Clear(streamID, tempWriteMask);

	    if (Bit_IsSet(streamID, tempExceptMask)) {
		event |= FS_EXCEPTION;
		Bit_Clear(streamID, tempExceptMask);
	    }

	    if (infoPtr->eventMask & event) {
		(*infoPtr->proc) (infoPtr->data, streamID, event);
	    }

	    streamID = Bit_FindFirstSet(maxPossNumStreams, tempWriteMask);
	}

	streamID = Bit_FindFirstSet(maxPossNumStreams, tempExceptMask);
	while (streamID != -1) {
	    if (streamID < 0 || streamID > MAX_NUM_STREAMS) {
		panic("Fs_Dispatch: stream ID %d out of range\n", streamID);
	    }

	    infoPtr = &(infoArray[streamID]);
	    Bit_Clear(streamID, tempExceptMask);

	    if (infoPtr->eventMask & FS_EXCEPTION) {
		(*infoPtr->proc) (infoPtr->data, streamID, FS_EXCEPTION);
	    }

	    streamID = Bit_FindFirstSet(maxPossNumStreams, tempExceptMask);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Fs_EventHandlerCreate --
 *
 *	Save the handler routine and data for a stream so it can be
 *	called when the stream becomes ready in the main dispatch loop.
 *	The handler "proc" should be declared as follows:
 *
 *	void
 *	proc(clientData, streamID, eventMask)
 *	    ClientData	clientData;
 *	    int		streamID;
 *	    int		eventMask;
 *	{
 *	}
 *
 *	The association between a handler and a stream can be destroyed 
 *	by calling Fs_EventHandlerDestroy.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The infoArray and select masks are updated so events on the
 *	stream will cause the proc routine to be called.
 *
 *----------------------------------------------------------------------
 */

void
Fs_EventHandlerCreate(streamID, eventMask, proc, clientData)
    register int streamID;		/* Stream to watch. */
    register int eventMask;		/* Events to watch for:  must be an
					 * OR'ed combination of FS_READABLE,
					 * FS_WRITABLE, or FS_EXCEPTION. */
    void	(*proc)();		/* Procedure to call when an event in
					 * eventMask occurs for streamID. */
    ClientData	clientData;		/* Value to pass to proc. */
{
    if (!initialized) {
	DispatcherInit();
    }

    if (streamID < 0 || streamID > MAX_NUM_STREAMS) {
	panic("Fs_EventHandlerCreate: stream ID %d out of range\n", streamID);
    } 

    if ((eventMask & (FS_READABLE|FS_WRITABLE|FS_EXCEPTION)) == 0) {
	panic("Fs_EventHandlerCreate: bad eventMask %x for stream %d\n", 
		eventMask, streamID);
    }

    if (infoArray[streamID].inUse) {
	panic("Fs_EventHandlerCreate: stream ID %d already has a handler (0x%x)\n", 
		streamID, infoArray[streamID].proc);
    } 
    infoArray[streamID].inUse = TRUE;
    infoArray[streamID].proc = proc;
    infoArray[streamID].data = clientData;
    infoArray[streamID].eventMask = eventMask;

    if (eventMask & FS_READABLE) {
	Bit_Set(streamID, readMask);
    }
    if (eventMask & FS_WRITABLE) {
	Bit_Set(streamID, writeMask);
    }
    if (eventMask & FS_EXCEPTION) {
	Bit_Set(streamID, exceptMask);
    }

    /*
     * Calculate the highest stream ID that's in use and add 1 to convert
     * it into the # of bits in active use in the select masks.
     */
    if (streamID >= maxPossNumStreams) {
	maxPossNumStreams = streamID + 1;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Fs_EventHandlerData --
 *
 *	Given an event ID for a stream, return the client data associated
 *	with the event.
 *
 * Results:
 *	The client data associated with the event.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ClientData
Fs_EventHandlerData(streamID)
    register int streamID;
{
    if (!initialized) {
	panic("Fs_EventHandlerData: not initialized yet.\n");
    }

    if (streamID < 0 || streamID > MAX_NUM_STREAMS) {
	panic("Fs_EventHandlerData: stream ID %d out of range\n", streamID);
    } 

    if (!infoArray[streamID].inUse) {
	panic("Fs_EventHandlerData: stream ID %d not in use\n", streamID);
    }

    return(infoArray[streamID].data);
}


/*
 *----------------------------------------------------------------------
 *
 * Fs_EventHandlerChangeData --
 *
 *	Given an event ID for a stream, return the client data associated
 *	with the event.
 *
 * Results:
 *	The previous value of the client data associated with the event.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ClientData
Fs_EventHandlerChangeData(streamID, newData)
    register int streamID;
    ClientData	 newData;
{
    ClientData	 oldData;

    if (!initialized) {
	panic("Fs_EventHandlerChangeData: not initialized yet.\n");
    }

    if (streamID < 0 || streamID > MAX_NUM_STREAMS) {
	panic("Fs_EventHandlerChangeData: stream ID %d out of range\n", streamID);
    } 

    if (!infoArray[streamID].inUse) {
	panic("Fs_EventHandlerChangeData: stream ID %d not in use\n", streamID);
    }

    oldData = infoArray[streamID].data;
    infoArray[streamID].data = newData;
    return(oldData);
}


/*
 *----------------------------------------------------------------------
 *
 * Fs_EventHandlerDestroy --
 *
 *	Destroy the event handler routine for a stream.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The infoArray and select masks are updated so events on
 *	streamID will be ignored.
 *
 *----------------------------------------------------------------------
 */

void
Fs_EventHandlerDestroy(streamID)
    register int streamID;
{
    register int eventMask;

    if (!initialized) {
	panic("Fs_EventHandlerDestroy: not initialized yet.\n");
    }

    if (streamID < 0 || streamID > MAX_NUM_STREAMS) {
	panic("Fs_EventHandlerDestroy: stream ID %d out of range\n", streamID);
    } 

    infoArray[streamID].inUse = FALSE;
    eventMask = infoArray[streamID].eventMask;
    infoArray[streamID].eventMask = 0;

    if (eventMask & FS_READABLE) {
	Bit_Clear(streamID, readMask);
    }
    if (eventMask & FS_WRITABLE) {
	Bit_Clear(streamID, writeMask);
    }
    if (eventMask & FS_EXCEPTION) {
	Bit_Clear(streamID, exceptMask);
    }

    /*
     * Adjust maxPossNumStreams if this stream was the highest one in use.
     */
    if (streamID == (maxPossNumStreams - 1)) {
	do {
	    maxPossNumStreams--;
	} while ((maxPossNumStreams >= 1) && 
			(!infoArray[maxPossNumStreams - 1].inUse));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Fs_TimeoutHandlerCreate --
 *
 *	Schedules a routine to be called at a certain time by adding 
 *	it to the timer queue. 
 *
 *	When the client routine is called at its scheduled time, it is 
 *	passed two parameters:
 *	 a) the clientData argument passed into this routine, and
 *	 b) the time it is scheduled to be called at.
 *	Hence the routine should be declared as:
 *
 *	    void
 *	    proc(clientData, time)
 *		ClientData clientData;
 *		Time time;
 *	    {}
 *
 *	The time a routine should be called at can be specified in two
 *	ways: an absolute time (e.g. 12:00 4 July 1776) or an interval.
 *	For example, to have proc called in 1 hour from now and every hour 
 *	after that, the call to Fs_TimeoutHandlerCreate is:
 *
 *	    Fs_TimeoutHandlerCreate(time_OneHour, TRUE, proc, clientData);
 *
 *      The 2nd argument (TRUE) to Fs_TimeoutHandlerCreate means the
 *      routine will be called at the interval + the current time.
 *      Routines scheduled with an interval will automatically be
 *      rescheduled after they are called. They should deschedule themselves 
 *      if they are not to be called more than once. Routines scheduled to
 *      run at an absolute time will be called just once and must
 *      reschedule themselves explicitly.
 *
 *	No error checking is done to make sure that a (proc, clientData) pair
 *	is not put on the list more than once.
 *
 *
 * Results:
 *	A token that can be used to destroy the handler with 
 *	Fs_TimeoutHandlerDestroy.
 *
 * Side effects:
 *	The timeout queue is extended.
 *
 *----------------------------------------------------------------------
 */

Fs_TimeoutHandler
Fs_TimeoutHandlerCreate(time, relativeTime, proc, clientData)
    Time 	time;			/* See comments above. */
    Boolean	relativeTime;
    void	(*proc)();		/* Procedure to call. */
    ClientData	clientData;		/* Value to pass to proc. */
{
    register TimeoutInfo *newPtr;
    TimeoutInfo *itemPtr;
    Boolean inserted;		/* TRUE if added to queue in FORALL loop. */

    if (!initialized) {
	DispatcherInit();
    }

    if (time.seconds == 0) {
	/*
	 * Make sure the timeout value isn't too small so that we don't
	 * spend all our time calling timeout handlers.
	 */
	if (time.microseconds < MIN_TIMEOUT) {
	    time.microseconds = MIN_TIMEOUT;
	}
    }

    newPtr = (TimeoutInfo *) malloc(sizeof(TimeoutInfo));

    if (relativeTime) {
	/*
	 * Convert the interval into an absolute time by adding the 
	 * interval to the current time.
	 */
	Time	curTime;

	(void) gettimeofday((struct timeval *) &curTime,
		(struct timezone *) NULL);
	Time_Add(curTime, time, &(newPtr->time));

	newPtr->interval = time;
	newPtr->resched = TRUE;
    } else {
	newPtr->time = time;
	newPtr->resched = FALSE;
    }
    newPtr->proc = proc;
    newPtr->data = clientData;

    /*
     *  Go through the timer queue and insert the new routine.  The queue
     *  is ordered by the time field in the element.  The sooner the
     *  routine needs to be called, the closer it is to the front of the
     *  queue.  The new routine will not be added to the queue inside the
     *  FOR loop if its scheduled time is after all elements in the queue
     *  or the queue is empty.  It will be added after the last element in
     *  the queue.
     */

    inserted = FALSE;  /* assume new element not inserted inside FOR loop.*/
    List_InitElement((List_Links *) newPtr);
    LIST_FORALL(&timeoutList, (List_Links *) itemPtr) {

       if (Time_LT(newPtr->time, itemPtr->time)) {
	    List_Insert((List_Links *) newPtr, LIST_BEFORE(itemPtr));
	    inserted = TRUE;
	    break;
	}
    }
    if (!inserted) {
	List_Insert((List_Links *) newPtr, LIST_ATREAR(&timeoutList));
    }

    return((Fs_TimeoutHandler) newPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Fs_TimeoutHandlerDestroy --
 *
 *	Deschedules a routine that was to be called at a certain time 
 *	by removing it from the timer queue.
 *
 *	It is not an error if the handler is not on the queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The timer queue structure is updated. 
 *
 *----------------------------------------------------------------------
 */

void
Fs_TimeoutHandlerDestroy(token)
    Fs_TimeoutHandler token;
{
    register List_Links *itemPtr;
    TimeoutInfo *infoPtr = (TimeoutInfo *) token;

    if (!initialized) {
	panic("Fs_TimeoutHandlerDestroy: not initialized yet.\n");
    }

    if (infoPtr == (TimeoutInfo *) NULL) {
	return;
    }

    LIST_FORALL(&timeoutList, itemPtr) {

	if ((List_Links *) infoPtr == itemPtr) {
	    List_Remove(itemPtr);
	    break;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CallTimeoutHandler --
 *
 *	Go through the timer queue and call the routines that are
 *	scheduled to be called now.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The called routine may cause side effects.
 *
 *----------------------------------------------------------------------
 */

static void
CallTimeoutHandler()
{
    register TimeoutInfo *readyPtr;	/* Ptr to handler that's ready to 
					 * be called. */

    register TimeoutInfo *itemPtr;	/* Used to examine timeout queue. */
#define itemListPtr	((List_Links *) itemPtr)

    Time curTime;

    /*
     *  The callback timer has expired. This means at least the first
     *  routine on the timer queue is ready to be called.  Go through
     *  the queue and call all routines that are scheduled to be
     *  called. Since the queue is ordered by time, we can quit looking 
     *  when we find the first routine that does not need to be called.
     */

    if (!List_IsEmpty(&timeoutList)) {

	(void) gettimeofday((struct timeval *) &curTime,
		(struct timezone *) NULL);

	itemListPtr = List_First(&timeoutList); 
	while (!List_IsAtEnd(&timeoutList, itemListPtr)) {
	    if (Time_GT(itemPtr->time, curTime)) {
		/*
		 * The first routine is not ready yet so all the other
		 * routines aren't ready either. We are done for now.
		 */
		 
		break;

	    } else {

		/*
		 * First remove the item before calling it. This allows 
		 * the routine to call Fs_TimeoutHandlerDestroy or
		 * Fs_TimeoutHandlerCreate without messing up the 
		 * list pointers.
		 */

		(List_Links *) readyPtr = itemListPtr;
		itemListPtr  = List_Next(itemListPtr);
		if (itemListPtr == NULL) {
		    panic(
		    "(FsDispatch)CallTimeoutHandler: next item == NULL\n");
		}
		List_Remove((List_Links *) readyPtr);


		if (readyPtr->proc == NULL) {
		    panic("(FsDispatch)CallTimeoutHandler: routine == NULL\n");
		} else {
		    (readyPtr->proc) (readyPtr->data, readyPtr->time);
		}

		/*
		 * If the routine is to be called again automatically,
		 * compute the next time to run and insert it back in the
		 * queue. Otherwise, free the element because it isn't 
		 * needed any more.
		 */

		if (!(readyPtr->resched)) {
		    free((char *) readyPtr);
		} else{
		    List_Links	*tempPtr;

		    Time_Add(curTime, readyPtr->interval, &(readyPtr->time));

		    tempPtr = List_First(&timeoutList);
		    while (!List_IsAtEnd((&timeoutList), tempPtr)) {
			if (Time_GT( ((TimeoutInfo *)tempPtr)->time, 
					readyPtr->time)) {
			    break;
			}
			tempPtr = List_Next(tempPtr);
		    }
		    List_InitElement((List_Links *) readyPtr);
		    List_Insert((List_Links *) readyPtr, LIST_BEFORE(tempPtr));
		}
	    }
	} /* while */
    } 
}


/*
 *----------------------------------------------------------------------
 *
 * ComputeTimeoutValue --
 *
 *	Compute when the first routine on the timeout queue needs to be 
 *	called. The value returned in *timeoutPtr is the amount time
 *	that must past before the first routine is called. If a routine
 *	needs to be called now, we return TRUE, otherwise a return value
 *	of FALSE means nothing's ready to be run yet.
 *
 * Results:
 *	TRUE		- routine(s) on the timeout queue need to be run now.
 *	FALSE		- nothing needs to be run yet.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Boolean
ComputeTimeoutValue(timeoutPtr)
    Time	*timeoutPtr;
{
    Time	firstTime;
    Time	curTime;

    if (List_IsEmpty(&timeoutList)) {
	/*
	 * Nothing on the timeout queue so return a very long timeout value.
	 */

	*timeoutPtr = forever;
	return(FALSE);

    } else {
	(void) gettimeofday((struct timeval *) &curTime,
		(struct timezone *) NULL);
	firstTime = ((TimeoutInfo *) (List_Next(&timeoutList)))->time;

	if (Time_LE(firstTime, curTime)) {
	    /*
	     * The callback time of the first routine has already past.
	     */

	    return(TRUE);
	} else {
	    /*
	     * The callback time of the first routine is in the future.
	     */

	    Time_Subtract(firstTime, curTime, timeoutPtr);
	    return(FALSE);
	}
    }
}
