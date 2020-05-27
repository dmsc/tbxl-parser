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

#include "listexpr.h"
#include "basexpr.h"
#include "expr.h"
#include "sbuf.h"
#include "vars.h"
#include "defs.h"
#include "darray.h"
#include "tokens.h"
#include "statements.h"
#include "ataribcd.h"
#include "program.h"
#include "dbg.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static int is_hex_digit(char c)
{
    if( c >= '0' && c <= '9' ) return 1;
    if( c >= 'A' && c <= 'F' ) return 1;
    return 0;
}

static void print_string_long(const char *str, int len, string_buf *s)
{
    const char *p;
    sb_put(s, '"');
    for(p=str; len>0; p++, len--)
    {
        if( *p < 32 || *p > 126 )
        {
            sb_put(s, '\\');
            sb_put_hex(s, (uint8_t)(*p), 2);
        }
        else if( *p == '"' )
        {
            sb_put(s, '"');
            sb_put(s, '"');
        }
        else if( *p == '\\' )
        {
            sb_put(s, '\\');
            if( (len > 2 && is_hex_digit(p[1]) && is_hex_digit(p[2])) ||
                (len > 1 && (p[1] < 32 || p[1] > 126)) )
                sb_put(s, '\\');
        }
        else
            sb_put(s, *p);
    }
    sb_put(s, '"');
}

static void print_string_short(const expr *e, string_buf *s)
{
    const char *p;
    const char *str = (const char *)e->str;
    int len = e->slen;

    sb_put(s, '"');
    for(p=str; len>0; p++, len--)
    {
        if( *p == '"' && (parser_get_dialect() == parser_dialect_turbo) )
            sb_put(s, '"');
        else if( *p == '"' || *p == '\x9b' )
        {
            err_print(expr_get_file_name(e), expr_get_file_line(e),
                      "string containts non-listable %s.\n",
                      *p == '"' ? "'\"'" : "end of line");
        }
        sb_put(s, *p);
    }
    sb_put(s, '"');
}

// Used to store information about printed definitions
typedef darray(int) darray_int;

static void add_used_def(darray_int *dp, int id)
{
    for(unsigned i=0; i<darray_len(dp); i++)
        if( darray_i(dp, i) == id )
            return;
    darray_add(dp, id);
}

static void get_used_def(darray_int *dp, const expr *e)
{
    if( !e )
        return;
    get_used_def(dp, e->lft);
    get_used_def(dp, e->rgt);

    if( e->type == et_def_number || e->type == et_def_string )
        add_used_def(dp, e->var);
}

static void print_def_orig(string_buf *s, const defs *d, int id)
{
    int strdef = defs_get_type(d, id);
    sb_puts(s, "\t$define ");
    sb_puts(s, defs_get_name(d, id));
    if( strdef )
    {
        int len;
        const char *str = defs_get_string(d, id, &len);
        sb_puts(s, "$ = ");
        print_string_long(str, len, s);
        sb_put(s, '\n');
    }
    else
    {
        char buf[64];
        double val = defs_get_numeric(d, id);
        sprintf(buf, " = %g\n", val);
        sb_puts(s, buf);
    }
}

string_buf *expr_print_used_defs(const expr *ex)
{
    if( !ex )
        return 0;

    darray_int dp;
    darray_init(dp, 16);
    get_used_def(&dp, ex);

    if( !darray_len(&dp) )
    {
        darray_delete(dp);
        return 0;
    }

    string_buf *s = sb_new();
    const defs *d = pgm_get_defs(expr_mngr_get_program(ex->mngr));
    for(unsigned i=0; i<darray_len(&dp); i++)
        print_def_orig(s, d, darray_i(&dp,i));
    darray_delete(dp);
    return s;
}

static void print_def_long(string_buf *s, const defs *d, int id, int strdef)
{
    sb_put(s, '@');
    sb_puts(s, defs_get_name(d, id));
    if( strdef )
        sb_put(s, '$');
}

static void print_var_long(string_buf *s, vars *v, int id)
{
    sb_puts(s, vars_get_long_name(v, id));
    enum var_type type = vars_get_type(v, id);
    if( type == vtString )
        sb_put(s, '$');
    else if( type == vtArray )
        sb_puts(s, "( ");
}

