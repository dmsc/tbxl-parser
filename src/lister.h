/*
 *  Basic Parser - TurboBasic XL compatible parsing and transformation tool.
 *  Copyright (C) 2015 Daniel Serpell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#pragma once

#include <stdio.h>

typedef struct program_struct program;

// List program to a file, in "long" format
// if cont_ascii is != 0, convert comments to ASCII.
// Returns 0 if OK.
int lister_list_program_long(FILE *f, program *pgm, int conv_ascii);

// List program to a file, in "short" format
// max_line_len is the maximum character in the output lines, excluding the EOL
// Returns 0 if OK.
int lister_list_program_short(FILE *f, program *pgm, unsigned max_line_len);

