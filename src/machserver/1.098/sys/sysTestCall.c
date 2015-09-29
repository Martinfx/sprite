/* 
 * sysTestCall.c --
 *
 *	These routines do direct console I/O.
 *	These routines are so close to obsolete they should be removed.
 *	However, I tried that and it broke initsprite.
 *
 * Copyright (C) 1985 Regents of the University of California
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/kernel/sys/RCS/sysTestCall.c,v 9.5 91/03/30 17:21:16 mendel Exp $ SPRITE (Berkeley)";
#endif not lint

#include <sprite.h>
#include <sysSysCall.h>
#include <sysTestCall.h>
#include <sys.h>
#include <dbg.h>
#include <vm.h>
#include <machMon.h>
#include <stdio.h>

struct test_args {
    int argArray[SYS_MAX_ARGS];
};


/*
 * ----------------------------------------------------------------------------
 *
 * Test_PrintOut --
 *
 *      Does a printf that will work during boot before everything
 *	else gets initialized.  This is used only by initsprite, as far
 *	as I can tell.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Outputs data.
 *
 * ----------------------------------------------------------------------------
 */

int 
Test_PrintOut(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9)
    int arg0; 
    int arg1; 
    int arg2; 
    int arg3; 
    int arg4; 
    int arg5; 
    int arg6;
    int arg7; 
    int arg8; 
    int arg9;
{
    struct test_args args;
    struct {
	int 	arg;
	int 	mapped;
	int	numBytes;
    } nargs[SYS_MAX_ARGS];
    int		len;
    int		i;
    char	*string;

    args.argArray[0] = arg0;
    args.argArray[1] = arg1;
    args.argArray[2] = arg2;
    args.argArray[3] = arg3;
    args.argArray[4] = arg4;
    args.argArray[5] = arg5;
    args.argArray[6] = arg6;
    args.argArray[7] = arg7;
    args.argArray[8] = arg8;
    args.argArray[9] = arg9;

    Vm_MakeAccessible(VM_READONLY_ACCESS, 1024, (Address) (args.argArray[0]), 
					  &len, (Address *) &(nargs[0].arg));
    if (len == 0) {
	return(FAILURE);
    }
    nargs[0].numBytes = len;

    nargs[0].mapped = TRUE;
    for (i = 1; i < SYS_MAX_ARGS; i++) {
	nargs[i].mapped = FALSE;
    }
    string = (char *) nargs[0].arg;
    i = 1;
    while (*string != '\0') {
	if (*string != '%') {
	    string++;
	    continue;
	}
	string++;
	if (*string == 's') {
	    Vm_MakeAccessible(VM_READONLY_ACCESS, 1024,
	      		      (Address) args.argArray[i], &len,
			      (Address *) &(nargs[i].arg));
	    if (len == 0) {
		for (i = 0; i < SYS_MAX_ARGS; i++) {
		    if (nargs[i].mapped) {
			Vm_MakeUnaccessible((Address) (nargs[i].arg),
				nargs[i].numBytes);
		    }
		}
		return(FAILURE);
	    }

	    nargs[i].mapped = TRUE;
	    nargs[i].numBytes = len;
	} else {
	    nargs[i].arg = args.argArray[i];
	}
	string++;
	i++;
    }
    printf((char *) nargs[0].arg, nargs[1].arg, nargs[2].arg,
	    nargs[3].arg, nargs[4].arg, nargs[5].arg, nargs[6].arg,
	    nargs[7].arg, nargs[8].arg, nargs[9].arg);
    for (i = 0; i < SYS_MAX_ARGS; i++) {
	if (nargs[i].mapped) {
	    Vm_MakeUnaccessible((Address) (nargs[i].arg), nargs[i].numBytes);
	}
    }
    return(0);
}

/*
 * ----------------------------------------------------------------------------
 *
 * Test_GetLine --
 *
 *      Gets data from console.  I'm not sure this is actually used.
 *
 * Results:
 *      The string.
 *
 * Side effects:
 *      Gets a string.
 *
 * ----------------------------------------------------------------------------
 */
int 
Test_GetLine(string, length)
    char	*string;
    int		length;
{
#ifdef sun4c
    printf("Test_GetLine() doesn`t work on sun4c\n");
    return (FAILURE);
#else
    int		i, numBytes;
    char 	*realString;

    printf("Obsolete Test_GetLine() called\n");
    Vm_MakeAccessible(VM_OVERWRITE_ACCESS, length, (Address) string,
		      &numBytes, (Address *) &realString);

    Mach_MonGetLine(1);
    i = 0;
    realString[i] = Mach_MonGetNextChar();
    while (i < length - 1 && realString[i] != '\0') {
	i++;
	realString[i] = Mach_MonGetNextChar();
    }
    realString[i] = '\0';

    Vm_MakeUnaccessible((Address) realString, numBytes);

    return(SUCCESS);
#endif
}

/*
 * ----------------------------------------------------------------------------
 *
 * Test_GetChar --
 *
 *      Gets data from console.  I'm not sure this is actually used.
 *
 * Results:
 *      The character.
 *
 * Side effects:
 *      Gets a character.
 *
 * ----------------------------------------------------------------------------
 */
int 
Test_GetChar(charPtr)
    char	*charPtr;
{
#ifdef sun4c
    printf("Test_GetChar() doesn`t work on sun4c\n");
    return (FAILURE);
#else
    char 	*realCharPtr;
    int		numBytes;

    printf("Obsolete Test_GetChar() called\n");
    Vm_MakeAccessible(VM_OVERWRITE_ACCESS, 1, (Address) charPtr,
		      &numBytes, (Address *) &realCharPtr);
    if (numBytes == 0) {
	return(SYS_ARG_NOACCESS);
    }
    *realCharPtr = Mach_MonGetNextChar();

    printf("%c", *realCharPtr);

    Vm_MakeUnaccessible((Address) realCharPtr, numBytes);
    return(SUCCESS);
#endif
}
