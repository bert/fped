#
# Makefile - Makefile of fped, the footprint editor
#
# Written 2009-2012 by Werner Almesberger
# Copyright 2009-2012 by Werner Almesberger
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#

PREFIX ?= /usr/local

UPLOAD = www-data@downloads.qi-hardware.com:werner/fped/

OBJS = fped.o expr.o coord.o obj.o delete.o inst.o util.o error.o \
       unparse.o file.o dump.o kicad.o pcb.o postscript.o gnuplot.o meas.o \
       layer.o overlap.o hole.o tsort.o bitset.o \
       cpp.o lex.yy.o y.tab.o \
       gui.o gui_util.o gui_style.o gui_inst.o gui_status.o gui_canvas.o \
       gui_tool.o gui_over.o gui_meas.o gui_frame.o gui_frame_drag.o

XPMS = point.xpm delete.xpm delete_off.xpm \
       vec.xpm frame.xpm \
       line.xpm rect.xpm pad.xpm rpad.xpm hole.xpm arc.xpm circ.xpm \
       meas.xpm meas_x.xpm meas_y.xpm \
       stuff.xpm stuff_off.xpm meas_off.xpm \
       bright.xpm bright_off.xpm all.xpm all_off.xpm

PNGS = intro-1.png intro-2.png intro-3.png intro-4.png intro-5.png \
       intro-6.png concept-inst.png

SHELL = /bin/bash

CPPFLAGS +=
CFLAGS_GTK = `pkg-config --cflags gtk+-2.0`
LIBS_GTK = `pkg-config --libs gtk+-2.0`

CFLAGS_WARN = -Wall -Wshadow -Wmissing-prototypes \
	      -Wmissing-declarations -Wno-format-zero-length
CFLAGS += -g -std=gnu99 $(CFLAGS_GTK) -DCPP='"cpp"' \
         -DVERSION='"$(GIT_VERSION)$(GIT_STATUS)"' $(CFLAGS_WARN)
SLOPPY = -Wno-unused -Wno-implicit-function-declaration \
	 -Wno-missing-prototypes -Wno-missing-declarations
LDFLAGS +=
LDLIBS = -lm -lfl $(LIBS_GTK)
YACC = bison -y
YYFLAGS = -v

GIT_VERSION:=$(shell git rev-parse HEAD | cut -c 1-7)
GIT_STATUS:=$(shell [ -z "`git status -s -uno`" ] || echo +)

MKDEP = $(DEPEND) $(1).c | \
	sed -e \
	'/^\(.*:\)\? */{p;s///;s/ *\\\?$$/ /;s/  */:\n/g;H;}' \
	  -e '$${g;p;}' -e d >$(1).d; \
	[ "$${PIPESTATUS[*]}" = "0 0" ] || { rm -f $(1).d; exit 1; }


# ----- Verbosity control -----------------------------------------------------

CPP := $(CPP)   # make sure changing CC won't affect CPP

CC_normal	:= $(CC)
YACC_normal	:= $(YACC)
LEX_normal	:= $(LEX)
DEPEND_normal	:= $(CPP) $(CFLAGS) -MM -MG

CC_quiet	= @echo "  CC       " $@ && $(CC_normal)
YACC_quiet	= @echo "  YACC     " $@ && $(YACC_normal)
LEX_quiet	= @echo "  LEX      " $@ && $(LEX_normal)
GEN_quiet	= @echo "  GENERATE " $@ &&
DEPEND_quiet	= @$(DEPEND_normal)

ifeq ($(V),1)
    CC		= $(CC_normal)
    LEX		= $(LEX_normal)
    YACC	= $(YACC_normal)
    GEN		=
    DEPEND	= $(DEPEND_normal)
else
    CC		= $(CC_quiet)
    LEX		= $(LEX_quiet)
    YACC	= $(YACC_quiet)
    GEN		= $(GEN_quiet)
    DEPEND	= $(DEPEND_quiet)