static int print_var_short(string_buf *s, vars *v, int id, const expr *e)
{
    const char *sn = vars_get_short_name(v, id);
    if( !sn )
    {
        err_print(expr_get_file_name(e), expr_get_file_line(e),
                  "invalid short variable name.\n");
        sn = "ERROR";
    }
    sb_puts(s, sn);
    enum var_type type = vars_get_type(v, id);
    switch(type)
    {
        case vtMaxType:
        case vtNone:
        case vtFloat:
        case vtLabel:
            return 1;
        case vtString:
            sb_put(s, '$');
            return 0;
        case vtArray:
            sb_put(s, '(');
            return 0;
    }
    return 0;
}

static int check_add_indent(enum enum_statements s)
{
    switch(s)
    {
        case STMT_DO:
        case STMT_ELSE:
        case STMT_FOR:
        case STMT_IF_MULTILINE:
        case STMT_IF_THEN:
        case STMT_PROC:
        case STMT_PROC_VAR:
        case STMT_REPEAT:
        case STMT_WHILE:
            return 1;
        default:
            return 0;
    }
}

static int check_del_indent(enum enum_statements s)
{
    switch(s)
    {
        case STMT_ELSE:
        case STMT_ENDIF:
        case STMT_ENDPROC:
        case STMT_LOOP:
        case STMT_NEXT:
        case STMT_UNTIL:
        case STMT_WEND:
        case STMT_ENDIF_INVISIBLE:
            return 1;
        default:
            return 0;
    }
}

static void print_indent(string_buf *s, int i)
{
    for(; i>0; --i)
        sb_put(s, '\t');
}

// Prints a comment to the string_buf
static void print_comment(string_buf *b, const uint8_t *txt, unsigned len, const expr *r)
{
    if( r && r->type == et_data )
        sb_write(b, r->str, r->slen);
    else
        sb_puts(b, ". ");
    sb_write(b, txt, len);
}

// Print comment converting to ASCII
static void print_comment_ascii(string_buf *b, const uint8_t *txt, unsigned len, const expr *r)
{
    // First, convert to ASCII
    string_buf *tmp = sb_new();
    for( ; len > 0; txt++, len--)
    {
        // Conversion table for values 0x00 to 0x1F
        static const char convAscii[32] = {
            '*', '|', '[', '\'', '|', ',', '/', '\\',
            '/', '.', '\\', '\'', '\'', '^', '_', '.',
            '$', ',', '-', '+', 'o', '_', ']', '-',
            '-', '|', '\'', 'e', '^', 'v', '<', '>'
        };
        // Remove "inverse video" and test each non-printable character
        int c = 0x7F & (*txt);
        if( c < 0x20 )
            c = convAscii[c];
        else if( c == 0x60 )
            c = '*'; // Diamonds
        else if( c == 0x7b )
            c = '*'; // Spades
        else if( c == 0x7d )
            c = '^'; // Up-Left arrow
        else if( c == 0x7e )
            c = '<'; // Delete
        else if( c == 0x7f )
            c = '>'; // Insert
        sb_put(tmp, c);
    }
    // Print
    print_comment(b, (const uint8_t *)sb_data(tmp), sb_len(tmp), r);
    sb_delete(tmp);
}

