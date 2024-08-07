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

// Saves a program in Turbo Basic XL compatible format.
// If 'variables' is 0, write short (one letter) variable names,
// if it is 1, write full variable names, <0 don't write any name.
// Returns 0 if OK.
int bas_write_program(FILE *f, program *pgm, int variables, unsigned max_line_len);

