#
# Makefile --
#
# Makefile for Extended Tcl updates to the standard UCB Tcl.
# 
#------------------------------------------------------------------------------
# Copyright 1992 Karl Lehenbauer and Mark Diekhans.
#
# Permission to use, copy, modify, and distribute this software and its
# documentation for any purpose and without fee is hereby granted, provided
# that the above copyright notice appear in all copies.  Karl Lehenbauer and
# Mark Diekhans make no representations about the suitability of this
# software for any purpose.  It is provided "as is" without express or
# implied warranty.
#------------------------------------------------------------------------------
# $Id: Makefile,v 1.1 92/12/03 12:18:29 jhh Exp Locker: jhh $
#------------------------------------------------------------------------------
#


include ../Config.mk
include ../config/$(TCL_CONFIG_FILE)
SHELL=/bin/sh

#------------------------------------------------------------------------------

CFLAGS=$(OPTIMIZE_FLAG) $(XCFLAGS) -I../$(TCL_UCB_DIR) $(MEM_DEBUG_FLAGS) \
  $(SYS_DEP_FLAGS)

#------------------------------------------------------------------------------

OBJS=tclBasic.o tclExpr.o

#------------------------------------------------------------------------------

all: made.tmp

made.tmp: $(OBJS)
	-$(AR) d  ../libtcl.a $(OBJS)
	$(AR)  cr ../libtcl.a $(OBJS)
	touch made.tmp

tclBasic.c: ../$(TCL_UCB_DIR)/tclBasic.c
	rm -f tclBasic.c
	sed -f tclBasic.sed <../$(TCL_UCB_DIR)/tclBasic.c >tclBasic.c

tclExpr.c: ../$(TCL_UCB_DIR)/tclExpr.c
	rm -f tclExpr.c
	sed -f tclExpr.sed <../$(TCL_UCB_DIR)/tclExpr.c >tclExpr.c

#------------------------------------------------------------------------------

clean:
	-rm -f made.tmp tclBasic.c tclExpr.c
	-rm -f $(OBJS)
