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
    hdr=outdir "/statements.h"
    peg=outdir "/statements.peg"
    src=outdir "/statements.c"
    ARGV[2]=""
    num = 0
    table = ""
    enums = ""
    print "processing " infile " to " outdir
}

{
    n=$1;
    s=$2;
    q=length($3);
    f=length(s);
    dopeg=$4

    if( substr($3,q,1) == "." )
        q = q - 1;

    short = toupper($3);
    if ( length($3) == length(s) )
        short = toupper(s);

    # PEG file, calls test function, if valid, print
    if( dopeg )
        printf "%-16s= &{ testStatement( %d, STMT_%s ) } SPC\n",n, dopeg-1, n, n > peg;

    # Header - enum definition
    enums = enums sprintf("    %s,\n", "STMT_" n);

    # Header - table
    table = table sprintf("    { %-11s %1d, %-7s },\n",\
                          "\"" s "\",", q, "\"" short "\"");

    num = num + 1;
}

END {
    printf "#pragma once\n" \
           "/* This file is auto-generated from %s, don't modify */\n" \
           "\n" \
           "enum enum_statements {\n" \
           "%s" \
           "};\n" \
           "\n" \
           "struct statements {\n" \
           "    const char *stm_long;\n" \
           "    int min;\n" \
           "    const char *stm_short;\n" \
           "};\n"\
           "extern const struct statements statements[%d];" \
           "\n", \
           FILENAME, enums, num+1 > hdr
    printf "/* This file is auto-generated from %s, don't modify */\n" \
           "#include \"statements.h\"\n" \
           "" \
           "const struct statements statements[%d] = {\n" \
           "%s" \
           "    { \"\", 0, \"\" }\n" \
           "};\n" \
           "\n", \
           FILENAME, num+1, table > src
}
