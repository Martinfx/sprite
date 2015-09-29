/*
 * lfsStats.h --
 *
 *	Declarations of Lfs stats structure.
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
 * $Header: /sprite/src/kernel/Cvsroot/kernel/lfs/lfsStats.h,v 1.5 92/03/06 11:56:58 mgbaker Exp $ SPRITE (Berkeley)
 */

#ifndef _LFS_STATS
#define _LFS_STATS

/* constants */

#define	LFS_STATS_VERSION 1

typedef struct LfsStatsCounter {
	unsigned int high;	/* High 32 bits. */
	unsigned int low;	/* Low 32 bits. */
} LfsStatsCounter; 

#define LFS_STATS_MAX_SIZE	4096
#define	LFS_STATS_CDIST_BUCKETS	64
#ifdef LFS_STATS_COLLECT
#define	LFS_STATS_INC(counter)	((++(counter).low == 0) ? (counter).high++ : 0)
#define	LFS_STATS_ADD(counter, count)	\
	((((counter).low + (count) < (counter).low) ? (counter).high++ : 0),\
		((counter).low = (counter).low + (count)))
#else
#define	LFS_STATS_INC(counter)	
#define	LFS_STATS_ADD(counter, count)
#endif
/* data structures */
#define LFSCOUNT LfsStatsCounter
typedef struct Lfs_StatsVersion1 {
    int	version;	/* Version number of stats structure. */
    int	size;		/* Size of this structure in bytes. */
    /*
     * Segmented log counts.
     */
    struct LfsLogStats {
	LFSCOUNT segWrites;     /* Number of segmetns writes. */
	LFSCOUNT partialWrites; /* Number of non-full segment writes. */
	LFSCOUNT emptyWrites;   /* Number of empty segment writes. */
	LFSCOUNT blocksWritten; /* Number of blocks written to the log.*/
	LFSCOUNT bytesWritten;   /* Number of active bytes written to the log.*/
	LFSCOUNT summaryBlocksWritten; /* Number of summary blocks written to 
				       * disk. */
	LFSCOUNT summaryBytesWritten; /* Number of summary bytes written. */
	LFSCOUNT wasteBlocks;      /* Number of waste blocks. */
	LFSCOUNT newSegments;      /* Number of new segments requests. */
	LFSCOUNT cleanSegWait;     /* Number of waits for a clean segment. */
	LFSCOUNT useOldSegment;    /* Number of requests that could use old 
			            * segment. */
	LFSCOUNT locks;	      /* Number of log was locked. */
	LFSCOUNT lockWaits;   /* Number time we waited to lock log. */

	/*
	 * The following are for ASPLOS measurements only, and can be removed
	 * after that's all over.  Remember to put back the padding entries and
	 * zero stuff.  Mary  2/14/92
	 */
	LFSCOUNT fsyncWrites;		/* Seg. writes due to fsync. */
	LFSCOUNT fsyncPartialWrites;	/* Partial seg. writes due to fsync. */
	LFSCOUNT fsyncBytes;		/* Bytes fsynced. */
	LFSCOUNT fsyncPartialBytes;	/* Bytes fsynced to partial segs. */
	LFSCOUNT partialWriteBytes;	/* Bytes to disk in partial segs. */
	LFSCOUNT cleanPartialWriteBytes;/* Partial bytes written in cleaning. */
	LFSCOUNT fileBytesWritten;	/* File bytes written to disk. */
	LFSCOUNT cleanFileBytesWritten;	/* File bytes written in cleaning. */
	LFSCOUNT partialFileBytes;	/* File bytes written to partial seg. */

	LFSCOUNT padding[7];
    } log;
	/*
	 * Checkpoint related counters
	 */
    struct LfsCheckPointStats {
	LFSCOUNT count;	   /* Number of checkpoints performed. */
	LFSCOUNT segWrites;	 /* Number of seg writes during checkpoints. */
	LFSCOUNT blocksWritten; /* Number of log writes during checkpoints. */
	LFSCOUNT bytesWritten; /* Number of bytes written during 
				* checkpoints. */
	LFSCOUNT totalBlocks; /* Total blocks written to checkpoint area. */
	LFSCOUNT totalBytes;  /* Total bytes written to checkpoint area. */
	LFSCOUNT samples;     /* Stable mem locality samples. */
	LFSCOUNT padding[15];
    } checkpoint;


    struct LfsLogCleanStats {
	LFSCOUNT startRequests;	/* Number of start cleaning calls. */
	LFSCOUNT alreadyActive;	/* Number of start calls with cleaning 
				 * already active. */
	LFSCOUNT getSegsRequests; /* Number of request to LfsGetSegsToClean */
	LFSCOUNT segsToClean;		/* Number of segs returned to clean. */
	LFSCOUNT numSegsToClean;	/* Number of segments return. */
	LFSCOUNT segReads;		/* Number of segment read in. */
	LFSCOUNT readErrors;	        /* Number we got errors reading in. */
	LFSCOUNT readEmpty;		/* Number we found to be empty. */
	LFSCOUNT bytesCleaned;		/* Number of active bytes cleaned. */
	LFSCOUNT cacheBlocksUsed;	/* Number of cache blocks used. */
	LFSCOUNT segWrites;	   /* Number of seg writes during cleaning. */
	LFSCOUNT blocksWritten; /* Number of log writes during cleaning. */
	LFSCOUNT bytesWritten;      /* Number of bytes written during 
				* cleaning. */
	LFSCOUNT summaryBlocksRead;	/* Number of summary blocks read. */

	LFSCOUNT padding[16];
    } cleaning;

    struct LfsBlockIOStats {
	LFSCOUNT reads;	/* Number of block read. */
	LFSCOUNT bytesReads;	/* Number of bytes read. */
	LFSCOUNT allocs;	    /* Calls to Lfs_BlockAllocate. */
	LFSCOUNT fastAllocs;	    /* Number fast allocs. */
	LFSCOUNT slowAllocs;	    /* Number of slow allocs. */
	LFSCOUNT slowAllocFails; /* Number of slow allocs that failed. */
	LFSCOUNT totalBytesRead; /* Total bytes read. */
	LFSCOUNT totalBytesWritten; /* Total number of bytes written. */
	LFSCOUNT segCacheHits;	    /* Reads that hit in the seg cache. */
	LFSCOUNT padding[15];
    } blockio;

    struct LfsDescStats {
	LFSCOUNT fetches;	/* Number of descriptor fetches. */
	LFSCOUNT goodFetch;  /* Number of fetches that succed. */
	LFSCOUNT fetchCacheMiss; /* Fetches that missed in the cache. */
	LFSCOUNT fetchSearched;  /* Number of descriptors searched during ftetch. */
	LFSCOUNT stores;	/* Number of descriptor stores. */
	LFSCOUNT freeStores;	/* Number of stores of free descriptors. */
	LFSCOUNT accessTimeUpdate; /* Number of access time updates. */
	LFSCOUNT dirtyList;	      /* File put on dirty list. */
	LFSCOUNT truncs;	/* Descriptor truncates. */
	LFSCOUNT truncSizeZero;	/* Truncate to size zero. */
	LFSCOUNT delete;	/* Truncate with delete. */
	LFSCOUNT inits;	/* Descriptor init calls. */
	LFSCOUNT getNewFileNumber; /* Number of calls for getNewFileNumber */
	LFSCOUNT scans;	      /* Scans during getNewFileNumber */
	LFSCOUNT free;	      /* Number of descriptor frees. */
	LFSCOUNT mapBlocksWritten; /* Descriptor map blocks written. */
	LFSCOUNT mapBlockCleaned;  /* Map blocks cleaned. */
	LFSCOUNT descMoved;	      /* Number of descriptor that move. */
	LFSCOUNT descMapBlockAccess;  /* Accesses to desc map blocks. */
	LFSCOUNT descMapBlockMiss;    /* Accesses that missed in cache. */
	LFSCOUNT residentCount;	      /* Count of resident blocks at cp. */
	LFSCOUNT cleaningFetch;	      /* Fetching during cleaning. */
	LFSCOUNT cleaningFetchMiss;   /* Miss during cleaning. */
	LFSCOUNT padding[11];
    } desc;
    struct LfsIndexStats {
	LFSCOUNT    get;	/* Get file index count. */
	LFSCOUNT    set;    /* Set file index count. */
	LFSCOUNT getFetchBlock; /* Number of indirect blocks fetched by get.*/
	LFSCOUNT setFetchBlock; /* Number of indirect blocks fetched by set. */
	LFSCOUNT growFetchBlock; /* Number of indirect blocks fetched by grow */
	LFSCOUNT getFetchHit; /* Number of indirect blocks found in cache for 
			      * get. */
	LFSCOUNT setFetchHit; /* Number of indirect blocks found in cache for
			      * set. */
	LFSCOUNT truncs; /* Truncate index count. */
	LFSCOUNT deleteFetchBlock; /* Fetching a cache block for delete. */
	LFSCOUNT deleteFetchBlockMiss; /* Reading a cache block for delete. */
	LFSCOUNT getCleaningFetchBlock; /* Fetch an index block for cleaning. */
	LFSCOUNT getCleaningFetchHit;   /* Hit on an index block for cleaning.*/
	LFSCOUNT padding[14];
    } index;

    struct LfsFileLayoutStats {
	LFSCOUNT calls;	/* Calls to LfsFileLayoutProc. */
	LFSCOUNT dirtyFiles; /* Number of dirty files fetched. */
	LFSCOUNT dirtyBlocks; /* Number of dirty blocks fetched. */
	LFSCOUNT dirtyBlocksReturned; /* Number of dirty blocks returned. */
	LFSCOUNT filledRegion; /* Number of times we filled a region. */
	LFSCOUNT segWrites;	  /* Number writes of file data. */
	LFSCOUNT cacheBlocksWritten;	  /* Number of cache block written. */
	LFSCOUNT descBlockWritten;    /* Number of descriptor blocks written. */
	LFSCOUNT descWritten;	  /* Number of descriptor written. */
	LFSCOUNT filesWritten;	  /* Number of files written. */
	LFSCOUNT cleanings;	  /* Number of file layout cleans. */
	LFSCOUNT descBlocksCleaned; /* Number of descriptor blocks we had to 
				 * clean. */
	LFSCOUNT descCleaned; /* Number of descriptors cleaned. */
	LFSCOUNT descCopied;	/* Number of descriptors copied during cleaning. */
	LFSCOUNT fileCleaned; /* Number of files encounted during cleaning. */
	LFSCOUNT fileVersionOk; /* Number of files encounted during cleaning that
			    * could be alive by version number ok. */
	LFSCOUNT blocksCleaned; /* Number of blocks encounted during 
				* cleaning. */
	LFSCOUNT blocksCopied;  /* Number of blocks copied during cleaning. */
	LFSCOUNT blocksCopiedHit; /* Number of blocks founding in cache 
			      * during cleaning. */
	LFSCOUNT cleanNoHandle;	/* Files deleted while being cleaned. */
	LFSCOUNT cleanLockedHandle; /* File cleaning failed due to locked 
				     * handle. */
	/*
	 * These next stats are for ASPLOS paper and can be removed and
	 * zeroed after that's done.   Mary 2/15/92.
	 */
	LFSCOUNT descLayoutBytes;	/* Descriptor bytes in layout proc. */

	LFSCOUNT padding[13];
    } layout;

    struct LfsSegUsageStats {
	LFSCOUNT blocksFreed; 
	LFSCOUNT bytesFreed;
	LFSCOUNT usageSet;
	LFSCOUNT blocksWritten;
	LFSCOUNT blocksCleaned;
	LFSCOUNT segUsageBlockAccess;  /* Accesses to usage array blocks. */
	LFSCOUNT segUsageBlockMiss;    /* Accesses that missed in cache. */
	LFSCOUNT residentCount;	      /* Count of resident blocks at cp. */
	LFSCOUNT padding[13];
    } segusage;

    struct LfsCacheBackendStats {
	LFSCOUNT startRequests;	/* Number of start write-back calls. */
	LFSCOUNT alreadyActive;	/* Number of start calls with write-back 
				 * already active. */
	LFSCOUNT padding[16];
    } backend;

    struct LfsDirLogStats {
	LFSCOUNT entryAllocNew;	  /* Number of calls to LfsDirLogEntryAlloc for
				   * new entry. */
	LFSCOUNT entryAllocOld;	  /* Number of calls to LfsDirLogEntryAlloc for
				   * old entry. */
	LFSCOUNT entryAllocFound; /* Number of calls to LfsDirLogEntryAlloc that
				   * found their entry. */
	LFSCOUNT entryAllocWaits; /* Number of waits in LfsDirLogEntryAlloc. */
	LFSCOUNT newLogBlock; /* Number of new block blocks fetched. */
	LFSCOUNT fastFindFail; 
	LFSCOUNT findEntrySearch;
	LFSCOUNT dataBlockWritten;
	LFSCOUNT blockWritten;
	LFSCOUNT bytesWritten;
				    /* This next is for ASPLOS only.
				     * Remove when done.  -Mary 2/15/92.  */
	LFSCOUNT cleaningBytesWritten;	/* Bytes written during cleaning. */
	LFSCOUNT padding[1];
    } dirlog;
    unsigned int cleaningDist[LFS_STATS_CDIST_BUCKETS];
} Lfs_StatsVersion1;
#undef LFSCOUNT

typedef Lfs_StatsVersion1 Lfs_Stats;

#endif /* _LFS_STATS */

