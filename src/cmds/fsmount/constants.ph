#
# $Header: /sprite/src/admin/fsmount/RCS/constants.ph,v 1.1 91/11/18 17:14:51 voelker Exp Locker: voelker $
#
# The file of return codes and other handy constants for use with fsmount.
# (note: this is 'require()d' by the fsmount script.)
#

$MAX_FIELD_LENGTH =     256;
$MAX_LINE_LENGTH =      1024;
$MAX_EXEC_ARGS =        20;
$MAX_PASS = 		10;

#
# Return codes from fscheck.
#
$FSCHECK_OK = 		 0;
$FSCHECK_SOFT_ERROR = 	 1;
$FSCHECK_OUT_OF_MEMORY = 2;
$FSCHECK_NOREBOOT = 	 3;
$FSCHECK_REBOOT = 	 4;

$FSCHECK_HARD_ERROR = 	 -1;
$FSCHECK_READ_FAILURE =  -2;
$FSCHECK_WRITE_FAILURE = -3;
$FSCHECK_BAD_ARG = 	 -4;
$FSCHECK_MORE_MEMORY = 	 -5;
$FSCHECK_DISK_FULL = 	 -6;

#
# Return code from child process if the exec fails.
#
$EXEC_FAILED = 		 32;

#
# Exit codes.
#
$OK = 		0;
$REBOOT = 	1;
$HARDERROR = 	2;
$SOFTERROR = 	3;
$NOREBOOT = 	4;

#
# Status of entry in mount table. Starts off as CHILD_OK, changes to 
# CHILD_RUNNING while fscheck is running. If fscheck completes ok, 
# then status changes to CHILD_CHECKED to indicate that the prefix should 
# be attached. Otherwise the status changes to CHILD_FAILURE.
#
$CHILD_OK = 	        0;
$CHILD_RUNNING = 	1;
$CHILD_FAILURE = 	2;
$CHILD_CHECKED =        3;
$CHILD_MOUNTED =        4;
