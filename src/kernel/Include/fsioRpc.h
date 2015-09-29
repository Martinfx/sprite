/*
 * fsioRpc.h --
 *
 *	Declarations of RPC calls for the fsio module.
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
 * $Header: /sprite/src/kernel/Cvsroot/kernel/fsio/fsioRpc.h,v 1.2 92/10/28 18:31:38 mgbaker Exp $ SPRITE (Berkeley)
 */

#ifndef _FSIORPC
#define _FSIORPC

#include <rpc.h>

/* procedures */

extern ReturnStatus Fsio_RpcStreamMigCloseNew _ARGS_((ClientData srvToken, 
		int clientID, int command, Rpc_Storage *storagePtr));

#endif /* _FSIORPC */

