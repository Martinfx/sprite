#
# Makefile for boot programs in general.
# This is included by Makefile.boot after $(TM).md/md.mk is included
# The following variables should be defined already:
#	NAME		program to be created
#	OBJS		object files from which to create it
#	CLEANOBJS	object files to be removed as part of "make clean"
#			(need not just be object files)
#	SRCS		sources for dependency generation
#	TM		target machine type for object files, etc.
#	TM		target machine type for object files etc.
#	MACHINES	list of all target machines currently available
#	INSTALLDIR	place to install program
#	LINKSTART	address at which the boot program should be linked.
#
# Optional variables that may be defined by the invoker:
#	XAFLAGS		additional flags to pass to assembler
#	XCFLAGS		additional flags to pass to linker
#	DEPFLAGS	additional flags to pass to makedepend
#	no_targets	if defined, this file will not define all of the
#			basic targets (make, make clean, etc.)
#	use_version	if defined, then this file will set things up
#			to include a version number that is automatically
#			incremented
#
# $Header: /sprite/src/boot/sunprom/RCS/local.mk,v 1.4 90/11/27 11:17:35 jhh Exp $
#

#
# The variables below should be defined in md.mk, but they are given
# default values just in case md.mk doesn't exist yet.
#
HDRS		?=
OBJS		?=
SRCS		?=

#
# First define search paths for libraries, include files, lint libraries,
# and even sources.
#
.PATH.h		:
.PATH.h		: . $(TM).md /sprite/src/kernel/Include /sprite/src/kernel/Include/$(TM).md /sprite/src/kernel/dev /sprite/src/kernel/dev/$(TM).md /sprite/src/kernel/fs /sprite/src/kernel/fs/$(TM).md /sprite/lib/include /sprite/lib/include/$(TM).md 
.PATH.ln	: /sprite/lib/lint
.PATH.c		:
.PATH.c		: $(TM).md
.PATH.s		:
.PATH.s		: $(TM).md

#
# Important directories. 
#
MISCLIBDIR	= /sprite/lib/misc
BINDIR		= /sprite/cmds.$(MACHINE)

#
# System programs -- assign conditionally so they may be redefined in
# including makefile
#
AS		?= $(BINDIR)/as
CC		?= $(BINDIR)/cc
CHGRP		?= $(BINDIR)/chgrp
CHMOD		?= $(BINDIR)/chmod
CHOWN		?= $(BINDIR)/chown
CP		?= $(BINDIR)/cp
CPP		?= $(BINDIR)/cpp -traditional -$
CTAGS		?= $(BINDIR)/ctags
ECHO		?= $(BINDIR)/echo
LD		?= $(BINDIR)/ld
LINT		?= $(BINDIR)/lint
MAKEDEPEND	?= $(BINDIR)/makedepend
MKVERSION	?= $(BINDIR)/mkversion
MV		?= $(BINDIR)/mv
RM		?= $(BINDIR)/rm
SED		?= $(BINDIR)/sed
TEST            ?= $(BINDIR)/test
TOUCH		?= $(BINDIR)/touch
UPDATE		?= $(BINDIR)/update

#
# Several variables (such as where to install) are set based on the
# TYPE variable.  Of course, any of these variables can be overridden
# by explicit assignments.
#
TYPE		?= boot
INSTALLDIR	?= /sprite/boot
TMINSTALLDIR	?= /sprite/boot/$(TM).md

#
# Figure out what stuff we'll pass to sub-makes.
#
PASSVARS	= 'INSTALLDIR=$(INSTALLDIR)' 'TM=$(TM)' $(.MAKEFLAGS)
#ifdef		XCFLAGS
PASSVARS	+= 'XCFLAGS=$(XCFLAGS)'
#endif
#ifdef		XAFLAGS
PASSVARS	+= 'XAFLAGS=$(XAFLAGS)'
#endif
#ifdef		NOBACKUP
PASSVARS	+= 'NOBACKUP=$(NOBACKUP)'
#endif

#
# Flags. These are ones that are needed by *all* boot programs. Any other
# ones should be added with the += operator in the local.mk file.
# The FLAGS variables are defined with the += operator in case this file
# is included after the main makefile has already defined them...

#include 	<tm.mk>

