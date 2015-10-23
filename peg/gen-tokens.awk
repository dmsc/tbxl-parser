#!/usr/bin/awk -f
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

BEGIN {
    FS="\t"
    infile = ARGV[1]
    outdir = ARGV[2]
    hdr=outdir "/tokens.h"
    peg=outdir "/tokens.peg"
    src=outdir "/tokens.c"
    ARGV[2]=""
    num = 0
    table = ""
    enums = ""
    print "processing " infile " to " outdir
}

{
    n=$1;
    s=$2;
    m=$3;
    l=$4;
    dopeg=$6;

    spc = " SPC";
    if( s == "\"" ) {
        s = "\\\"";
        spc = "";
    }

    # PEG file, calls test function, if valid, print
    if( dopeg )
        printf "%-16s= &{ testToken( TOK_%s ) } %s\n", n, n, spc, n > peg;

    # Header - enum definition
    enums = enums sprintf("    %s,\n", "TOK_" n);

    # Header - table
    table = table sprintf("    { %-11s %-11s %-7s },\n",\
                          "\"" s "\",", "\"" m "\",", "\"" l "\"");

    num = num + 1;
}

END {
    printf "#pragma once\n" \
           "/* This file is auto-generated from %s, don't modify */\n" \
           "\n" \
           "enum enum_tokens {\n" \
           "%s" \
           "    TOK_LAST_TOKEN\n" \
           "};\n" \
           "\n" \
           "struct tokens {\n" \
           "    const char *tok_in;\n" \
           "    const char *tok_short;\n" \
           "    const char *tok_long;\n" \
           "};\n"\
           "const extern struct tokens tokens[%d];" \
           "\n", \
           FILENAME, enums, num+1 > hdr
    printf "/* This file is auto-generated from %s, don't modify */\n" \
           "#include \"tokens.h\"\n" \
           "" \
           "const struct tokens tokens[%d] = {\n" \
           "%s" \
           "    { \"\", 0, \"\" }\n" \
           "};\n" \
           "\n", \
           FILENAME, num+1, table > src
}
