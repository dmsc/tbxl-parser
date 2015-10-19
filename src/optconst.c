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

#include "optconst.h"
#include "expr.h"
#include "dbg.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Simplify printing warnings
#define warn(...) \
    do { warn_print(expr_get_file_name(ex), expr_get_file_line(ex), __VA_ARGS__); } while(0)

static int set_number(expr *e, double x)
{
    if( e->lft ) expr_delete( e->lft );
    if( e->rgt ) expr_delete( e->rgt );
    if( e->str ) free(e->str);
    e->lft = 0;
    e->rgt = 0;
    e->type = et_c_number;
    e->num = x;
    return 1;
}

static int set_tok(expr *e, enum enum_tokens x)
{
    if( e->lft ) expr_delete( e->lft );
    if( e->rgt ) expr_delete( e->rgt );
    if( e->str ) free(e->str);
    e->lft = 0;
    e->rgt = 0;
    e->type = et_tok;
    e->tok = x;
    return 1;
}

static int set_string(expr *e, char *buf, unsigned len)
{
    if( e->lft ) expr_delete( e->lft );
    if( e->rgt ) expr_delete( e->rgt );
    if( e->str ) free(e->str);
    e->lft = 0;
    e->rgt = 0;
    e->type = et_c_string;
    e->str = (uint8_t *)buf;
    e->slen = len;
    return 1;
}

static int set_expr(expr *e, expr *ne)
{
    memcpy(e, ne, sizeof(*e));
    return 1;
}

static int chk_int(expr *e)
{
    return ( e->num < 0 || e->num >= 65535.5 );
}

static int hex(char c)
{
    if( c>='0' && c<='9' )
        return c - '0';
    if( c>='A' && c<='F' )
        return c - 'A' + 10;
    return -1;
}

static int ex_strcomp(expr *a, expr *b)
{
    unsigned ln = a->slen < b->slen ? a->slen : b->slen;
    int cmp = memcmp(a->str, b->str, ln);
    if( !cmp && a->slen < b->slen ) cmp = -1;
    if( !cmp && a->slen > b->slen ) cmp = 1;
    return cmp;
}

