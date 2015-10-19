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
#include "tokens.h"
#include "statements.h"
#include "vars.h"
#include "defs.h"
#include "dbg.h"
#include "parser-peg.h"
#include "expr.h"
#include <string.h>

static int parse_error;
static const char *file_name;
static int file_line;
static program *cur_program;
static enum parser_mode parser_mode;
static int parser_optimize;
static int last_def;
static char last_const_string[256]; // Holds last processed string
static int  last_const_string_len;  // and its length
static expr_mngr *mngr;
static expr *last_stmt;

program *parse_get_current_pgm(void)
{
    return cur_program;
}

void parser_set_current_pgm(program *p)
{
    cur_program = p;
}

static void set_current_pgm(program *pgm)
{
    cur_program = pgm;
}

void store_stmt(void)
{
    if( last_stmt )
        pgm_set_expr(parse_get_current_pgm(), last_stmt);
}

expr *add_comment(const char *str, int len)
{
    return expr_new_data(mngr, (const uint8_t *)str, len);
}

expr *add_data_stmt(const char *str, int len)
{
    return expr_new_data(mngr, (const uint8_t *)str, len);
}

void add_force_line()
{
    if( last_stmt && last_stmt->type != et_lnum )
        last_stmt = expr_new_lnum(mngr, last_stmt, -1);
}

void add_linenum(double num)
{
    if( num < 0 || num > 65535 )
        print_error("line number out of range","");
    else
        last_stmt = expr_new_lnum(mngr, last_stmt, (int)(num+0.5));
}

expr *ex_comma(expr *l, expr *r)
{
    return expr_new_bin(mngr, l, r, TOK_COMMA);
}

expr *ex_bin(expr *l, expr *r, enum enum_tokens k)
{
    return expr_new_bin(mngr, l, r, k);
}

expr *add_number(double n)
{
    return expr_new_number(mngr, n);
}

expr *add_hex_number(double n)
{
    return expr_new_hexnumber(mngr,n);
}

expr *add_string(void)
{
    return expr_new_string(mngr, (const uint8_t *)last_const_string, last_const_string_len);
}

void add_stmt(enum enum_statements st, expr *toks)
{
    last_stmt = expr_new_stmt(mngr, last_stmt, toks, st);
}

expr *add_ident(const char *name, enum var_type type)
{
    // Search if there is a definition with the same name
    defs *d = pgm_get_defs( parse_get_current_pgm() );
    if( defs_search(d, name) >= 0 )
    {
        err_print(file_name, file_line, "'%s' is a definition, use '@%s' instead.\n", name, name);
        parse_error++;
        return 0;
    }

    vars *v = pgm_get_vars( parse_get_current_pgm() );
    // Search or create if not found
    int id = vars_search(v, name, type);
    if( id < 0 )
    {
        id = vars_new_var(v, name, type, file_name, file_line);
        if( id < 0 )
        {
            err_print(file_name, file_line, "too many variables, got '%s'\n", name);
            parse_error++;
            return 0;
        }
        info_print(file_name, file_line, "renaming %s var '%s' -> '%s'\n",
                   var_type_name(type), name, vars_get_short_name(v, id));
    }
    switch(type)
    {
        case vtFloat:
            return expr_new_var_num(mngr,id);
        case vtString:
            return expr_new_var_str(mngr,id);
        case vtArray:
            return expr_new_var_array(mngr,id);
        case vtLabel:
            return expr_new_label(mngr,id);
        case vtNone:
        case vtMaxType:
            return 0;
    }
    return 0;
}

expr *add_strdef_val(const char *def_name)
{
    defs *d = pgm_get_defs( parse_get_current_pgm() );
    int id;
    if( (id = defs_search(d, def_name)) < 0 )
    {
        err_print(file_name, file_line, "'%s' not defined.\n", def_name);
        parse_error++;
        return 0;
    }
    const char *data;
    int len;
    if( !defs_get_string(d, id, &data, &len) )
    {
        err_print(file_name, file_line, "'%s' not a string definition.\n", def_name);
        parse_error++;
        return 0;
    }
    else
        return expr_new_string(mngr, (const uint8_t *)data, len);
}

expr *add_numdef_val(const char *def_name)
{
    defs *d = pgm_get_defs( parse_get_current_pgm() );
    int id;
    if( (id = defs_search(d, def_name)) < 0 )
    {
        err_print(file_name, file_line, "'%s' not defined.\n", def_name);
        parse_error++;
        return 0;
    }
    double val;
    if( !defs_get_numeric(d, id, &val) )
    {
        err_print(file_name, file_line, "'%s' not a numeric definition.\n", def_name);
        parse_error++;
        return 0;
    }
    else
        return expr_new_number(mngr, val);
}

