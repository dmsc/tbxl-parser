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
#include "tokens.h"
#include "statements.h"
#include "ataribcd.h"
#include <stdio.h>
#include <assert.h>
#include <strings.h>

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
        else if( *p == '\\' && len > 2 && is_hex_digit(p[1]) && is_hex_digit(p[2]) )
        {
            sb_put(s, '\\');
            sb_put(s, '\\');
        }
        else
            sb_put(s, *p);
    }
    sb_put(s, '"');
}

static void print_string_short(const char *str, int len, string_buf *s)
{
    const char *p;
    sb_put(s, '"');
    for(p=str; len>0; p++, len--)
    {
        if( *p == '"' )
            sb_put(s, '"');
        sb_put(s, *p);
    }
    sb_put(s, '"');
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

static int print_var_short(string_buf *s, vars *v, int id)
{
    sb_puts(s, vars_get_short_name(v, id));
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
static void print_comment(string_buf *b, const uint8_t *txt, unsigned len)
{
    // See if the text already has a "REM" or "'":
    if( ( !len  || (txt[0] != '\'' && txt[0] != '.') ) &&
        ( len<2 || strncasecmp("r.", (const char *)txt, 2) ) &&
        ( len<3 || strncasecmp("rem", (const char *)txt, 3) ) )
        sb_puts(b, "rem ");
    sb_write(b, txt, len);
}

// Print comment converting to ASCII
static void print_comment_ascii(string_buf *b, const uint8_t *txt, unsigned len)
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
    print_comment(b, (uint8_t *)tmp->data, tmp->len);
    sb_delete(tmp);
}

static int print_expr_long_rec(string_buf *out, const expr *e, vars *varl, int skip_then)
{
    int use_l_parens = 0;
    int use_r_parens = 0;
    int prec = 0;

    switch( e->type )
    {
        case et_lnum:
        case et_stmt:
        case et_data:
            fprintf(stderr,"INTERNAL ERROR: unexpected expr type.\n");
            return 0;
        case et_tok:
            prec = tok_prec_level(e->tok);
            switch(e->tok)
            {
                case TOK_STRP:
                case TOK_CHRP:
                case TOK_USR:
                case TOK_ASC:
                case TOK_VAL:
                case TOK_LEN:
                case TOK_ADR:
                case TOK_ATN:
                case TOK_COS:
                case TOK_PEEK:
                case TOK_SIN:
                case TOK_RND:
                case TOK_FRE:
                case TOK_EXP:
                case TOK_LOG:
                case TOK_CLOG:
                case TOK_SQR:
                case TOK_SGN:
                case TOK_ABS:
                case TOK_INT:
                case TOK_PADDLE:
                case TOK_STICK:
                case TOK_PTRIG:
                case TOK_STRIG:
                case TOK_DPEEK:
                case TOK_INSTR:
                case TOK_HEXP:
                case TOK_UINSTR:
                case TOK_RAND:
                case TOK_TRUNC:
                case TOK_FRAC:
                case TOK_DEC:
                    use_l_parens = 1;
                    use_r_parens = 1;
                    break;
                case TOK_L_PRN:
                case TOK_S_L_PRN:
                case TOK_A_L_PRN:
                case TOK_D_L_PRN:
                case TOK_FN_PRN:
                case TOK_DS_L_PRN:
                    use_r_parens = 1;
                    break;
                default:
                    break;
            }
            if( e->lft && e->lft->type == et_tok && prec > tok_prec_level(e->lft->tok) )
            {
                sb_puts(out, "( ");
                print_expr_long_rec(out, e->lft, varl, skip_then);
                sb_puts(out, " )");
            }
            else if( e->lft )
            {
                print_expr_long_rec(out, e->lft, varl, skip_then);
            }
            if( !skip_then || e->tok != TOK_THEN )
                sb_puts(out, tokens[e->tok].tok_long);
            break;
        case et_c_number:
            {
                atari_bcd n = atari_bcd_from_double(e->num);
                atari_bcd_print(n, out);
            }
            break;
        case et_c_hexnumber:
            {
                atari_bcd n = atari_bcd_from_double(e->num);
                sb_put(out, '$');
                atari_bcd_print_hex(n, out);
            }
            break;
        case et_c_string:
            print_string_long((const char *)e->str, e->slen, out );
            break;
        case et_var_number:
        case et_var_string:
        case et_var_array:
        case et_var_label:
            print_var_long(out, varl, e->var);
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
        print_expr_long_rec(out, e->rgt, varl, skip_then);
        if( use_r_parens )
            sb_puts(out, " )");
    }
    return 0;
}

string_buf *expr_print_long(const expr *e, vars *varl, int *indent, int conv_ascii)
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
    else if( e->stmt == STMT_REM )
    {
        assert(e->rgt && e->rgt->type == et_data);
        if( conv_ascii )
            print_comment_ascii(b, e->rgt->str, e->rgt->slen);
        else
            print_comment(b, e->rgt->str, e->rgt->slen);
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
    else
    {
        const char *st = statements[e->stmt].stm_long;
        if( e->stmt == STMT_ENDIF_INVISIBLE )
            st = "ENDIF";
        sb_puts_lcase(b, st);
        if( *st )
            sb_put(b, ' ');
        if( e->rgt )
            print_expr_long_rec(b, e->rgt, varl, e->stmt == STMT_IF_THEN);
    }
    return b;
}

string_buf *expr_print_alone(const expr *e, vars *varl)
{
    int indent = 0;
    return expr_print_long(e, varl, &indent, 1);
}

static int print_expr_short_rec(string_buf *out, const expr *e, vars *varl)
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
            fprintf(stderr,"INTERNAL ERROR: unexpected expr type.\n");
            return 0;
        case et_tok:
            prec = tok_prec_level(e->tok);
            switch(e->tok)
            {
                case TOK_STRP:
                case TOK_CHRP:
                case TOK_USR:
                case TOK_ASC:
                case TOK_VAL:
                case TOK_LEN:
                case TOK_ADR:
                case TOK_ATN:
                case TOK_COS:
                case TOK_PEEK:
                case TOK_SIN:
                case TOK_RND:
                case TOK_FRE:
                case TOK_EXP:
                case TOK_LOG:
                case TOK_CLOG:
                case TOK_SQR:
                case TOK_SGN:
                case TOK_ABS:
                case TOK_INT:
                case TOK_PADDLE:
                case TOK_STICK:
                case TOK_PTRIG:
                case TOK_STRIG:
                case TOK_DPEEK:
                case TOK_INSTR:
                case TOK_HEXP:
                case TOK_UINSTR:
                case TOK_RAND:
                case TOK_TRUNC:
                case TOK_FRAC:
                case TOK_DEC:
                    use_l_parens = 1;
                    use_r_parens = 1;
                    break;
                case TOK_L_PRN:
                case TOK_S_L_PRN:
                case TOK_A_L_PRN:
                case TOK_D_L_PRN:
                case TOK_FN_PRN:
                case TOK_DS_L_PRN:
                    use_r_parens = 1;
                    break;
                default:
                    break;
            }
            if( e->lft && e->lft->type == et_tok && prec > tok_prec_level(e->lft->tok) )
            {
                sb_put(out, '(');
                print_expr_short_rec(out, e->lft, varl);
                sb_put(out, ')');
            }
            else if( e->lft )
                add_space = print_expr_short_rec(out, e->lft, varl);
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
            print_string_short((const char *)e->str, e->slen, out );
            break;
        case et_var_number:
        case et_var_string:
        case et_var_array:
        case et_var_label:
            add_space = print_var_short(out, varl, e->var);
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
        add_space = print_expr_short_rec(out, e->rgt, varl);
        if( use_r_parens )
        {
            sb_put(out, ')');
            add_space = 0;
        }
    }
    return add_space;
}

string_buf *expr_print_short(const expr *e, vars *varl, int *skip_colon, int *no_split)
{
    assert(e && e->type == et_stmt);

    string_buf *b = sb_new();

    if( e->stmt == STMT_ENDIF_INVISIBLE )
    {
        (*no_split) --;
        return b;
    }
    else if( e->stmt == STMT_REM_ || e->stmt == STMT_REM || e->stmt == STMT_BAS_ERROR )
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
            print_expr_short_rec(b, e->rgt, varl);
    }
    return b;
}


