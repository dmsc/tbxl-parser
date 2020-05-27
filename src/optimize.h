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

typedef struct program_struct program;

enum optimize_levels {
    OPT_CONST_FOLD = 1,
    OPT_NUMBER_TOK = 2,
    OPT_COMMUTE    = 4,
    OPT_LINE_NUM   = 8,
    OPT_CONST_VARS = 16,
    OPT_FIXED_VARS = 32
};

// Returns the "standard" optimizations
enum optimize_levels optimize_all(void);

// Returns the optimization option from the text
enum optimize_levels optimize_option(const char *txt);

// Optimizes the program.
// returns 0 if ok.
int optimize_program(program *pgm, int level);

