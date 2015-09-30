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

int parse_file(const char *fname);
program *parse_get_current_pgm(void);
void parser_set_current_pgm(program *);

// Parser modes
enum parser_mode {
    parser_mode_default,
    parser_mode_compatible,
    parser_mode_extended
};

// Set parser options
enum parser_mode parser_get_mode(void);
void parser_set_mode(enum parser_mode mode);
int parser_get_optimize(void);
void parser_set_optimize(int);

