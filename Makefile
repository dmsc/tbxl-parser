#
#  Basic Parser - TurboBasic XL compatible parsing and transformation tool.
#  Copyright (C) 2015 Daniel Serpell
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program.  If not, see <http://www.gnu.org/licenses/>
#
CROSS=
EXT=
CC=$(CROSS)gcc
CFLAGS=-Wall -O2 -g -Wstrict-prototypes -Wmissing-prototypes
DEPFLAGS=-MMD -MP
LDFLAGS=
LDLIBS=
PEGOPTS=

B=build

# C sources, in "src" directory
SOURCES=\
 ataribcd.c\
 basic.c\
 baswriter.c\
 line.c\
 lister.c\
 main.c\
 parser.c\
 program.c\
 sbuf.c\
 stmt.c\
 vars.c\

# Word lists, in "peg" directory
WORDS=\
 statements.txt\
 tokens.txt\

# Source PEG file, in "peg" directory
PEGS=\
 peg/basic-base.peg\
 peg/directives.peg\

# Generated PEG file
GENPEG=\
  $(B)/src/basic.peg

# Output dirs
ODIRS=$(B)/src $(B)/obj

# Git version
GIT_VERSION := $(shell git describe --long --dirty)

# Compressed distribution file, generated with "make dist"
DISTNAME=basicParser-$(GIT_VERSION)
D=$(B)/$(DISTNAME)
ZIPFILE=$(DISTNAME).zip

# Generated source files form PEG and word lists
W_SRC=$(patsubst %.txt, $(B)/src/%.c, $(WORDS))
W_INC=$(patsubst %.txt, $(B)/src/%.h, $(WORDS))
W_PEG=$(patsubst %.txt, $(B)/src/%.peg, $(WORDS))
P_SRC=$(patsubst %.peg, %_peg.c, $(GENPEG))

VERSION_TMP=$(B)/src/version.tmp
VERSION_H=$(B)/src/version.h

# Compiled object files
OBJS=$(patsubst %.c, $(B)/obj/%.o, $(notdir $(SOURCES) $(W_SRC)))

# Add include path from build directory
INCLUDES=-I$(B)/src/

# Main Target
TARGET=$(B)/basicParser$(EXT)

# Main rule - build output directories and main program
all: $(ODIRS) $(TARGET)

# Make directories
$(ODIRS):
	mkdir -p $@

# Compile main program
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

# Force generating of header files *before* compilation of depending sources
$(OBJS): $(VERSION_H) $(W_INC) $(P_SRC)

# Compile C source
$(B)/obj/%.o: src/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEPFLAGS) -o $@ $<

$(B)/obj/%.o: $(B)/src/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEPFLAGS) -o $@ $<

# Generate C source from PEG
$(P_SRC): %_peg.c: %.peg
	peg -o$@ $<

# Generate version info from GIT
.PHONY: $(VERSION_TMP)
$(VERSION_TMP):
	echo "#define GIT_VERSION \"$(GIT_VERSION)\"" > $@

$(VERSION_H): $(VERSION_TMP)
	cmp -s $@ $< || cp -f $< $@
	rm -f $<

# Special rules for generated word lists
$(W_PEG): $(B)/src/%.peg: peg/%.txt peg/gen-%.awk
	awk -f peg/gen-$*.awk $< $(B)/src/
$(W_INC): $(B)/src/%.h: peg/%.txt peg/gen-%.awk
	awk -f peg/gen-$*.awk $< $(B)/src/
$(W_SRC): $(B)/src/%.c: peg/%.txt peg/gen-%.awk
	awk -f peg/gen-$*.awk $< $(B)/src/

# Concatenation of all peg files
$(B)/src/basic.peg: $(PEGS) $(B)/src/statements.peg $(B)/src/tokens.peg
	cat $^ > $@

.PHONY:
clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(TARGET)

distclean: clean
	rm -f $(W_PEG) $(W_INC) $(W_SRC) $(P_SRC) $(GENPEG)
	-rmdir $(ODIRS)
	-rmdir $(B)

# Make distribution ZIP
.PHONY:
dist: all
	rm -rf $(D)
	mkdir -p $(D)
	$(CROSS)strip $(TARGET)
	cp -a $(TARGET) $(D)/
	cp -a README.md $(D)/
	cp -a LICENSE   $(D)/
	cp -a samples   $(D)/
	rm -f $(B)/$(ZIPFILE)
	(cd $B; zip -9vr $(ZIPFILE) $(DISTNAME))


# Include dependencies
-include $(OBJS:.o=.d)
