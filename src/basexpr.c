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

#include "basexpr.h"
#include "expr.h"
#include "sbuf.h"
#include "tokens.h"
#include "defs.h"
#include "program.h"
#include "statements.h"
#include "ataribcd.h"
#include <stdio.h>
#include <assert.h>

static int expr_get_bas_rec(string_buf *out, expr *e)
{
    int use_l_parens = 0;
    int use_r_parens = 0;
    int prec = 0;
    int ret = 0;

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
                sb_put(out, 0x10 + TOK_L_PRN);
                expr_get_bas_rec(out, e->lft);
                sb_put(out, 0x10 + TOK_R_PRN);
            }
            else if( e->lft )
            {
                expr_get_bas_rec(out, e->lft);
            }
            sb_put(out, 0x10 + e->tok);
            if( e->tok == TOK_COLON || e->tok == TOK_THEN )
                ret = 1;
            break;
        case et_c_number:
            {
                atari_bcd n = atari_bcd_from_double(e->num);
                sb_put(out, 0x0E);
                sb_put(out, n.exp);
                sb_put(out, n.dig[0]);
                sb_put(out, n.dig[1]);
                sb_put(out, n.dig[2]);
                sb_put(out, n.dig[3]);
                sb_put(out, n.dig[4]);
            }
            break;
        case et_c_hexnumber:
            {
                atari_bcd n = atari_bcd_from_double(e->num);
                sb_put(out, 0x0D);
                sb_put(out, n.exp);
                sb_put(out, n.dig[0]);
                sb_put(out, n.dig[1]);
                sb_put(out, n.dig[2]);
                sb_put(out, n.dig[3]);
                sb_put(out, n.dig[4]);
            }
            break;
        case et_c_string:
            sb_put(out, 0x0F);
            sb_put(out, e->slen);
            sb_write(out, e->str, e->slen);
            break;
        case et_def_number:
            {
                defs *d = pgm_get_defs(expr_mngr_get_program(e->mngr));
                double val;
                defs_get_numeric(d, e->var, &val);
                atari_bcd n = atari_bcd_from_double(val);
                sb_put(out, 0x0E);
                sb_put(out, n.exp);
                sb_put(out, n.dig[0]);
                sb_put(out, n.dig[1]);
                sb_put(out, n.dig[2]);
                sb_put(out, n.dig[3]);
                sb_put(out, n.dig[4]);
            }
            break;
        case et_def_string:
            {
                defs *d = pgm_get_defs(expr_mngr_get_program(e->mngr));
                const char *str;
                int len;
                defs_get_string(d, e->var, &str, &len);
                sb_put(out, 0x0F);
                sb_put(out, len);
                sb_write(out, (const uint8_t *)str, len);
            }
            break;
        case et_var_number:
        case et_var_string:
        case et_var_array:
        case et_var_label:
            assert(e->var>=0 && e->var<256);
            if( e->var > 127 )
                sb_put(out, 0);
            sb_put(out, (e->var) ^ 0x80);
            break;
        case et_void:
            return 0;
    }
    if( e->rgt )
    {
        if( use_r_parens == 0 && e->rgt->type == et_tok && prec >= tok_prec_level(e->rgt->tok) && prec > 0 )
        {
            use_r_parens = 1;
            sb_put(out, 0x10 + TOK_L_PRN);
        }
        else if( use_l_parens )
            sb_put(out, 0x10 + TOK_FN_PRN);
        ret = expr_get_bas_rec(out, e->rgt);
        if( use_r_parens )
        {
            sb_put(out, 0x10 + TOK_R_PRN);
            ret = 0;
        }
    }
    return ret;
}

string_buf *expr_get_bas(const expr *e, int *end_colon, int *no_split)
{
    assert( e->type == et_stmt );

    string_buf *b = sb_new();
    if( e->stmt == STMT_REM_ || e->stmt == STMT_REM || e->stmt == STMT_BAS_ERROR )
    {
        return b;
    }
    else if( e->stmt == STMT_DATA )
    {
        sb_put(b, e->stmt);
        assert(e->rgt && e->rgt->type == et_data);
        sb_write(b, e->rgt->str, e->rgt->slen);
        sb_put(b, 155);
        *end_colon = 0;
        return b;
    }
    else if( e->stmt == STMT_ENDIF_INVISIBLE )
    {
        (*no_split) --;
        return b;
    }
    else if( e->stmt == STMT_IF_THEN )
    {
        (*no_split) ++; // Can't split the "THEN" part
    }

    // Put statement
    if( e->stmt == STMT_IF_THEN || e->stmt == STMT_IF_MULTILINE || e->stmt == STMT_IF_NUMBER )
        sb_put(b, STMT_IF);
    else
        sb_put(b, e->stmt);

    int colon = 0;
    if( e->rgt )
        colon = expr_get_bas_rec(b, e->rgt);
    else
        colon = 0;

    if( !colon )
        sb_put(b, 0x10 + TOK_COLON);
    *end_colon = 1;
    return b;
}

int expr_get_bas_len(const expr *e)
{
    int ec = 0, ns = 0;
    string_buf *b = expr_get_bas(e, &ec, &ns);
    int len = b->len;
    sb_delete(b);
    return len;
}
