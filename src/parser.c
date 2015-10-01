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
#include <stdio.h>
#include "parser.h"
#include "program.h"
#include "line.h"
#include "stmt.h"
#include "tokens.h"
#include "statements.h"
#include "vars.h"
#include "dbg.h"
#include "parser-peg.h"

static int parse_error;
static const char *file_name;
static int file_line;
static program *cur_program;
static enum parser_mode parser_mode;
static int parser_optimize;

program *parse_get_current_pgm(void)
{
    return cur_program;
}

void parser_set_current_pgm(program *p)
{
    cur_program = p;
}

static line *current_line(void)
{
    return pgm_get_current_line( parse_get_current_pgm() );
}

static stmt *get_statement(void)
{
    return line_get_statement(current_line());
}

static void set_current_pgm(program *pgm)
{
    cur_program = pgm;
}

void add_comment(const char *str, int len)
{
    stmt_add_comment(get_statement(), str, len);
}

void add_data_stmt(const char *str, int len)
{
    stmt_add_data(get_statement(), str, len);
}

void add_force_line()
{
    if( !line_is_num(pgm_get_current_line(parse_get_current_pgm())) )
        pgm_add_line(parse_get_current_pgm(), line_new_linenum(-1, file_line) );
}

void add_linenum(double num)
{
    if( num < 0 || num > 65535 )
        print_error("line number out of range","");
    else
        pgm_add_line(parse_get_current_pgm(), line_new_linenum( (int)(num+0.5), file_line ) );
}

void add_number(double n)
{
    stmt_add_number(get_statement(), n);
}

void add_hex_number(double n)
{
    stmt_add_hex_number(get_statement(), n);
}

void add_string(const char *str, int len)
{
    stmt_add_string(get_statement(), str, len);
}

void add_extended_string(const char *str, int len)
{
    if( stmt_add_extended_string(get_statement(), str, len, file_name, file_line) )
        print_error("extended string", "invalid");
}

void add_token(enum enum_tokens tk)
{
    stmt_add_token(get_statement(), tk);
}

void add_stmt(enum enum_statements st)
{
    pgm_add_line(parse_get_current_pgm(), line_new_statement(st, file_line));
}

void add_ident(const char *name, enum var_type type)
{
    vars *v = pgm_get_vars( parse_get_current_pgm() );
    // Search or create if not found
    int id = vars_search(v, name, type);
    if( id < 0 )
    {
        id = vars_new_var(v, name, type, file_name, file_line);
        if( id < 0 )
        {
            err_print(file_name, file_line, "too many variables, got '%s'\n", name);
            return;
        }
        info_print(file_name, file_line, "renaming %s var '%s' -> '%s'\n",
                   var_type_name(type), name, vars_get_short_name(v, id));
    }
    stmt_add_var(get_statement(), id);
}

void print_error(const char *msg, const char *pos)
{
    err_print(file_name, file_line, "expected %s, got '%s'\n", msg, pos);
    parse_error++;
}

void inc_file_line(void)
{
    file_line ++;
}

void parse_init(const char *fname)
{
    parse_error = 0;
    file_line = 1;
    file_name = fname;
    parser_mode = parser_mode_default;
    set_current_pgm( program_new(fname) );
}

int get_parse_errors(void)
{
    return parse_error;
}

enum parser_mode parser_get_mode(void)
{
    return parser_mode;
}

void parser_set_mode(enum parser_mode mode)
{
    info_print(file_name,file_line,"setting parsing mode to %s\n",
               mode==parser_mode_default ? "default" :
               mode==parser_mode_compatible ? "compatible" :
               mode==parser_mode_extended ? "extended" : "unknown");
    parser_mode = mode;
}

void parser_set_optimize(int opt)
{
    info_print(file_name,file_line,"%s optimizations\n", opt ? "enabling" : "disabling");
    parser_optimize = opt ? -1 : 0;
}

void parser_add_optimize(int level, int set)
{
    info_print(file_name,file_line,"%s optimization %d\n", set ? "enable" : "disable", level);
    if( set )
        parser_optimize |= level;
    else
        parser_optimize &= ~level;
}

int parser_get_optimize(void)
{
    return parser_optimize;
}