void add_definition(const char *def_name)
{
    // Search variable, error if already found
    vars *v = pgm_get_vars( parse_get_current_pgm() );
    if( vars_search(v, def_name, vtFloat)  >= 0 ||
        vars_search(v, def_name, vtString) >= 0 ||
        vars_search(v, def_name, vtArray)  >= 0 ||
        vars_search(v, def_name, vtLabel)  >= 0 )
    {
        err_print(file_name, file_line, "variable '%s' already used.\n", def_name);
        parse_error++;
        return;
    }

    // Search in definitions
    defs *d = pgm_get_defs( parse_get_current_pgm() );
    if( defs_search(d, def_name) >= 0 )
    {
        err_print(file_name, file_line, "'%s' already defined.\n", def_name);
        parse_error++;
        return;
    }

    last_def = defs_new_def(d, def_name, file_name, file_line);
}

void add_incbin_file(const char *bin_file_name)
{
    FILE *bf = fopen(bin_file_name, "rb");
    if( !bf )
    {
        err_print(file_name, file_line, "error opening file '%s'.\n", bin_file_name);
        parse_error++;
        return;
    }
    char buf[256];
    int len = fread(buf, 1, 255, bf);
    if( len <= 0 )
    {
        err_print(file_name, file_line, "error reading file '%s'.\n", bin_file_name);
        parse_error++;
        return;
    }
    fclose(bf);
    if( len >= 248 )
    {
        err_print(file_name, file_line, "binary file '%s' is too big, truncating.\n", bin_file_name);
        parse_error++;
        len = 247;
    }
    // Add to last def:
    defs *d = pgm_get_defs( parse_get_current_pgm() );
    defs_set_string(d, last_def, buf, len);
}

void set_numdef_value(double x)
{
    // Add to last def:
    defs *d = pgm_get_defs( parse_get_current_pgm() );
    defs_set_numeric(d, last_def, x);
}

void set_strdef_value(void)
{
    // Add to last def:
    defs *d = pgm_get_defs( parse_get_current_pgm() );
    defs_set_string(d, last_def, last_const_string, last_const_string_len);
}

void print_error(const char *msg, const char *pos)
{
    err_print(file_name, file_line, "expected %s, got '%s'\n", msg, pos);
    parse_error++;
}

void inc_file_line(void)
{
    file_line ++;
    expr_mngr_set_file_line(mngr, file_line);
}