endif

# ----- Rules -----------------------------------------------------------------

.PHONY:		all dep depend clean spotless
.PHONY:		install uninstall manual upload-manual
.PHONY:		montage test tests valgrind

.SUFFIXES:	.fig .xpm .ppm

# compile and generate dependencies, based on
# http://scottmcpeak.com/autodepend/autodepend.html

%.o:		%.c
		$(CC) $(CPPFLAGS) $(CFLAGS) -c $*.c -o $*.o
		$(call MKDEP, $*)

# generate 26x26 pixels icons, then drop the 1-pixel frame

.fig.ppm:
		$(GEN) fig2dev -L ppm -Z 0.32 -S 4 $< | \
		  convert -crop 24x24+1+1 - - >$@; \
		  [ "$${PIPESTATUS[*]}" = "0 0" ] || { rm -f $@; exit 1; }

# ppmtoxpm is very chatty, so we suppress its stderr

.ppm.xpm:
		$(GEN) export TMP=_tmp$$$$; ppmcolormask white $< >$$TMP && \
		  ppmtoxpm -name xpm_`basename $@ .xpm` -alphamask $$TMP \
		  $< >$@ 2>/dev/null && rm -f $$TMP || \
		  { rm -f $@ $$TMP; exit 1; }

all:		fped

fped:		$(OBJS)
		$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

lex.yy.c:	fpd.l y.tab.h
		$(LEX) fpd.l

lex.yy.o:	lex.yy.c y.tab.h
		$(CC) -c $(CFLAGS) $(SLOPPY) lex.yy.c
		$(call MKDEP, lex.yy)

y.tab.c y.tab.h: fpd.y
		$(YACC) $(YYFLAGS) -d fpd.y

y.tab.o:	y.tab.c
		$(CC) -c $(CFLAGS) $(SLOPPY) y.tab.c
		$(call MKDEP, y.tab)

gui_tool.o gui.o: $(XPMS:%=icons/%)

# ----- Upload the GUI manual -------------------------------------------------

manual:		$(XPMS:%=icons/%)
		for n in $(XPMS:%.xpm=%); do \
		    convert icons/$$n.xpm manual/$$n.png || exit 1; done
		fig2dev -L png -S 4 manual/concept-inst.fig \
		    >manual/concept-inst.png

upload-manual:	manual
		scp gui.html README $(UPLOAD)/
		scp $(XPMS:%.xpm=manual/%.png) $(PNGS:%=manual/%) \
		  $(UPLOAD)/manual/

# ----- Debugging help --------------------------------------------------------

montage:
		montage -label %f -frame 3 __dbg????.png png:- | display -

# ----- Dependencies ----------------------------------------------------------

dep depend .depend:
		@echo 'no need to run "make depend" anymore' 1>&2

-include $(OBJS:.o=.d)

# ----- Tests -----------------------------------------------------------------

test tests:	all
		LANG= sh -c \
		  'passed=0 && cd test && \
		  for n in [a-z]*; do \
		  [ $$n != core ] && SCRIPT=$$n CWD_PREFIX=.. . ./$$n; done; \
		  echo "Passed all $$passed tests"'

valgrind:
		VALGRIND="valgrind -q" $(MAKE) tests

# ----- Cleanup ---------------------------------------------------------------

clean:
		rm -f $(OBJS) $(XPMS:%=icons/%) $(XPMS:%.xpm=icons/%.ppm)
		rm -f lex.yy.c y.tab.c y.tab.h y.output .depend $(OBJS:.o=.d)
		rm -f __dbg????.png _tmp* test/core

spotless:	clean
		rm -f fped

# ----- Install / uninstall ---------------------------------------------------

install:	all
		mkdir -p $(DESTDIR)/$(PREFIX)/bin/
		install -m 755 fped $(DESTDIR)/$(PREFIX)/bin/

uninstall:
		rm -f $(DESTDIR)/$(PREFIX)/bin/fped