static int do_constprop(expr *ex)
{
    if( !ex )
        return 0;

    // Try to apply in the left/right branches
    int x = do_constprop(ex->lft) + do_constprop(ex->rgt);

    // Only apply to tokens:
    if( ex->type != et_tok )
        return x;

    // Finally, apply here:
    enum enum_tokens tk = ex->tok;
    int l_inum = ex->lft && (ex->lft->type == et_c_number || ex->lft->type == et_c_hexnumber);
    int r_inum = ex->rgt && (ex->rgt->type == et_c_number || ex->rgt->type == et_c_hexnumber);
    int l_istr = ex->lft && (ex->lft->type == et_c_string);
    int r_istr = ex->rgt && (ex->rgt->type == et_c_string);

    switch(tk)
    {
        case TOK_OR:
            if( (l_inum && ex->lft->num != 0) || (r_inum && ex->rgt->num != 0) )
                return set_number(ex,1.0);
            else if( l_inum && r_inum )
                return set_number(ex, (ex->lft->num != 0) || (ex->rgt->num != 0) );
            return x;
        case TOK_AND:
            if( (l_inum && ex->lft->num == 0) || (r_inum && ex->rgt->num == 0) )
                return set_number(ex,0.0);
            else if( l_inum && r_inum )
                return set_number(ex, (ex->lft->num != 0) && (ex->rgt->num != 0) );
            return x;
        case TOK_N_LEQ:
            if( l_inum && r_inum )
                return set_number(ex, ex->lft->num <= ex->rgt->num);
            return x;
        case TOK_N_NEQ:
            if( l_inum && r_inum )
                return set_number(ex, ex->lft->num != ex->rgt->num);
            return x;
        case TOK_N_GEQ:
            if( l_inum && r_inum )
                return set_number(ex, ex->lft->num >= ex->rgt->num);
            return x;
        case TOK_N_LE:
            if( l_inum && r_inum )
                return set_number(ex, ex->lft->num < ex->rgt->num);
            return x;
        case TOK_N_GE:
            if( l_inum && r_inum )
                return set_number(ex, ex->lft->num > ex->rgt->num);
            return x;
        case TOK_N_EQ:
            if( l_inum && r_inum )
                return set_number(ex, ex->lft->num == ex->rgt->num);
            return x;
        case TOK_NOT:
            if( r_inum )
                return set_number(ex, 0 != ex->rgt->num);
            return x;
        case TOK_PLUS:
            if( l_inum && r_inum )
                return set_number(ex, ex->lft->num + ex->rgt->num);
            return x;
        case TOK_MINUS:
            if( l_inum && r_inum )
                return set_number(ex, ex->lft->num - ex->rgt->num);
            return x;
        case TOK_STAR:
            if( l_inum && r_inum )
                return set_number(ex, ex->lft->num * ex->rgt->num);
            return x;
        case TOK_SLASH:
            if( l_inum && r_inum )
            {
                if( ex->rgt->num == 0 )
                    warn("at '/', integer division by 0\n");
                return set_number(ex, ex->lft->num / ex->rgt->num);
            }
            return x;
        case TOK_DIV:
            if( l_inum && r_inum )
            {
                if( ex->rgt->num == 0 )
                    warn("at 'DIV', integer division by 0\n");
                return set_number(ex, trunc(ex->lft->num / ex->rgt->num));
            }
            return x;
        case TOK_MOD:
            if( l_inum && r_inum )
            {
                if( ex->rgt->num == 0 )
                    warn("at 'MOD', integer division by 0\n");
                return set_number(ex, ex->lft->num - ex->rgt->num * trunc(ex->lft->num / ex->rgt->num));
            }
            return x;
        case TOK_ANDPER:
            if( (l_inum && chk_int(ex->lft)) || (r_inum && chk_int(ex->rgt)) )
            {
                warn("operands to '&' out of range\n");
                return set_number(ex,0.0);
            }
            if( (l_inum && ex->lft->num < 0.5) || (r_inum && ex->rgt->num < 0.5) )
                return set_number(ex,0.0);
            else if( l_inum && r_inum )
                return set_number(ex, lrint(ex->lft->num) & lrint(ex->rgt->num) );
            return x;
        case TOK_EXCLAM:
            if( (l_inum && chk_int(ex->lft)) || (r_inum && chk_int(ex->rgt)) )
            {
                warn("operands to '!' out of range\n");
                return set_number(ex,0.0);
            }
            if( (l_inum && ex->lft->num >= 65534.5) || (r_inum && ex->rgt->num >= 65534.5) )
                return set_number(ex,1.0);
            else if( l_inum && r_inum )
                return set_number(ex, lrint(ex->lft->num) | lrint(ex->rgt->num) );
            return x;
        case TOK_EXOR:
            if( (l_inum && chk_int(ex->lft)) || (r_inum && chk_int(ex->rgt)) )
            {
                warn("operands to 'EXOR' out of range\n");
                return set_number(ex,0.0);
            }
            if( l_inum && r_inum )
                return set_number(ex, lrint(ex->lft->num) ^ lrint(ex->rgt->num) );
            return x;
        case TOK_UPLUS:
        case TOK_L_PRN:
            // Always collapse this node
            return set_expr(ex, ex->rgt);
        case TOK_UMINUS:
            if( r_inum )
                return set_number(ex, - ex->rgt->num);
            return x;
        case TOK_CARET:
            if( l_inum && r_inum )
                return set_number(ex, pow(ex->lft->num, ex->rgt->num) );
            return x;
        case TOK_TRUNC:
            if( r_inum )
                return set_number(ex, trunc(ex->rgt->num));
            return x;
        case TOK_FRAC:
            if( r_inum )
                return set_number(ex, ex->rgt->num - trunc(ex->rgt->num));
            return x;
        case TOK_PER_0:
            return set_number(ex, 0);
        case TOK_PER_1:
            return set_number(ex, 1);
        case TOK_PER_2:
            return set_number(ex, 2);
        case TOK_PER_3:
            return set_number(ex, 3);
        case TOK_EXP:
            if( r_inum )
                return set_number(ex, exp(ex->rgt->num));
            return x;
        case TOK_LOG:
            if( r_inum )
            {
                if( ex->rgt->num <= 0 )
                    warn("at 'LOG', argument <= 0\n");
                return set_number(ex, log(ex->rgt->num));
            }
            return x;
        case TOK_CLOG:
            if( r_inum )
            {
                if( ex->rgt->num <= 0 )
                    warn("at 'CLOG', argument <= 0\n");
                return set_number(ex, log10(ex->rgt->num));
            }
            return x;
        case TOK_SQR:
            if( r_inum )
            {
                if( ex->rgt->num < 0 )
                    warn("at 'SQR', argument < 0\n");
                return set_number(ex, sqrt(ex->rgt->num));
            }
            return x;
        case TOK_SGN:
            if( r_inum )
                return set_number(ex, ex->rgt->num < 0 ? -1 : ex->rgt->num > 0 ? 1 : 0);
            return x;
        case TOK_ABS:
            if( r_inum )
                return set_number(ex, fabs(ex->rgt->num) );
            return x;
        case TOK_INT:
            if( r_inum )
                return set_number(ex, floor(ex->rgt->num) );
            return x;

            // NOTE: trig functions change behaviour depending on DEG/RAD....
        case TOK_ATN:
        case TOK_COS:
        case TOK_SIN:
            return x;

        case TOK_S_LEQ:
            if( l_istr && r_istr )
                return set_number(ex, ex_strcomp(ex->lft, ex->rgt) <= 0 );
            return x;
        case TOK_S_NEQ:
            if( l_istr && r_istr )
                return set_number(ex, ex_strcomp(ex->lft, ex->rgt) != 0 );
            return x;
        case TOK_S_GEQ:
            if( l_istr && r_istr )
                return set_number(ex, ex_strcomp(ex->lft, ex->rgt) >= 0 );
            return x;
        case TOK_S_LE:
            if( l_istr && r_istr )
                return set_number(ex, ex_strcomp(ex->lft, ex->rgt) < 0 );
            return x;
        case TOK_S_GE:
            if( l_istr && r_istr )
                return set_number(ex, ex_strcomp(ex->lft, ex->rgt) > 0 );
            return x;
        case TOK_S_EQ:
            if( l_istr && r_istr )
                return set_number(ex, ex_strcomp(ex->lft, ex->rgt) == 0 );
            return x;

        case TOK_CHRP:
            if( r_inum )
            {
                char *buf = malloc(1);
                buf[0] = (int)ex->rgt->num;
                return set_string(ex, buf, 1);
            }
            return x;

        case TOK_STRP:
        case TOK_HEXP:
            return x; // TODO: is it worth to optimize?

        case TOK_LEN:
            if( r_istr )
                return set_number(ex, ex->rgt->slen );
            return x;
        case TOK_ASC:
            if( r_istr && ex->rgt->slen )
                return set_number(ex, 0xFF&(ex->rgt->str[0]) );
            return x;
        case TOK_DEC:
            if( r_istr )
            {
                int c1 = ex->rgt->slen > 0 ? hex(ex->rgt->str[0]) : -1;
                int c2 = ex->rgt->slen > 1 ? hex(ex->rgt->str[1]) : -1;
                if( c1 < 0 )
                    return set_number(ex, 0);
                if( c2 < 0 )
                    return set_number(ex, c1 );
                else
                    return set_number(ex, c1 * 16 + c2 );
            }
            return x;
        case TOK_VAL:
        case TOK_INSTR:
        case TOK_UINSTR:
            return x; // TODO: is it worth to optimize?


            // Those vary at runtime:
        case TOK_PEEK:
        case TOK_RAND:
        case TOK_USR:
        case TOK_ADR:
        case TOK_RND:
        case TOK_FRE:
        case TOK_PADDLE:
        case TOK_STICK:
        case TOK_PTRIG:
        case TOK_STRIG:
        case TOK_DPEEK:
        case TOK_INKEYP:
        case TOK_TIMEP:
        case TOK_TIME:
        case TOK_ERR:
        case TOK_ERL:
        case TOK_RND_S:

            // Not operators/functions:
        case TOK_F_ASGN:
        case TOK_S_ASGN:
        case TOK_FOR_TO:
        case TOK_STEP:
        case TOK_THEN:
        case TOK_ON_GOTO:
        case TOK_ON_GOSUB:
        case TOK_ON_EXEC:
        case TOK_ON_GOSHARP:
        case TOK_SHARP:

        case TOK_COMMA:
        case TOK_A_COMMA:
        case TOK_SEMICOLON:
        case TOK_S_L_PRN:
        case TOK_A_L_PRN:
        case TOK_D_L_PRN:
        case TOK_FN_PRN:
        case TOK_DS_L_PRN:

        case TOK_QUOTE:
        case TOK_DUMMY:
        case TOK_DOLAR:
        case TOK_COLON:
        case TOK_EOL:
        case TOK_R_PRN:

        case TOK_LAST_TOKEN:
            return x;
    }
    return x;
}

