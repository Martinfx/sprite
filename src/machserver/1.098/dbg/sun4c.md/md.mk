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
# Fri Aug  2 17:43:10 PDT 1991
#
# For more information, refer to the mkmf manual page.
#
# $Header: /sprite/lib/mkmf/RCS/Makefile.md,v 1.6 90/03/12 23:28:42 jhh Exp $
#
# Allow mkmf

SRCS		= sun4c.md/dbgIP.c sun4c.md/dbgMain.c
HDRS		= sun4c.md/dbg.h sun4c.md/dbgInt.h sun4c.md/vmInt.h sun4c.md/vmMachInt.h
MDPUBHDRS	= sun4c.md/dbg.h
OBJS		= sun4c.md/dbgIP.o sun4c.md/dbgMain.o
CLEANOBJS	= sun4c.md/dbgIP.o sun4c.md/dbgMain.o
INSTFILES	= sun4c.md/md.mk sun4c.md/dependencies.mk Makefile local.mk tags TAGS
SACREDOBJS	= 