static int print_expr_long_rec(string_buf *out, const expr *e, int skip_then)
{
    int use_l_parens = 0;
    int use_r_parens = 0;
    int prec = 0;

    switch( e->type )
    {
        case et_lnum:
        case et_stmt:
        case et_data:
            assert(0 /* unexpected expr type */);
            return 0;
        case et_tok:
            prec = tok_prec_level(e->tok);
            int p = tok_need_parens(e->tok);
            use_l_parens = p > 1;
            use_r_parens = p != 0;

            if( e->lft && e->lft->type == et_tok && prec > tok_prec_level(e->lft->tok) )
            {
                sb_puts(out, "( ");
                print_expr_long_rec(out, e->lft, skip_then);
                sb_puts(out, " )");
            }
            else if( e->lft )
            {
                print_expr_long_rec(out, e->lft, skip_then);
            }
            if( !skip_then || e->tok != TOK_THEN )
                sb_puts(out, tokens[e->tok].tok_long);
            break;
        case et_c_number:
            if( e->num < -9.999999999e99 )
                sb_puts(out,"-9.999999999e99");
            else if( e->num > 9.999999999e99 )
                sb_puts(out,"9.999999999e99");
            else if( e->num > -1e-99 && e->num < 1e-99 )
                sb_puts(out,"0");
            else if( e->num > -9.999999999e99 && e->num < 9.999999999e99 )
            {
                char buf[64];
                sprintf(buf, "%.12g", e->num);
                sb_puts(out, buf);
            }
            else
                sb_puts(out, "(0/0)");
            break;
        case et_c_hexnumber:
            {
                int n = e->num;
                sb_put(out, '$');
                if( n < 256 )
                    sb_put_hex(out, n, 2);
                else
                    sb_put_hex(out, n, 4);
            }
            break;
        case et_c_string:
            print_string_long((const char *)e->str, e->slen, out );
            break;
        case et_def_number:
        case et_def_string:
            print_def_long(out, pgm_get_defs(expr_mngr_get_program(e->mngr)), e->var, e->type == et_def_string);
            break;
        case et_var_number:
        case et_var_string:
        case et_var_array:
        case et_var_label:
            print_var_long(out, pgm_get_vars(expr_mngr_get_program(e->mngr)), e->var);
            break;
        case et_void:
            return 0;
    }
    if( e->rgt )
    {
        if( use_r_parens == 0 && e->rgt->type == et_tok && prec >= tok_prec_level(e->rgt->tok) && prec > 0 )
        {
            use_r_parens = 1;
            sb_puts(out, "( ");
        }
        else if( use_l_parens )
            sb_puts(out, "( ");
        print_expr_long_rec(out, e->rgt, skip_then);
        if( use_r_parens )
            sb_puts(out, " )");
    }
    return 0;
}

string_buf *expr_print_long(const expr *e, int *indent, int conv_ascii)
{
    assert(e && e->type == et_stmt);

    string_buf *b = sb_new();

    if( check_del_indent(e->stmt) && *indent)
        (*indent)--;

    int pind = *indent;

    *indent += check_add_indent(e->stmt);

    print_indent(b, pind);
    if( e->stmt == STMT_REM_ )
    {
        int i;
        for(i=0; i<30; i++)
            sb_put(b, '-');
    }
    else if( e->stmt == STMT_REM || e->stmt == STMT_REM_HIDDEN )
    {
        assert(e->rgt && e->rgt->type == et_data);
        if( conv_ascii )
            print_comment_ascii(b, e->rgt->str, e->rgt->slen, e->rgt->lft);
        else
            print_comment(b, e->rgt->str, e->rgt->slen, e->rgt->lft);
        // Return here, don't trim the extra spaces in REM
        if( e->rgt->slen )
            return b;
    }
    else if( e->stmt == STMT_DATA )
    {
        assert(e->rgt && e->rgt->type == et_data);
        sb_puts(b, "data ");
        sb_write(b, e->rgt->str, e->rgt->slen);
    }
    else if( e->stmt == STMT_BAS_ERROR )
    {
        sb_puts(b, "ERROR - ");
        if( e->rgt && e->rgt->type == et_data && e->rgt->slen )
        {
            sb_write(b, e->rgt->str, e->rgt->slen-1);
            // Skip last extra COLON
            char c = e->rgt->str[e->rgt->slen-1];
            if( c != 0x10 + TOK_COLON )
                sb_put(b, c);
        }
    }
    else if( e->stmt == STMT_EXEC_PAR )
    {
        // We need to special case this, as parameters have embedded type information!
        assert(e->rgt && e->rgt->type == et_tok && e->rgt->tok == TOK_COMMA);
        const char *st = statements[e->stmt].stm_long;
        sb_puts_lcase(b, st);
        sb_put(b, ' ');
        print_expr_long_rec(b, e->rgt->lft, 0);
        sb_puts(b, ", ");
        expr *arg = e->rgt->rgt;
        unsigned pos = sb_len(b);
        string_buf *tmp = sb_new();
        while( arg && arg->type == et_tok && arg->tok == TOK_COMMA )
        {
            assert(arg->rgt && arg->rgt->rgt);
            sb_puts(tmp, ", ");
            print_expr_long_rec(tmp, arg->rgt->rgt, 0);
            arg = arg->lft;
            sb_insert(b, pos, tmp);
            sb_clear(tmp);
        }
        if( arg )
        {
            assert(arg->rgt);
            print_expr_long_rec(tmp, arg->rgt, 0);
            sb_insert(b, pos, tmp);
            sb_clear(tmp);
        }
        sb_delete(tmp);
    }
    else
    {
        const char *st = statements[e->stmt].stm_long;
        if( e->stmt == STMT_ENDIF_INVISIBLE )
            st = "ENDIF";
        sb_puts_lcase(b, st);
        if( *st )
            sb_put(b, ' ');
        if( e->rgt )
            print_expr_long_rec(b, e->rgt, e->stmt == STMT_IF_THEN);
    }
    // Strip spaces from end of line
    sb_trim_end(b, ' ');
    return b;
}