void opt_constprop(expr *ex)
{
    // Apply rules over the tree until stops changing
    int changed = 1;
    while(changed)
        changed = do_constprop(ex);
}


static int do_convert_tok(expr *ex)
{
    if( !ex )
        return 0;

    // Try to apply in the left/right branches
    int x = do_convert_tok(ex->lft) + do_convert_tok(ex->rgt);

    // Convert numbers only
    if( ex->type != et_c_hexnumber && ex->type != et_c_number )
        return x;

    if( ex->num == 0 )
        return set_tok(ex, TOK_PER_0);
    if( ex->num == 1 )
        return set_tok(ex, TOK_PER_1);
    if( ex->num == 2 )
        return set_tok(ex, TOK_PER_2);
    if( ex->num == 3 )
        return set_tok(ex, TOK_PER_3);

    return x;
}

void opt_convert_tok(expr *ex)
{
    // Apply rules over the tree until stops changing
    int changed = 1;
    while(changed)
        changed = do_convert_tok(ex);
}

// Computes the maximum height of the tree
static int ex_tree_height(const expr *ex)
{
    if( !ex ) return 0;
    int hgl = ex_tree_height(ex->lft);
    int hgr = ex_tree_height(ex->rgt);
    if( hgl > hgr )
        return hgl + 1;
    else
        return hgr + 1;
}

