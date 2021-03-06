/* 
 * xgoned.c --
 *
 *	This program monitors the idle/busy state of the machine and
 *	starts up a screen saver (xgone) if the machine has been
 *	idle longer than a given amount of time.
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
static char rcsid[] = "$Header: /sprite/src/daemons/xgoned/RCS/xgoned.c,v 1.2 90/03/23 00:02:17 douglis Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include <stdio.h>
#include <sprite.h>
#include <stdlib.h>
#include <syslog.h>
#include <option.h>
#include <sysStats.h>
#include <kernel/sched.h>
#include <sys/types.h>
#include <sys/wait.h>

/*
 * Default interval, in seconds.
 */

#define DEFAULT_INTERVAL 300


int		interval = DEFAULT_INTERVAL;
char		*screenSaver = "xgone";
Boolean		detach = TRUE;
Boolean		debug = FALSE;

Option optionArray[] = {
	{OPT_INT, "t", (char *)&interval,
	     "Timeout before starting screen saver."},
	{OPT_FALSE, "D", (char *)&detach,
	     "Don't detach the process."},
	{OPT_TRUE, "d", (char *)&debug,
	    "Print out debugging information"},
};
static int numOptions = sizeof(optionArray) / sizeof(Option);

/*
 *----------------------------------------------------------------------
 *
 * main --
 *
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

main(argc, argv)
    int		argc;
    char	**argv;
{
    Sched_Instrument	stats;
    ReturnStatus	status;
    union wait		waitStatus;
    char		*myname;
    int			pid;
    int			i;
    extern int		errno;
    int 		wasActive = 1;

    argc = Opt_Parse(argc, argv, optionArray, numOptions,
		       OPT_ALLOW_CLUSTERING);

    myname = argv[0];

    if (interval < 1) {
	fprintf(stderr, "%s: Invalid interval '%d'.\n", myname, interval);
	exit(1);
    }
    if (detach) {
	Proc_Detach(0);
    }
    while (1) {
	if (debug) {
	    printf("Checking status.\n", interval);
	}
	status = Sys_Stats(SYS_SCHED_STATS, 0, (Address) &stats);
	if (status != SUCCESS) {
	    fprintf(stderr, "%s: Sys_Stats failed, \"%s\"\n", myname,
		Stat_GetMsg(status));
	    exit(1);
	}
	if (wasActive && stats.noUserInput.seconds > interval) {
	    wasActive = 0;
	    pid = vfork();
	    if (pid == 0) {
		if (debug) {
		    printf("Execing %s.\n", screenSaver);
		}
		(void) execlp(screenSaver, screenSaver, 0);
		fprintf(stderr, "Exec failed.\n");
		_exit(1);
	    } else {
		wait(&waitStatus);
		if (debug) {
		    printf("Wait returned.\n");
		}
	    }
	} else if (stats.noUserInput.seconds <= interval) {
	    wasActive = 1;
	}
	if (debug) {
	    printf("Sleeping %d seconds.\n", interval >> 1);
	}
	sleep(interval >> 1);
    }
}
