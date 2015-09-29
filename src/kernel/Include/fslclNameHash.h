/* fslclNameHash.h --
 *
 *	Definitions for the filesystem name hash table.
 *
 * Copyright 1990 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * $Header: /sprite/src/kernel/Cvsroot/kernel/fslcl/fslclNameHash.h,v 9.2 90/10/08 15:39:26 mendel Exp $ SPRITE (Berkeley)
 */


#ifndef	_FSLCLNAMEHASH
#define	_FSLCLNAMEHASH

#include <list.h>

/* 
 * The hash table includes an array of bucket list headers,
 * and some administrative information that affects the hash
 * function and comparison functions.  The table is contrained to
 * have a power of two number of entries to make the hashing go faster.
 */
typedef struct FslclHashTable {
    struct FslclHashBucket *table;	/* Pointer to array of List_Links. */
    List_Links		lruList;	/* The header of the LRU list */
    int 		size;		/* Actual size of array. */
    int 		numEntries;	/* Number of entries in the table. */
    int 		downShift;	/* Shift count, used in hashing 
					 * function. */
    int 		mask;		/* Used to select bits for hashing. */
} FslclHashTable;

/*
 * Default size of the name hash table.
 */
extern fslclNameHashSize;
#define FSLCL_NAME_HASH_SIZE	512

/*
 * The bucket header is just a list header.
 */

typedef struct FslclHashBucket {
    List_Links	list;
} FslclHashBucket;

/* 
 * The following defines one entry in the hash table. 
 */

typedef struct FslclHashEntry {
    List_Links	links;		/* For the list starting at the bucket header */
    FslclHashBucket *bucketPtr;	/* Pointer to hash bucket for this entry */
    struct FsLruList {
	List_Links links;	/* Links for the LRU list */
	struct FslclHashEntry *entryPtr;	/* Back pointer needed to get entry */
    } lru;
    Fs_HandleHeader *hdrPtr;	/* Pointer to handle of named component. */
    Fs_HandleHeader *keyHdrPtr;	/* Pointer to handle of parent directory. */
    char 	keyName[4];	/* Text name of this entry.  Note: the
				 * actual size may be longer if necessary
				 * to hold the whole string. This MUST be
				 * the last entry in the structure!!! */
} FslclHashEntry;

typedef struct FslclLruEntry {
    List_Links	lruList;	/* This record is used to map from the LRU */
    FslclHashBucket *entryPtr;	/* List back to the hash entry */
} FslclLruEntry;

/*
 * The following procedure declarations and macros
 * are the only things that should be needed outside
 * the implementation code.
 */

extern FslclHashTable	fslclNameTable;
extern FslclHashTable	*fslclNameTablePtr;
extern Boolean		fslclNameCaching;

extern void FslclNameHashStats _ARGS_((void));
extern FslclHashEntry *FslclHashLookOnly _ARGS_((FslclHashTable *table,
			char *string, Fs_HandleHeader *keyHdrPtr));
extern void FslclHashDelete _ARGS_((FslclHashTable *table, char *string, 
			Fs_HandleHeader *keyHdrPtr));
extern FslclHashEntry *FslclHashInsert _ARGS_((FslclHashTable *table, 
			char *string, Fs_HandleHeader *keyHdrPtr, 
			Fs_HandleHeader *hdrPtr));

#define FSLCL_HASH_LOOK_ONLY(table, string, keyHandle) \
    (fslclNameCaching ? \
	FslclHashLookOnly(table, string, (Fs_HandleHeader *)keyHandle) : \
	(FslclHashEntry *)NIL)

#define FSLCL_HASH_INSERT(table, string, keyHandle, handle) \
    if (fslclNameCaching) { \
	(void)FslclHashInsert(table, string, (Fs_HandleHeader *)keyHandle, \
			   (Fs_HandleHeader *)handle); \
    }

#define FSLCL_HASH_DELETE(table, string, keyHandle) \
    if (fslclNameCaching) { \
	FslclHashDelete(table, string, (Fs_HandleHeader *)keyHandle); \
    }

#endif /* _FSLCLNAMEHASH */