static int do_commute(expr *ex)
{
    if( !ex )
        return 0;

    // Try to apply in the left/right branches
    int x = do_commute(ex->lft) + do_commute(ex->rgt);

    // Only apply to tokens
    if( ex->type != et_tok )
        return x;

    // See if our TOKEN is commutative
    enum enum_tokens tk = ex->tok;
    switch(tk)
    {
        // Commutative numeric operators
        case TOK_OR:
        case TOK_AND:
        case TOK_N_NEQ:
        case TOK_N_GE:
        case TOK_N_EQ:
        case TOK_PLUS:
        case TOK_STAR:
        case TOK_ANDPER:
        case TOK_EXCLAM:
        case TOK_EXOR:
        // Commutative string operators
        case TOK_S_NEQ:
        case TOK_S_EQ:
            break;

        // Non-equality comparisons, can be replaced by the inverse operation
        case TOK_N_LEQ:
        case TOK_N_GEQ:
        case TOK_N_LE:
        case TOK_S_LEQ:
        case TOK_S_GEQ:
        case TOK_S_LE:
        case TOK_S_GE:
            // TODO:
            return x;

        // Non commutative numeric binary operators.
        case TOK_MINUS:
        case TOK_SLASH:
        case TOK_DIV:
        case TOK_MOD:
        case TOK_CARET:
        default:
            return x;
    }

    // Get our precedence
    int prec = tok_prec_level(ex->tok);

    // Only apply if we have a TOKEN on left with less precedence, this avoids
    // adding an extra parenthesis on the right:
    if( ex->lft && ex->lft->type == et_tok && prec >= tok_prec_level(ex->lft->tok) )
        return x;

    // Get tree heights at left/right
    int hgr = ex_tree_height(ex->rgt);
    int hgl = ex_tree_height(ex->lft);

    // Commute to get a higher height on the left or if we have a parenthesis that
    // we can avoid by swapping:
    if( hgr > hgl ||
        ( ex->rgt && ex->rgt->type == et_tok && prec == tok_prec_level(ex->rgt->tok) ) )
    {
        // Swap
        expr *tmp = ex->lft;
        ex->lft = ex->rgt;
        ex->rgt = tmp;
        return 1;
    }

    return x;
}

void opt_commute(expr *ex)
{
    // Apply rules over the tree until stops changing
    int changed = 1;
    while(changed)
        changed = do_commute(ex);
}
