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
# Mon Dec 14 17:41:17 PST 1992
#
# For more information, refer to the mkmf manual page.
#
# $Header: /sprite/lib/mkmf/RCS/Makefile.md,v 1.6 90/03/12 23:28:42 jhh Exp $
#
# Allow mkmf

SRCS		= fileAccess.c lfs.c stableMem.c descMap.c seg.c segUsage.c
HDRS		= lfslib.h lfslibInt.h
MDPUBHDRS	= 
OBJS		= sun4.md/fileAccess.o sun4.md/lfs.o sun4.md/stableMem.o sun4.md/descMap.o sun4.md/seg.o sun4.md/segUsage.o
CLEANOBJS	= sun4.md/fileAccess.o sun4.md/lfs.o sun4.md/stableMem.o sun4.md/descMap.o sun4.md/seg.o sun4.md/segUsage.o
INSTFILES	= sun4.md/md.mk sun4.md/dependencies.mk Makefile local.mk tags
SACREDOBJS	= 