string_buf *expr_print_alone(const expr *e)
{
    int indent = 0;
    return expr_print_long(e, &indent, 1);
}

static int print_expr_short_rec(string_buf *out, const expr *e)
{
    int add_space = 0;
    int use_l_parens = 0;
    int use_r_parens = 0;
    int prec = 0;

    switch( e->type )
    {
        case et_lnum:
        case et_stmt:
        case et_data:
            assert(0 /* unexpected expr type */);
            return 0;
        case et_tok:
            prec = tok_prec_level(e->tok);
            int p = tok_need_parens(e->tok);
            use_l_parens = p > 1;
            use_r_parens = p != 0;

            if( e->lft && e->lft->type == et_tok && prec > tok_prec_level(e->lft->tok) )
            {
                sb_put(out, '(');
                print_expr_short_rec(out, e->lft);
                sb_put(out, ')');
            }
            else if( e->lft )
                add_space = print_expr_short_rec(out, e->lft);
            {
                const char *t = tokens[e->tok].tok_short;
                if( add_space && ( (t[0] >= 'A' && t[0] <= 'Z') || t[0] == '_' ) )
                    sb_put(out, ' ');
                sb_puts(out, t);
                add_space = 0;
            }
            break;
        case et_c_number:
        case et_c_hexnumber:
            {
                atari_bcd n = atari_bcd_from_double(e->num);
                atari_bcd_print(n, out);
            }
            break;
        case et_c_string:
            print_string_short(e, out);
            break;
        case et_def_number:
        case et_def_string:
            assert(0 /* defs not supported */);
            break;
        case et_var_number:
        case et_var_string:
        case et_var_array:
        case et_var_label:
            add_space = print_var_short(out, pgm_get_vars(expr_mngr_get_program(e->mngr)), e->var, e);
            break;
        case et_void:
            return 0;
    }
    if( e->rgt )
    {
        if( use_r_parens == 0 && e->rgt->type == et_tok && prec >= tok_prec_level(e->rgt->tok) && prec > 0 )
        {
            use_r_parens = 1;
            sb_put(out, '(');
        }
        else if( use_l_parens )
            sb_put(out, '(');
        add_space = print_expr_short_rec(out, e->rgt);
        if( use_r_parens )
        {
            sb_put(out, ')');
            add_space = 0;
        }
    }
    return add_space;
}

string_buf *expr_print_short(const expr *e, int *skip_colon, int *no_split)
{
    assert(e && e->type == et_stmt);

    string_buf *b = sb_new();

    if( e->stmt == STMT_ENDIF_INVISIBLE )
    {
        (*no_split) --;
        return b;
    }
    else if( e->stmt == STMT_REM_ || e->stmt == STMT_REM || e->stmt == STMT_REM_HIDDEN || e->stmt == STMT_BAS_ERROR )
        return b;

    *skip_colon = 0;
    if( e->stmt == STMT_DATA )
    {
        assert(e->rgt && e->rgt->type == et_data);
        sb_puts(b, "D.");
        sb_write(b, e->rgt->str, e->rgt->slen);
    }
    else
    {
        if( e->stmt == STMT_PRINT )
            sb_put(b, '?');
        else
            sb_puts(b, statements[e->stmt].stm_short);
        // Check if it is an IF/THEN, can't be split:
        if( e->stmt == STMT_IF_THEN )
        {
            *skip_colon = 1;
            (*no_split) ++; // Can't split the "THEN" part
        }
        if( e->rgt )
            print_expr_short_rec(b, e->rgt);
    }
    return b;
}


