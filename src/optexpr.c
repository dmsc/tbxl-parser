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

#include "optexpr.h"
#include "program.h"
#include "stmt.h"
#include "tokens.h"
#include "statements.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct expr_mngr_struct {
    program *pgm;
    expr *data;
    unsigned len;
    unsigned size;
} expr_mngr;

static void memory_error(void)
{
    fprintf(stderr,"INTERNAL ERROR: memory allocation failure.\n");
    abort();
}

void expr_delete(expr *n)
{
    if( n->lft )
        expr_delete(n->lft);
    if( n->rgt )
        expr_delete(n->rgt);
    if( n->str )
        free(n->str);
    n->lft = 0;
    n->rgt = 0;
    n->str = 0;
}

static expr *expr_new(expr_mngr *);

expr *expr_new_void(expr_mngr *mngr)
{
    expr *n = calloc(1, sizeof(expr));
    n->type = et_void;
    return n;
}

expr *expr_new_bin(expr_mngr *mngr, expr *l, expr *r, enum enum_tokens tk)
{
    expr *n = expr_new(mngr);
    n->lft = l;
    n->rgt = r;
    n->tok = tk;
    n->type = et_tok;
    return n;
}

expr *expr_new_uni(expr_mngr *mngr, expr *r, enum enum_tokens tk)
{
    expr *n = expr_new(mngr);
    n->rgt = r;
    n->tok = tk;
    n->type = et_tok;
    return n;
}

expr *expr_new_tok(expr_mngr *mngr, enum enum_tokens tk)
{
    expr *n = expr_new(mngr);
    n->tok = tk;
    n->type = et_tok;
    return n;
}

expr *expr_new_var_num(expr_mngr *mngr, int vn)
{
    expr *n = expr_new(mngr);
    n->type = et_var_number;
    n->var = vn;
    return n;
}

expr *expr_new_var_str(expr_mngr *mngr, int vn)
{
    expr *n = expr_new(mngr);
    n->type = et_var_string;
    n->var = vn;
    return n;
}

expr *expr_new_var_array(expr_mngr *mngr, int vn)
{
    expr *n = expr_new(mngr);
    n->type = et_var_array;
    n->var = vn;
    return n;
}

expr *expr_new_label(expr_mngr *mngr, int vn)
{
    expr *n = expr_new(mngr);
    n->type = et_var_label;
    n->var = vn;
    return n;
}

expr *expr_new_number(expr_mngr *mngr, double x)
{
    expr *n = expr_new(mngr);
    n->num = x;
    n->type = et_c_number;
    return n;
}

expr *expr_new_hexnumber(expr_mngr *mngr, double x)
{
    expr *n = expr_new(mngr);
    n->num = x;
    n->type = et_c_hexnumber;
    return n;
}

expr *expr_new_string(expr_mngr *mngr, uint8_t *str, unsigned len)
{
    expr *n = expr_new(mngr);
    n->type = et_c_string;
    n->str = malloc(len);
    n->slen = len;
    memcpy(n->str, str, len);
    return n;
}

