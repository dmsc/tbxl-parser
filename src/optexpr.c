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
#include "line.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void memory_error(void)
{
    fprintf(stderr,"INTERNAL ERROR: memory allocation failure.\n");
    abort();
}

void expr_delete(expr *n)
{
    if( n->str )
        free(n->str);
    n->lft = 0;
    n->rgt = 0;
    n->str = 0;
}

static expr *expr_new(expr_mngr *);

expr *expr_new_void(expr_mngr *mngr)
{
    expr *n = expr_new(mngr);
    n->type = et_void;
    return n;
}

expr *expr_new_stmt(expr_mngr *mngr, expr *prev, expr *toks, enum enum_statements stmt)
{
    expr *n = expr_new(mngr);
    n->lft = prev;
    n->rgt = toks;
    n->stmt = stmt;
    n->type = et_stmt;
    return n;
}

expr *expr_new_lnum(expr_mngr *mngr, expr *prev, int lnum)
{
    expr *n = expr_new(mngr);
    n->lft = prev;
    n->rgt = 0;
    n->num = lnum;
    n->type = et_lnum;
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

expr *expr_new_string(expr_mngr *mngr, const uint8_t *str, unsigned len)
{
    expr *n = expr_new(mngr);
    n->type = et_c_string;
    n->str = malloc(len);
    n->slen = len;
    memcpy(n->str, str, len);
    return n;
}

expr *expr_new_data(expr_mngr *mngr, const uint8_t *data, unsigned len)
{
    expr *n = expr_new(mngr);
    n->type = et_data;
    n->str = malloc(len);
    n->slen = len;
    memcpy(n->str, data, len);
    return n;
}

// Returns the precedence level of a token
int tok_prec_level(enum enum_tokens tk)
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
static void expr_to_tokens(expr *e, stmt *s)
{
    int use_l_parens = 0;
    int use_r_parens = 0;
    int prec = 0;

    switch( e->type )
    {
        case et_lnum:
        case et_stmt:
            fprintf(stderr,"INTERNAL ERROR: unexpected expr type.\n");
            return;

        case et_data:
            stmt_add_data(s, (const char *)e->str, e->slen);
            return;

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
            stmt_add_binary_string(s, (const char *)e->str, e->slen);
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

static stmt *expr_to_statement(expr *e)
{
    if( e->type != et_stmt )
        return 0;
    stmt *ret = stmt_new(e->stmt);
    if( e->rgt )
        expr_to_tokens(e->rgt, ret);
    return ret;
}

// Convert expression tree back to program
int expr_to_program(expr *e, program *out)
{
    if( !e )
        return 0;

    if( e->type != et_stmt && e->type != et_lnum )
    {
        fprintf(stderr,"INTERNAL ERROR: expr tree invalid\n");
        return 1;
    }

    int err = expr_to_program(e->lft, out);

    line *l;
    if( e->type == et_stmt )
    {
        stmt *s = expr_to_statement(e);
        if( !s )
            err ++;

        l = line_new_from_stmt(s, 0);
    }
    else
        l = line_new_linenum(e->num, 0);

    pgm_add_line(out,l);
    return err;
}

const char *expr_get_file_name(expr *e)
{
    return expr_mngr_get_file_name(e->mngr);
}

int expr_get_file_line(expr *e)
{
    return e->file_line;
}

program *expr_get_program(const expr *e)
{
    return expr_mngr_get_program(e->mngr);
}

///////////////////////////////////////////////////////////////////////
// Number of expressions in each expression manager block
#define EXPR_MNGR_BLOCK_SIZE 1024
// Maximum memory available to the expression manager
#define EXPR_MNGR_MAX_SIZE 131072

typedef struct expr_mngr_struct {
    program *pgm;
    expr *blocks[EXPR_MNGR_MAX_SIZE / EXPR_MNGR_BLOCK_SIZE];
    expr *current;
    const char *file_name;
    unsigned file_line;
    unsigned len;
    unsigned size;
} expr_mngr;

static expr *expr_new(expr_mngr *m)
{
    if( m->len == m->size )
    {
        unsigned idx = m->size / EXPR_MNGR_BLOCK_SIZE;
        // TODO: compact our expr list removing unused entries
        m->size += EXPR_MNGR_BLOCK_SIZE;
        if( m->size > EXPR_MNGR_MAX_SIZE )
            memory_error();
        m->blocks[idx] = calloc(sizeof(expr), EXPR_MNGR_BLOCK_SIZE);
        if( !m->blocks[idx] )
            memory_error();
        m->current = m->blocks[idx];
    }
    expr *e = m->current;
    memset(e, 0, sizeof(expr));
    m->len++;
    m->current ++;
    e->mngr = m;
    e->file_line = m->file_line;
    return e;
}

expr_mngr *expr_mngr_new(program *pgm)
{
    expr_mngr *m = calloc(sizeof(expr_mngr),1);
    if( !m )
        memory_error();
    m->pgm  = pgm;
    m->file_name = pgm_get_file_name(pgm);
    m->len  = 0;
    m->size = EXPR_MNGR_BLOCK_SIZE;
    m->blocks[0] = calloc(sizeof(expr), EXPR_MNGR_BLOCK_SIZE);
    if( !m->blocks[0] )
        memory_error();
    m->current = m->blocks[0];
    return m;
}

void expr_mngr_delete(expr_mngr *m)
{
    // Free all associated expressions
    for(unsigned i=0; i < m->size/EXPR_MNGR_BLOCK_SIZE; i++)
    {
        expr *c = m->blocks[i];
        for(unsigned j=0; j<EXPR_MNGR_BLOCK_SIZE; j++, c++)
            expr_delete( c );
        free(m->blocks[i]);
    }
    free(m);
}

void expr_mngr_set_file_line(expr_mngr *m, int fline)
{
    m->file_line = fline;
}

int expr_mngr_get_file_line(const expr_mngr *m)
{
    return m->file_line;
}

const char *expr_mngr_get_file_name(const expr_mngr *m)
{
    return m->file_name;
}

program *expr_mngr_get_program(const expr_mngr *m)
{
    return m->pgm;
}

