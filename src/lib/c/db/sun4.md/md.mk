#
# Prototype Makefile for machine-dependent directories.
#
# A file of this form resides in each ".md" subdirectory of a
# command.  Its name is typically "md.mk".  During makes in the
# parent directory, this file (or a similar file in a sibling
# subdirectory) is included to define machine-specific things
# such as additional source and object files.
#
# This Makefile is automatically generated.
# DO NOT EDIT IT OR YOU MAY LOSE YOUR CHANGES.
#
# Generated from /sprite/lib/mkmf/Makefile.md
# Mon Jun  8 14:23:39 PDT 1992
#
# For more information, refer to the mkmf manual page.
#
# $Header: /sprite/lib/mkmf/RCS/Makefile.md,v 1.6 90/03/12 23:28:42 jhh Exp $
#
# Allow mkmf

SRCS		= Db_Close.c Db_Get.c Db_Open.c Db_WriteEntry.c Db_Put.c Db_ReadEntry.c DbLockDesc.c
HDRS		= dbInt.h
MDPUBHDRS	= 
OBJS		= sun4.md/Db_Close.o sun4.md/Db_Get.o sun4.md/Db_Open.o sun4.md/Db_WriteEntry.o sun4.md/Db_Put.o sun4.md/Db_ReadEntry.o sun4.md/DbLockDesc.o
CLEANOBJS	= sun4.md/Db_Close.o sun4.md/Db_Get.o sun4.md/Db_Open.o sun4.md/Db_WriteEntry.o sun4.md/Db_Put.o sun4.md/Db_ReadEntry.o sun4.md/DbLockDesc.o
INSTFILES	= sun4.md/md.mk sun4.md/dependencies.mk Makefile
SACREDOBJS	= 