XCFLAGS		?= -L/sprite/lib/$(TM).md
XAFLAGS		?=
LINTFLAGS	?= -m$(TM)
INSTALLFLAGS	?=
LDFLAGS		?=
AFLAGS		+= $(TMAFLAGS) $(XAFLAGS)
CFLAGS		+= $(TMCFLAGS) $(XCFLAGS) -I. -I$(TM).md  -DKERNEL \
		-I../../kernel/Include/$(TM).md -I../../kernel/Include \
		-I../../kernel/fs -I../../kernel/fs/$(TM).md \
		-I../../kernel/dev -I../../kernel/dev/$(TM).md -DNO_PRINTF -O

# KERNELSTART is the absolute address at which the kernel expects to have
# its code loaded.
# LINKSTART is where the boot program is loaded into memory.  It has
# to be loaded high enough so that the kernel image it loads does
# not overwrite the boot program.  (If it does, it generally happens
# as it zeros out the bss segment.  The PROM will abort with Exception 10
# or something immediately after the boot program prints out the kernel sizes.)
# BOOTDIR is the directory in which the boot things live.
#

#if !empty(TM:Msun3)
KERNELSTART	?= 0x4000
LINKSTART	?= $(KERNELSTART:S/0x/d/)
#else
KERNELSTART	?= 0x4000
LINKSTART	?= $(KERNELSTART:S/0x/20/)
#endif

CFLAGS		+= -DBOOT_CODE=0x$(LINKSTART) \
		-DKERNEL_START=$(KERNELSTART) -DBOOTDIR=\"$(INSTALLDIR)\"


#
# The .INCLUDES variable already includes directories that should be
# used by cc and other programs by default.  Remove them, just so that
# the output looks cleaner.

#if empty(TM:Msun4)
CFLAGS		+= $(.INCLUDES:S|^-I/sprite/lib/include$||g:S|^-I/sprite/lib/include/$(TM).md$||g)
#else
CFLAGS		+= $(.INCLUDES)
#endif

#
# Transformation rules: these have special features to place .o files
# in md subdirectories, run preprocessor over .s files, etc.
# There are no profile rules for boot programs because they aren't profiled.
#

.c.o		:
	$(RM) -f $(.TARGET)
	$(CC) $(CFLAGS) -c $(.IMPSRC) -o $(.TARGET)
.s.o	:
	$(CPP) $(CFLAGS:M-[ID]*) -D$(TM) -D_ASM $(.IMPSRC) > $(.PREFIX).pp
	$(AS) -o $(.TARGET) $(AFLAGS) $(.PREFIX).pp
	$(RM) -f $(.PREFIX).pp

#
# The following targets are .USE rules for creating things.
#

#
# MAKEBOOT usage:
#	<program> : <objects> <libraries> MAKEBOOT
#
# Similar to MAKECMD, except it doesn't create the version.[ho] files,
# and the variable LINKSTART is used to define where the boot program
# gets loaded.
#
MAKEBOOT	:  .USE -lc
	rm -f $(.TARGET)
	$(LD) -N -e start -T $(LINKSTART) $(CFLAGS:M-L*) $(LDFLAGS) \
		-o $(.TARGET) $(.ALLSRC:N-lc:Nend.o) -lc $(.ALLSRC:Mend.o)

#
# MAKEINSTALL usage:
#	install : <dependencies> MAKEINSTALL
#
# The program is installed in $(TMINSTALLDIR) and backed-up to
# $(TMINSTALLDIR).old
#
#ifndef NOBACKUP
BACKUP		= -b $(TMINSTALLDIR).old
#else
BACKUP		=
#endif  NOBACKUP

MAKEINSTALL	: .USE
	/sprite/admin.$(MACHINE)/makeboot $(TM).md/$(NAME) $(TMINSTALLDIR)/$(NAME)

#
# MAKELINT usage:
#	<fluff-file> : <sources to be linted> MAKELINT
#
# <fluff-file> is the place to store the output from the lint.
#
MAKELINT	: .USE
	$(RM) -f $(.TARGET)
	$(LINT) $(LINTFLAGS) $(CFLAGS:M-[ID]*) $(.ALLSRC) > $(.TARGET) 2>&1

#
# MAKEDEPEND usage:
#	<dependency-file> : <sources> MAKEDEPEND
#
# Generate dependency file suitable for inclusion in future makes.