// Returns the precedence level of a token
static int tok_prec_level(enum enum_tokens tk)
{
    switch(tk)
    {
        case TOK_F_ASGN:
        case TOK_S_ASGN:
        case TOK_FOR_TO:
        case TOK_STEP:
        case TOK_THEN:
        case TOK_ON_GOTO:
        case TOK_ON_GOSUB:
        case TOK_ON_EXEC:
        case TOK_ON_GOSHARP:
            return -1;

        case TOK_COMMA:
        case TOK_A_COMMA:
        case TOK_SEMICOLON:
            return 0;

        case TOK_SHARP:
            return 1;

        case TOK_OR:
            return 2;

        case TOK_AND:
            return 3;

        case TOK_N_LEQ:
        case TOK_N_NEQ:
        case TOK_N_GEQ:
        case TOK_N_LE:
        case TOK_N_GE:
        case TOK_N_EQ:
            return 4;

        case TOK_NOT:
            return 5;

        case TOK_PLUS:
        case TOK_MINUS:
            return 6;

        case TOK_STAR:
        case TOK_SLASH:
        case TOK_DIV:
        case TOK_MOD:
            return 7;

        case TOK_ANDPER:
        case TOK_EXCLAM:
        case TOK_EXOR:
            return 8;

        case TOK_CARET:
            return 9;

        case TOK_UPLUS:
        case TOK_UMINUS:
            return 10;

        case TOK_S_LEQ:
        case TOK_S_NEQ:
        case TOK_S_GEQ:
        case TOK_S_LE:
        case TOK_S_GE:
        case TOK_S_EQ:
            return 11;

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
            return 12;
        case TOK_S_L_PRN:
        case TOK_A_L_PRN:
        case TOK_D_L_PRN:
        case TOK_FN_PRN:
        case TOK_DS_L_PRN:
            return 13;

        case TOK_QUOTE:
        case TOK_DUMMY:
        case TOK_DOLAR:
        case TOK_COLON:
        case TOK_EOL:
        case TOK_L_PRN:
        case TOK_R_PRN:
        case TOK_INKEYP:
        case TOK_TIMEP:
        case TOK_TIME:
        case TOK_ERR:
        case TOK_ERL:
        case TOK_RND_S:
        case TOK_PER_0:
        case TOK_PER_1:
        case TOK_PER_2:
        case TOK_PER_3:
            return 11;
        default:
            return 0;
    }
}

// Expressions to TOKENS
void expr_to_tokens(expr *e, stmt *s)
{
    int use_l_parens = 0;
    int use_r_parens = 0;
    int prec = 0;

    switch( e->type )
    {
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
                stmt_add_token(s, TOK_L_PRN);
                expr_to_tokens(e->lft, s);
                stmt_add_token(s, TOK_R_PRN);
            }
            else if( e->lft )
            {
                expr_to_tokens(e->lft, s);
            }
            stmt_add_token(s, e->tok);
            break;
        case et_c_number:
            stmt_add_number(s, e->num);
            break;
        case et_c_hexnumber:
            stmt_add_hex_number(s, e->num);
            break;
        case et_c_string:
            stmt_add_string(s, (const char *)e->str, e->slen);
            break;
        case et_var_number:
        case et_var_string:
        case et_var_array:
        case et_var_label:
            stmt_add_var(s, e->var);
            break;
        case et_void:
            return;
    }
    if( e->rgt )
    {
        if( use_r_parens == 0 && e->rgt->type == et_tok && prec >= tok_prec_level(e->rgt->tok) && prec > 0 )
        {
            use_r_parens = 1;
            stmt_add_token(s, TOK_L_PRN);
        }
        else if( use_l_parens )
            stmt_add_token(s, TOK_FN_PRN);
        expr_to_tokens(e->rgt, s);
        if( use_r_parens )
            stmt_add_token(s, TOK_R_PRN);
    }
}

///////////////////////////////////////////////////////////////////////

static expr *expr_new(expr_mngr *m)
{
    if( m->len == m->size )
    {
        // TODO: compact our expr list removing unused entries
        unsigned old_size = m->size;
        m->size *= 2;
        if( !m->size )
            memory_error();
        m->data = realloc(m->data, sizeof(expr) * m->size);
        if( !m->data )
            memory_error();
        memset(m->data + old_size, 0, sizeof(expr) * (m->size - old_size));
    }
    m->len++;
    return &(m->data[m->len-1]);
}

expr_mngr *expr_mngr_new(program *pgm)
{
    expr_mngr *m = malloc(sizeof(expr_mngr));
    if( !m )
        memory_error();
    m->len  = 0;
    m->size = 256;
    m->data = calloc(sizeof(expr), 256);
    if( !m->data )
        memory_error();
    return m;
}

void expr_mngr_delete(expr_mngr *m)
{
    // Free all associated expressions
    for(unsigned i=0; i < m->len; i++)
        expr_delete( &(m->data[i]) );
    free(m->data);
    free(m);
}