void parse_init(const char *fname)
{
    last_def = -1;
    parse_error = 0;
    file_line = 1;
    file_name = fname;
    program *pgm = program_new(fname);
    set_current_pgm(pgm);
    mngr = pgm_get_expr_mngr(pgm);
    last_stmt = 0;
    expr_mngr_set_file_line(mngr, file_line);
    parser_mode = parser_mode_default;
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

// For processing of string constants
static int is_hex_digit(char c)
{
    if( c >= '0' && c <= '9' ) return 1;
    if( c >= 'A' && c <= 'F' ) return 1;
    return 0;
}

void push_string_const(const char *data, unsigned len)
{
    char *buf = &last_const_string[0];
    unsigned rlen = 0;
    for( ; len && rlen<256 ; rlen++)
    {
        if( len>1 && data[0] == '"' && data[1] == '"' )
        {
            buf[rlen] = *data;
            data += 2;
            len  -= 2;
        }
        else if( len>2 && data[0] == '\\' &&
                 is_hex_digit(data[1]) && is_hex_digit(data[2]) )
        {
            int c1 = data[1] > '9' ? data[1] - 'A' + 10 : data[1] - '0';
            int c2 = data[2] > '9' ? data[2] - 'A' + 10 : data[2] - '0';
            buf[rlen] = c1 * 16 + c2;
            data += 3;
            len  -= 3;
        }
        else if( len>1 && data[0] == '\\' && data[1] == '\\' )
        {
            buf[rlen] = *data;
            data += 2;
            len  -= 2;
        }
        else
        {
            buf[rlen] = *data;
            data += 1;
            len  -= 1;
        }
    }
    if( rlen > 255 )
    {
        err_print(file_name, file_line, "string constant length too big, truncating.\n");
        parse_error++;
        rlen = 255;
    }
    last_const_string_len = rlen;
}

// Holds a table of names to atascii codes:
struct
{
    const char *name;
    unsigned char code;
} atascii_names[] = {
    { "heart", 0 },
    { "rbranch", 1 },
    { "rline", 2 },
    { "tlcorner", 3 },
    { "lbranch", 4 },
    { "blcorner", 5 },
    { "udiag", 6 },
    { "ddiag", 7 },
    { "rtriangle", 8 },
    { "brblock", 9 },
    { "ltriangle", 10 },
    { "trblock", 11 },
    { "tlblock", 12 },
    { "tline", 13 },
    { "bline", 14 },
    { "blblock", 15 },
    { "clubs", 16 },
    { "brcorner", 17 },
    { "hline", 18 },
    { "cross", 19 },
    { "ball", 20 },
    { "bbar", 21 },
    { "lline", 22 },
    { "bbranch", 23 },
    { "tbranch", 24 },
    { "lbar", 25 },
    { "trcorner", 26 },
    { "esc", 27 },
    { "up", 28 },
    { "down", 29 },
    { "left", 30 },
    { "right", 31 },
    { "diamond", 96 },
    { "spade", 123 },
    { "vline", 124 },
    { "clr", 125 },
    { "del", 126 },
    { "ins", 127 },
    { "tbar", 21+128 },
    { "rbar", 25+128 },
    { "eol", 155 },
    { "bell", 253 }
};
#define atascii_names_len (sizeof(atascii_names)/sizeof(atascii_names[0]))

void push_extended_string(const char *data, unsigned len)
{
    // Interprets the string:
    unsigned i, rlen = 0;
    char *buf = &last_const_string[0];
    int state = 0, inverse = 0, count = 0, nameStart = 0, hex = 0;

    for(i=0; i<len && rlen < 256; i++)
    {
        char c = data[i];
        switch( state )
        {
            case 5: // CR
                if( c != '\n' )
                {
                    buf[rlen++] = '\r' ^ inverse;
                    if( rlen >= 255 )
                        break;
                }
                state = 0;
                // Fall through to normal character
            case 0: // Normal characters
                if( c == '{' )
                {
                    state = 1;
                    count = 0;
                }
                else if( c == '~' )
                    inverse ^= 0x80;
                else if( c == '\\' )
                    state = 3;
                else if( c == '\r' )
                    state = 5;  // Consume CR before LF
                else if( c == '\n' )
                    buf[rlen++] = 0x9B;
                else
                    buf[rlen++] = c ^ inverse;
                break;
            case 1: // Special character - count
                if( c >= '0' && c <= '9' )
                    count = count * 10 + (c - '0');
                else if( c == '*' )
                {
                    state = 2;
                    nameStart = i + 1;
                }
                else
                {
                    state = 2;
                    nameStart = i;
                }
                break;
            case 2:
                if( c == '}' )
                {
                    // End of special character name
                    unsigned len = i - nameStart;
                    unsigned j;
                    if( count == 0 )
                        count = 1;
                    for(j=0; j<atascii_names_len; j++)
                        if( strlen(atascii_names[j].name) == len &&
                            memcmp(atascii_names[j].name, data+nameStart, len) == 0 )
                            break;
                    if( j >= atascii_names_len )
                    {
                        err_print(file_name, file_line, "invalid character name inside extended string '%.*s'\n",
                                  len, data + nameStart);
                        parse_error++;
                        return;
                    }
                    else if( count > 0xFF || count + rlen > 0xFF )
                    {
                        err_print(file_name, file_line, "too many characters inside extended string '%.*s'\n",
                                  len, data);
                        parse_error++;
                        return;
                    }
                    else
                    {
                        while( count-- )
                            buf[rlen++] = atascii_names[j].code ^ inverse;
                    }
                    count = 0;
                    state = 0;
                }
                break;
            case 3: // Escaped character
                if( (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') )
                {
                    hex = c > '9' ? c - 'A' + 10 : c - '0';
                    state = 4;
                }
                else
                {
                    buf[rlen++] = c ^ inverse;
                    state = 0;
                }
                break;
            case 4: // Escaped character hex 2
                if( (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') )
                    buf[rlen++] = hex * 16 + (c > '9' ? c - 'A' + 10 : c - '0');
                else
                {
                    err_print(file_name, file_line, "invalid escape ('\\%c') inside extended string\n", c);
                    parse_error++;
                    return;
                }
                state = 0;
                break;
        }
    }
    if( rlen > 255 )
    {
        err_print(file_name, file_line, "extended string length too big, truncating.\n");
        parse_error++;
        rlen = 255;
    }
    last_const_string_len = rlen;
}

