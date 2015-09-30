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
#include "program.h"
#include "stmt.h"
#include "line.h"
#include "vars.h"

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

static void memory_error(void)
{
    fprintf(stderr,"INTERNAL ERROR: memory allocation failure.\n");
    abort();
}

struct program_struct {
    vars *variables; // Program variables
    int len;         // Number of lines
    line **lines;    // Array of lines
    unsigned size;   // Available lines
    char *file_name; // Input file name
};

program *program_new(const char *file_name)
{
    program *p;
    p = malloc(sizeof(program));
    p->len = 0;
    p->lines = malloc(64 * sizeof(line*));
    p->size  = 64;
    p->variables = vars_new();
    p->file_name = strdup(file_name);
    return p;
}

void program_delete(program *p)
{
    int i;
    for(i=0; i<p->len; i++)
        line_delete(p->lines[i]);
    free( p->lines );
    vars_delete( p->variables );
    free( p->file_name );
    free( p );
}


// Ensures that there is enough space for one more item in the lines array
static void check_lines_size(program *p)
{
    if( p->len == p->size )
    {
        p->size *= 2;
        if ( p->size >= (UINT_MAX/sizeof(line*)) )
            memory_error();
        p->lines = realloc(p->lines, sizeof(line*) * p->size);
        if( !p->lines )
            memory_error();
    }
}

line *pgm_get_current_line(program *p)
{
    if( !p->len )
    {
        fprintf(stderr, "INTERNAL ERROR: no current line\n");
        abort();
    }
    return p->lines[p->len-1];
}

void pgm_add_line(program *p, line *l)
{
    check_lines_size(p);
    p->lines[p->len] = l;
    p->len ++;
}

vars *pgm_get_vars(program *p)
{
    return p->variables;
}

line **pgm_get_lines(program *p)
{
    // Ensures that there is a NULL at the end of the list of lines
    check_lines_size(p);
    p->lines[p->len] = 0;
    return p->lines;
}

const char *pgm_get_file_name(program *p)
{
    return p->file_name;
}
