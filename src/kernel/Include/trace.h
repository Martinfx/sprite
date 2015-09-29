/*
 * trace.h --
 *
 *	Definitions for the generalized tracing facility.
 *
 * Copyright 1991 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that this copyright
 * notice appears in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * $Header: /sprite/src/kernel/Cvsroot/kernel/utils/trace.h,v 9.2 91/06/27 12:16:03 shirriff Exp $ SPRITE (Berkeley)
 */

#ifndef _TRACE
#define _TRACE

#include "spriteTime.h"

/*
 * Information applicable to an entire circular buffer.
 */

typedef struct Trace_Header {
    int numRecords;		/* the number of records in the buffer */
    int currentRecord;		/* which entry is the next to be written */
    int flags;			/* TRACE_INHIBIT, TRACE_NO_TIMES */
    int dataSize;		/* the size of client-specific trace
				 * information corresponding to each record */
    struct Trace_Record *recordArray;  /* pointer to array of trace records */
} Trace_Header;

/*
 * Trace Header Flags:
 *
 *	TRACE_INHIBIT		- Inhibit taking traces, used internally.
 *	TRACE_NO_TIMES		- Don't take time stamps, faster.
 */

#define TRACE_INHIBIT		0x01
#define TRACE_NO_TIMES		0x02

/*
 * Information stored per-record.
 */

typedef struct Trace_Record {
    Time time;			/* time record was last modified */
    int flags;			/* flags used by the Trace module */
    int event;			/* event being traced */
    ClientData *traceData;	/* pointer into corresponding client-specific
				 * record, if it exists */
} Trace_Record;

/*
 * Record Flags:
 *
 *	TRACE_DATA_VALID	- All data for this record is valid.
 *	TRACE_UNUSED		- Indicates that a record is not used yet.
 *	TRACE_DATA_INVALID	- The traceData area has been zeroed because
 *				  the caller passed a NIL pointer for data.
 */

#define TRACE_DATA_VALID	0x00
#define TRACE_UNUSED 		0x01
#define TRACE_DATA_INVALID	0x10

extern void		Trace_Init _ARGS_((Trace_Header *traceHdrPtr,
					   int numRecords, int size,
					   int flags));
extern void		Trace_Insert _ARGS_((Trace_Header *traceHdrPtr,
					     int event, ClientData data));
extern ReturnStatus	Trace_Dump _ARGS_((Trace_Header *traceHdrPtr,
					   int numRecs, Address addr));
extern ReturnStatus	Trace_Print _ARGS_((Trace_Header *traceHdrPtr,
					    int numRecs, int (*printProc)()));

#endif /* _TRACE */