MAKEDEPEND	: .USE
	@$(TOUCH) $(DEPFILE)
	$(MAKEDEPEND) $(CFLAGS:M-[ID]*) -m $(TM) -w60 -f $(DEPFILE) $(.ALLSRC)
	@$(MV) -f $(DEPFILE) $(DEPFILE).tmp
	@$(SED) -e '/^#/!s|^.|$(TM).md/&|' <$(DEPFILE).tmp > $(DEPFILE)
	@$(RM) -f $(DEPFILE).tmp

#if !defined(no_targets) && defined(NAME)
#
# We should define the main targets (make, make install, etc.).  See the
# mkmf man page for details on what these do.
#
LIBS			?=

#
# start.o must come first
default			: $(TM).md/$(NAME)
$(TM).md/$(NAME)	: $(TM).md/start.o $(OBJS:S/$(TM).md\/start.o//:S/makeBoot.o//) MAKEBOOT


clean			:: .NOEXPORT tidy 
	$(RM) -f $(TM).md/$(NAME) $(TM).md/$(NAME)$(PROFSUFFIX)

tidy			:: .NOEXPORT 
	$(RM) -f $(CLEANOBJS) $(CLEANOBJS:M*.o:S/.o$/.po/g) \
	    	y.tab.c lex.yy.c core \
		$(TM).md/lint \
		a.out *~ $(TM).md/*~ version.h gmon.out mon.out

DEPFILE = $(TM).md/dependencies.mk

depend			: $(DEPFILE)
$(DEPFILE)		! $(SRCS:M*.c) $(SRCS:M*.s) MAKEDEPEND


#
# For "install", a couple of tricks.  First, allow local.mk to disable
# by setting no_install.  Second, use :: instead of : so that local.mk
# can augment install with additional stuff.  Third, don't install if
# TMINSTALLDIR isn't set.
#
#ifndef no_install
#ifdef TMINSTALLDIR
install			:: $(TM).md/$(NAME) installman MAKEINSTALL
#else
install			:: .SILENT
	echo "Can't install $(NAME):  no install directory defined"
#endif TMINSTALLDIR
#endif no_install


#if empty(MANPAGES)
installman		:: .SILENT
	echo "There's no man page for $(NAME).  Please write one."
#elif !empty(MANPAGES:MNONE)
installman		::

#elif defined(INSTALLMAN)
installman		:: .SILENT
	$(UPDATE) -m 444 -l $(INSTALLMANFLAGS) $(MANPAGES) $(INSTALLMAN)
#else
installman		:: .SILENT
	echo "Can't install man page(s): no install directory defined"
#endif


lint			: $(TM).md/lint
$(TM).md/lint		: $(SRCS:M*.c) $(HDRS) $(LIBS:M-l*) MAKELINT


mkmf			:: .SILENT
	mkmf


newtm			:: .SILENT
	if test -d $(TM).md; then
	    true
	else
	    mkdir $(TM).md;
	    chmod 775 $(TM).md;
	    mkmf
	fi

#
# No profiling for boot programs
#
#profile			: $(TM).md/$(NAME)$(PROFSUFFIX)
#$(TM).md/$(NAME)$(PROFSUFFIX)	: $(OBJS:S/.o$/.po/g) $(LIBS:S/.a$/_p.a/g)
#	$(RM) -f $(.TARGET)
#	$(CC) $(CFLAGS) -pg -o $(.TARGET) $(.ALLSRC)


tags			:: $(SRCS:M*.c) $(HDRS)
	$(CTAGS) $(CTFLAGS) $(SRCS:M*.c)

#
# No version header for boot programs
#
#version.h		:
#	$(RM) -f version.h
#	$(MKVERSION) > version.h

#include	<all.mk>

#endif no_targets && NAME

.MAKEFLAGS	: -C		# No compatibility needed

#include	<rdist.mk>

DISTFILES    ?=

dist        !
#if defined(DISTDIR) && !empty(DISTDIR)
	for i in Makefile local.mk $(TM).md/md.mk \
	    $(MANPAGES) $(SRCS) $(HDRS)
	do
	if $(TEST) -e $${i}; then
	    $(UPDATE)  $${i} $(DISTDIR)/$${i} ;else true; fi
	done
#else
	@echo "Sorry, no distribution directory defined"
#endif

