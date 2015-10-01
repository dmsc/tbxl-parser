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

#include "optparse.h"
#include "optexpr.h"
#include "vars.h"
#include "program.h"
#include "stmt.h"
#include "tokens.h"
#include "statements.h"
#include "ataribcd.h"
#include <stdio.h>

// Structure to hold current parsing state
typedef struct pstate {
    uint8_t *pos;
    uint8_t *end;
    expr_mngr *mngr;
    vars *vrs;
} pstate;


// Forward declarations
static expr *p_num_expr(pstate *p);

static int sto(pstate *p, uint8_t **pos)
{
    *pos = p->pos;
    return 1;
}

// Store and Rewind parsing
static int rew(pstate *p, uint8_t *pos, int val)
{
    if( !val ) p->pos = pos;
    return val;
}
static int act(int val)
{
    return 1;
}
// Check parsing and rewind if not success
#define CHK(a) ( sto(p,&pos) && rew(p, pos, 0 != (a)) )
// Execute action
#define ACT(a) act(0 != (a))
// Parse any expression of the type " TOK_BINARY exp "
#define TOK_EXP2(tok, next) CHK( p_tok(p, tok) && 0 != (r=next(p)) && ACT(l = expr_new_bin(p->mngr, l, r, tok)) )
// Parse any expression of the type " TOK_UNARY exp "
#define TOK_EXP1(tok, next) CHK( p_tok(p, tok) && 0 != (r=next(p)) && ACT(r = expr_new_uni(p->mngr, r, tok)) )
// Parse any expression of the type " TOK_EXP "
#define TOK_EXP0(tok) CHK( p_tok(p, tok) && ACT(r = expr_new_tok(p->mngr, tok)) )

// Parse one token, advance if equal to given one
static int p_tok(pstate *p, enum enum_tokens tk)
{
    if( p->pos != p->end && tk + 0x10 == *(p->pos) )
    {
        p->pos = p->pos + 1;
        return 1;
    }
    return 0;
}

// Parse constant number
static expr *p_number(pstate *p)
{
    if( p->pos + 6 < p->end && (0x0D == *(p->pos) || 0x0E == *(p->pos)) )
    {
        int hex = (0x0D == *(p->pos));
        atari_bcd n;
        n.exp    = p->pos[1];
        n.dig[0] = p->pos[2];
        n.dig[1] = p->pos[3];
        n.dig[2] = p->pos[4];
        n.dig[3] = p->pos[5];
        n.dig[4] = p->pos[6];
        p->pos = p->pos + 7;
        if( !hex )
            return expr_new_number(p->mngr, atari_bcd_to_double(n));
        else
            return expr_new_hexnumber(p->mngr, atari_bcd_to_double(n));
    }
    return 0;
}

// Parse constant string
static expr *p_cstring_expr(pstate *p)
{
    if( p->pos+1 < p->end && 0x0F == *(p->pos) )
    {
        unsigned len = p->pos[1];
        if( len + p->pos + 1 < p->end )
        {
            uint8_t *s = p->pos + 2;
            p->pos = p->pos + 2 + len;
            return expr_new_string(p->mngr, s, len);
        }
    }
    return 0;
}

// Parse one variable, return var number + 1
static int p_var(pstate *p)
{
    unsigned varn;
    if( p->pos != p->end && *(p->pos) > 127 )
    {
        varn = (*p->pos) - 128;
        p->pos = p->pos + 1;
        return varn + 1;
    }
    else if( p->pos + 1 < p->end && 0 == *(p->pos) )
    {
        varn = p->pos[1] + 128;
        p->pos = p->pos + 2;
        return varn + 1;
    }
    return 0;
}

static expr *p_label_expr(pstate *p)
{
    uint8_t *pos;
    unsigned vn;
    if( CHK((vn = p_var(p)) && vtLabel == vars_get_type(p->vrs, vn-1) ) )
        return expr_new_label(p->mngr,vn-1);
    return 0;
}

static expr *p_array_access(pstate *p)
{
//ArrayAccess     <- NumExprErr ( A_COMMA NumExprErr )?
    uint8_t *pos;
    expr *l;
    if( CHK(l = p_num_expr(p)) )
    {
        expr *r;
        if( TOK_EXP2(TOK_A_COMMA, p_num_expr) )
            return l;
    }
    return l;
}

static expr *p_var_array_expr(pstate *p)
{
//VarArrayExpr    <- PVar
    uint8_t *pos;
    unsigned vn;
    if( CHK((vn = p_var(p)) && vtArray == vars_get_type(p->vrs, vn-1) ) )
        return expr_new_var_array(p->mngr,vn-1);
    return 0;
}


static expr *p_var_array_idx_expr(pstate *p)
{
//VarArrayIdxExpr <- PVar  A_L_PRN ArrayAccess R_PRN
    uint8_t *pos;
    expr *l, *r;
    if( CHK( (l=p_var_array_expr(p)) && p_tok(p, TOK_A_L_PRN) && (r = p_array_access(p)) && p_tok(p, TOK_R_PRN)) )
        return expr_new_bin(p->mngr,l, r, TOK_A_L_PRN);
    return 0;
}

static expr *p_var_num_expr(pstate *p)
{
//VarNumExpr      <- PVar
    uint8_t *pos;
    unsigned vn;
    if( CHK((vn = p_var(p)) && vtFloat == vars_get_type(p->vrs, vn-1) ) )
        return expr_new_var_num(p->mngr,vn-1);
    return 0;
}

static expr *p_var_str_expr(pstate *p)
{
//VarStrExpr      <- PVar
    uint8_t *pos;
    unsigned vn;
    if( CHK((vn = p_var(p)) && vtString == vars_get_type(p->vrs, vn-1) ) )
        return expr_new_var_str(p->mngr,vn-1);
    return 0;
}

static expr *p_var_str_sub_expr(pstate *p)
{
//VarStrSubExpr   <- PVarStr SubStr?
//SubStr          <- S_L_PRN ArrayAccess
    uint8_t *pos;
    expr *r, *l;
    if(  CHK( l = p_var_str_expr(p) ) )
    {
        if( CHK( p_tok(p, TOK_S_L_PRN) && (r = p_array_access(p)) && p_tok(p, TOK_R_PRN)) )
            return expr_new_bin(p->mngr,l, r, TOK_S_L_PRN);
        else
            return l;
    }
    return 0;
}

static expr *p_dim_var_array_expr(pstate *p)
{
//DimVarArray     <- PVar D_L_PRN ArrayAccess R_PRN
    uint8_t *pos;
    expr *l, *r;
    if( CHK( (l=p_var_array_expr(p)) && p_tok(p, TOK_D_L_PRN) && (r = p_array_access(p)) && p_tok(p, TOK_R_PRN)) )
        return expr_new_bin(p->mngr,l, r, TOK_D_L_PRN);
    return 0;
}

static expr *p_dim_var_str_expr(pstate *p)
{
//DimVarStr       <- PVar DS_L_PRN NumExpr R_PRN
    uint8_t *pos;
    expr *l, *r;
    if( CHK( (l=p_var_str_expr(p)) && p_tok(p, TOK_DS_L_PRN) && (r = p_num_expr(p)) && p_tok(p, TOK_R_PRN)) )
        return expr_new_bin(p->mngr,l, r, TOK_DS_L_PRN);
    return 0;
}

static expr *p_dim_var_expr(pstate *p)
{
//DimVar          <- DimVarArray
//                 / DimVarStr
    uint8_t *pos;
    expr *r;
    if(  CHK( r = p_dim_var_str_expr(p) )
       ||CHK( r = p_dim_var_array_expr(p) ) )
        return r;
    return 0;
}


static expr *p_par_num_expr(pstate *p)
{
//ParNumExpr       <- FN_PRN NumExpr R_PRN
    uint8_t *pos;
    expr *r;
    if( CHK( p_tok(p, TOK_FN_PRN) && (r = p_num_expr(p)) && p_tok(p, TOK_R_PRN)) )
        return r;
    return 0;
}

static expr *p_str_expr(pstate *p)
{
//StrExpr         <- STRP    ParNumExpr
//                 / CHRP    ParNumExpr
//                 / HEXP    ParNumExpr
//                 / INKEYP
//                 / TIMEP
//                 / ConstString
//                 / ExtendedString
//                 / VarStrSubExpr
    uint8_t *pos;
    expr *r;
    if(  TOK_EXP1(TOK_STRP,   p_par_num_expr)
      || TOK_EXP1(TOK_CHRP,   p_par_num_expr)
      || TOK_EXP1(TOK_HEXP,   p_par_num_expr)
      || TOK_EXP0(TOK_INKEYP)
      || TOK_EXP0(TOK_TIMEP)
      || CHK( r = p_cstring_expr(p) )
      || CHK( r = p_var_str_sub_expr(p) )
       )
        return r;
    return 0;
}

static expr *p_str_comp_expr(pstate *p)
{
//StrCompExpr     <- StrExpr (
//                     S_LEQ StrExpr
//                   / S_NEQ StrExpr
//                   / S_GEQ StrExpr
//                   / S_LE  StrExpr
//                   / S_GE  StrExpr
//                   / S_EQ  StrExpr
//                   )
    uint8_t *pos;
    expr *l, *r;
    if( CHK(l = p_str_expr(p)) )
    {
        while( TOK_EXP2(TOK_S_LEQ, p_str_expr)
            || TOK_EXP2(TOK_S_NEQ, p_str_expr)
            || TOK_EXP2(TOK_S_GEQ, p_str_expr)
            || TOK_EXP2(TOK_S_LE,  p_str_expr)
            || TOK_EXP2(TOK_S_GE,  p_str_expr)
            || TOK_EXP2(TOK_S_EQ,  p_str_expr) );
    }
    return l;
    return 0;
}

static expr *p_par_str_expr(pstate *p)
{
//ParStrExpr       <- FN_PRN ( StrExpr R_PRN
    uint8_t *pos;
    expr *r;
    if( CHK( p_tok(p, TOK_FN_PRN) && (r = p_str_expr(p)) && p_tok(p, TOK_R_PRN)) )
        return r;
    return 0;
}

static expr *p_in_instr_expr(pstate *p)
{
//InInstrExpr      <- StrExpr A_COMMA StrExpr ( A_COMMA NumExpr )?
    uint8_t *pos;
    expr *r, *l = 0;
    if( CHK( 0 != (l = p_str_expr(p)) && p_tok(p, TOK_A_COMMA) && 0 != (r=p_str_expr(p)) ) )
    {
        l = expr_new_bin(p->mngr,l, r, TOK_A_COMMA);
        if( TOK_EXP2(TOK_A_COMMA, p_num_expr) )
            return l;
        return l;
    }
    return 0;
}

static expr *p_par_instr_expr(pstate *p)
{
//ParInstrExpr     <- FN_PRN InInstrExpr R_PRN
    uint8_t *pos;
    expr *r;
    if( CHK( p_tok(p, TOK_FN_PRN) && (r = p_in_instr_expr(p)) && p_tok(p, TOK_R_PRN)) )
        return r;
    return 0;
}

static expr *p_in_usr_expr(pstate *p)
{
//InUsrExpr        <- NumExpr ( A_COMMA NumExpr )*
    uint8_t *pos;
    expr *r, *l = 0;
    if( CHK(l = p_num_expr(p)) )
        while( TOK_EXP2(TOK_A_COMMA, p_num_expr) );
    return l;
}

static expr *p_par_usr_expr(pstate *p)
{
//ParUsrExpr       <- FN_PRN InUsrExpr R_PRN
    uint8_t *pos;
    expr *r;
    if( CHK( p_tok(p, TOK_FN_PRN) && (r = p_in_usr_expr(p)) && p_tok(p, TOK_R_PRN)) )
        return r;
    return 0;
}

static expr *p_var_numeric_expr(pstate *p)
{
    uint8_t *pos;
    expr *r;
    if( CHK( r = p_var_array_idx_expr(p) )
     || CHK( r = p_var_num_expr(p) ) )
        return r;
    return 0;
}

static expr *p_unit_expr(pstate *p)
{
//UnitExpr        <- Number
//                 / StrCompExpr
//                 / L_PRN NumExpr R_PRN
//                 / USR    ParUsrExpr
//                 / ASC    ParStrExpr
//                 / VAL    ParStrExpr
//                 / LEN    ParStrExpr
//                 / ADR    ParStrExpr
//                 / ATN    ParNumExpr
//                 / COS    ParNumExpr
//                 / PEEK   ParNumExpr
//                 / SIN    ParNumExpr
//                 / RND    ParNumExpr
//                 / RND_S
//                 / FRE    ParNumExpr
//                 / EXP    ParNumExpr
//                 / LOG    ParNumExpr
//                 / CLOG   ParNumExpr
//                 / SQR    ParNumExpr
//                 / SGN    ParNumExpr
//                 / ABS    ParNumExpr
//                 / INT    ParNumExpr
//                 / PADDLE ParNumExpr
//                 / STICK  ParNumExpr
//                 / PTRIG  ParNumExpr
//                 / STRIG  ParNumExpr
//                 / DPEEK  ParNumExpr
//                 / INSTR  ParInstrExpr
//                 / DEC    ParStrExpr
//                 / FRAC   ParNumExpr
//                 / RAND   ParNumExpr
//                 / TRUNC  ParNumExpr
//                 / UINSTR ParInstrExpr
//                 / TIME
//                 / PER_0
//                 / PER_1
//                 / PER_2
//                 / PER_3
//                 / ERR
//                 / ERL
//                 / VarArrayExpr
//                 / VarNumExpr
    uint8_t *pos;
    expr *r;
    if(  CHK( r = p_number(p) )
      || CHK( r = p_str_comp_expr(p) )
      || CHK( p_tok(p, TOK_L_PRN) && (r = p_num_expr(p)) && p_tok(p, TOK_R_PRN) )
      || TOK_EXP1(TOK_USR,    p_par_usr_expr)
      || TOK_EXP1(TOK_ASC,    p_par_str_expr)
      || TOK_EXP1(TOK_VAL,    p_par_str_expr)
      || TOK_EXP1(TOK_LEN,    p_par_str_expr)
      || TOK_EXP1(TOK_ADR,    p_par_str_expr)
      || TOK_EXP1(TOK_ATN,    p_par_num_expr)
      || TOK_EXP1(TOK_COS,    p_par_num_expr)
      || TOK_EXP1(TOK_PEEK,   p_par_num_expr)
      || TOK_EXP1(TOK_SIN,    p_par_num_expr)
      || TOK_EXP1(TOK_RND,    p_par_num_expr)
      || TOK_EXP0(TOK_RND_S)
      || TOK_EXP1(TOK_FRE,    p_par_num_expr)
      || TOK_EXP1(TOK_EXP,    p_par_num_expr)
      || TOK_EXP1(TOK_LOG,    p_par_num_expr)
      || TOK_EXP1(TOK_CLOG,   p_par_num_expr)
      || TOK_EXP1(TOK_SQR,    p_par_num_expr)
      || TOK_EXP1(TOK_SGN,    p_par_num_expr)
      || TOK_EXP1(TOK_ABS,    p_par_num_expr)
      || TOK_EXP1(TOK_INT,    p_par_num_expr)
      || TOK_EXP1(TOK_PADDLE, p_par_num_expr)
      || TOK_EXP1(TOK_STICK,  p_par_num_expr)
      || TOK_EXP1(TOK_PTRIG,  p_par_num_expr)
      || TOK_EXP1(TOK_STRIG,  p_par_num_expr)
      || TOK_EXP1(TOK_DPEEK,  p_par_num_expr)
      || TOK_EXP1(TOK_INSTR,  p_par_instr_expr)
      || TOK_EXP1(TOK_DEC,    p_par_str_expr)
      || TOK_EXP1(TOK_FRAC,   p_par_num_expr)
      || TOK_EXP1(TOK_RAND,   p_par_num_expr)
      || TOK_EXP1(TOK_TRUNC,  p_par_num_expr)
      || TOK_EXP1(TOK_UINSTR, p_par_instr_expr)
      || TOK_EXP0(TOK_TIME)
      || TOK_EXP0(TOK_PER_0)
      || TOK_EXP0(TOK_PER_1)
      || TOK_EXP0(TOK_PER_2)
      || TOK_EXP0(TOK_PER_3)
      || TOK_EXP0(TOK_ERR)
      || TOK_EXP0(TOK_ERL)
      || CHK( r = p_var_numeric_expr(p) )
       )
        return r;
    return 0;
}

static expr *p_neg_expr(pstate *p)
{
//NegExpr         <- UMINUS NegExpr
//                 / UPLUS  NegExpr
//                 / UnitExpr
    uint8_t *pos;
    expr *r;
    return ( TOK_EXP1(TOK_UMINUS, p_neg_expr)
          || TOK_EXP1(TOK_UPLUS,  p_neg_expr)
          || CHK(r = p_unit_expr(p)) ) ? r : 0;
}

static expr *p_pow_expr(pstate *p)
{
//PowExpr         <- NegExpr (
//                     CARET PowExpr
//                     )*
    uint8_t *pos;
    expr *l, *r;
    if( CHK(l = p_neg_expr(p)) )
        while( TOK_EXP2(TOK_CARET, p_neg_expr) );
    return l;
}

static expr *p_bit_expr(pstate *p)
{
//BitExpr         <- PowExpr (
//                       ANDPER BitExpr
//                     / EXCLAM BitExpr
//                     / EXOR   BitExpr
//                     )*
    uint8_t *pos;
    expr *l, *r;
    if( CHK(l = p_pow_expr(p)) )
    {
        while( TOK_EXP2(TOK_ANDPER, p_pow_expr)
            || TOK_EXP2(TOK_EXCLAM, p_pow_expr)
            || TOK_EXP2(TOK_EXOR,   p_pow_expr) );
    }
    return l;
}

static expr *p_mult_expr(pstate *p)
{
//MultExpr        <- BitExpr (
//                     STAR  MultExpr
//                   / SLASH MultExpr
//                   / DIV   MultExpr
//                   / MOD   MultExpr
//                   )*
    uint8_t *pos;
    expr *l, *r;
    if( CHK(l = p_bit_expr(p)) )
    {
        while( TOK_EXP2(TOK_STAR,  p_bit_expr)
            || TOK_EXP2(TOK_SLASH, p_bit_expr)
            || TOK_EXP2(TOK_DIV,   p_bit_expr)
            || TOK_EXP2(TOK_MOD,   p_bit_expr) );
    }
    return l;
}

static expr *p_add_expr(pstate *p)
{
    //AddExpr         <- MultExpr (
    //                     PLUS AddExpr
    //                   / MINUS AddExpr
    //                   )*
    uint8_t *pos;
    expr *l, *r;
    if( CHK(l = p_mult_expr(p)) )
    {
        while( TOK_EXP2(TOK_PLUS,  p_mult_expr)
            || TOK_EXP2(TOK_MINUS, p_mult_expr) );
    }
    return l;
}

static expr *p_not_expr(pstate *p)
{
    //NotExpr         <- NOT NotExpr
    //                 / AddExpr
    uint8_t *pos;
    expr *r;
    return ( TOK_EXP1(TOK_NOT, p_not_expr)
          || CHK(r = p_add_expr(p)) ) ? r : 0;
}

static expr *p_comp_expr(pstate *p)
{
    //CompExpr        <-
    //                   NotExpr (
    //                       N_LEQ NotExpr
    //                     / N_NEQ NotExpr
    //                     / N_GEQ NotExpr
    //                     / N_LE  NotExpr
    //                     / N_GE  NotExpr
    //                     / N_EQ  NotExpr
    //                     )*
    uint8_t *pos;
    expr *l, *r;
    if( CHK(l = p_not_expr(p)) )
    {
        while( TOK_EXP2(TOK_N_LEQ, p_not_expr)
            || TOK_EXP2(TOK_N_NEQ, p_not_expr)
            || TOK_EXP2(TOK_N_GEQ, p_not_expr)
            || TOK_EXP2(TOK_N_LE,  p_not_expr)
            || TOK_EXP2(TOK_N_GE,  p_not_expr)
            || TOK_EXP2(TOK_N_EQ,  p_not_expr) );
    }
    return l;
}

static expr *p_or_expr(pstate *p)
{
    // OrExpr         <- CompExpr ( OR CompExpr )*
    uint8_t *pos;
    expr *l, *r;
    if( CHK(l = p_comp_expr(p)) )
        while( TOK_EXP2(TOK_OR,  p_comp_expr) );
    return l;
}

static expr *p_num_expr(pstate *p)
{
    // NumExpr         <- OrExpr ( AND OrExpr )*
    uint8_t *pos;
    expr *l, *r;
    if( CHK(l = p_or_expr(p)) )
    {
        while( TOK_EXP2(TOK_OR,  p_or_expr)
            || TOK_EXP2(TOK_AND, p_or_expr) );
    }
    return l;
}

static expr *p_dim_expr(pstate *p)
{
//                 / DIM    DimVar (COMMA DimVar)*
    uint8_t *pos;
    expr *r, *l = 0;
    if( CHK(l = p_dim_var_expr(p)) )
        while( TOK_EXP2(TOK_COMMA, p_dim_var_expr) );
    return l;
}

static expr *p_num_assign_expr(pstate *p)
{
    uint8_t *pos;
    expr *r, *l = 0;
    if( CHK( (l = p_var_numeric_expr(p)) && p_tok(p,TOK_F_ASGN) && (r=p_num_expr(p)) ) )
        return expr_new_bin(p->mngr, l, r, TOK_F_ASGN);
    return 0;
}

static expr *p_str_assign_expr(pstate *p)
{
    uint8_t *pos;
    expr *r, *l = 0;
    if( CHK( (l = p_var_str_sub_expr(p)) && p_tok(p,TOK_S_ASGN) && (r=p_str_expr(p)) ) )
        return expr_new_bin(p->mngr, l, r, TOK_S_ASGN);
    return 0;
}

static expr *p_any_expr(pstate *p)
{
    uint8_t *pos;
    expr *r;
    if(  CHK( r = p_num_expr(p) )
      || CHK( r = p_str_expr(p) ) )
        return r;
    return 0;
}

static expr *p_for_expr(pstate *p)
{
    uint8_t *pos;
#if 0
    expr *l, *r;
    if( CHK( (l=p_num_assign_expr(p)) && p_tok(p,TOK_FOR_TO) && (r=p_num_expr(p)) ) )
    {
        expr *s;
        if( CHK( p_tok(p,TOK_STEP) && (s=p_num_expr(p)) ) )
            r = expr_new_bin(p->mngr, r, s, TOK_STEP);
        return expr_new_bin(p->mngr, l, r, TOK_FOR_TO);
    }
#endif
    expr *l, *r;
    if(  (l=p_num_assign_expr(p)) &&  TOK_EXP2(TOK_FOR_TO, p_num_expr) )
        if( TOK_EXP2(TOK_STEP, p_num_expr) )
            return l;
    return l;
}

static expr *p_num_comma_expr(pstate *p)
{
    uint8_t *pos;
    expr *r, *l;
    if( CHK( (l=p_num_expr(p)) ) )
        while( TOK_EXP2(TOK_COMMA, p_num_expr) );
    return l;
}

static expr *p_label_comma_expr(pstate *p)
{
    uint8_t *pos;
    expr *r, *l;
    if( CHK( (l=p_label_expr(p)) ) )
        while( TOK_EXP2(TOK_COMMA, p_label_expr) );
    return l;
}

static expr *p_sharp_label_expr(pstate *p)
{
    uint8_t *pos;
    expr *l;
    if( CHK( p_tok(p, TOK_SHARP) && (l = p_label_expr(p)) ) )
        return expr_new_uni(p->mngr, l ,TOK_SHARP);
    return 0;
}

static expr *p_print_expr(pstate *p)
{
    uint8_t *pos;
    expr *r, *l = 0;
    if( (l=p_any_expr(p))
     || CHK( p_tok(p,TOK_COMMA) && (r=p_any_expr(p)) && ACT(l=expr_new_bin(p->mngr,0,r,TOK_COMMA)) )
     || CHK( p_tok(p,TOK_SEMICOLON) && (r=p_any_expr(p)) && ACT(l=expr_new_bin(p->mngr,0,r,TOK_SEMICOLON)) )
     || CHK( p_tok(p,TOK_COMMA) && ACT(l=expr_new_bin(p->mngr,0,0,TOK_COMMA)) )
     || CHK( p_tok(p,TOK_SEMICOLON) && ACT(l=expr_new_bin(p->mngr,0,0,TOK_SEMICOLON)) ) )
    {
        while( TOK_EXP2(TOK_COMMA, p_any_expr)
            || TOK_EXP2(TOK_SEMICOLON, p_any_expr)
            || CHK( p_tok(p, TOK_COMMA) && ACT(l=expr_new_bin(p->mngr,l,0,TOK_COMMA)) )
            || CHK( p_tok(p, TOK_SEMICOLON) && ACT(l=expr_new_bin(p->mngr,l,0,TOK_SEMICOLON)) ) );
        return l;
    }
    return expr_new_void(p->mngr);
}

static expr *p_io_expr(pstate *p)
{
    uint8_t *pos;
    expr *l;
    if( CHK( p_tok(p, TOK_SHARP) && (l = p_num_expr(p)) ) )
        return expr_new_uni(p->mngr, l ,TOK_SHARP);
    return 0;
}

static expr *p_print_io_expr(pstate *p)
{
    uint8_t *pos;
    expr *r, *l;
    if( CHK(l = p_io_expr(p)) )
    {
        if( CHK( p_tok(p,TOK_COMMA) && (r=p_print_expr(p)) ) )
            return expr_new_bin(p->mngr, l, r, TOK_COMMA);
        if( CHK( p_tok(p,TOK_SEMICOLON) && (r=p_print_expr(p)) ) )
            return expr_new_bin(p->mngr, l, r, TOK_SEMICOLON);
        return l;
    }
    else
        return p_print_expr(p);
}

static expr *p_on_expr(pstate *p)
{
    uint8_t *pos;
    expr *r, *l;
    // TODO: parse as line numbers!
    if( CHK( (l=p_num_expr(p)) && p_tok(p, TOK_ON_GOTO) && (r=p_num_comma_expr(p)) ) )
        return expr_new_bin(p->mngr, l,r,TOK_ON_GOTO);
    if( CHK( (l=p_num_expr(p)) && p_tok(p, TOK_ON_GOSUB) && (r=p_num_comma_expr(p)) ) )
        return expr_new_bin(p->mngr, l,r,TOK_ON_GOSUB);
    if( CHK( (l=p_num_expr(p)) && p_tok(p, TOK_ON_GOSHARP) && (r=p_label_comma_expr(p)) ) )
        return expr_new_bin(p->mngr, l,r,TOK_ON_GOSHARP);
    return 0;
}

static expr *p_if_then_expr(pstate *p)
{
    uint8_t *pos;
    expr *l;
    if( CHK( (l=p_num_expr(p)) && p_tok(p, TOK_THEN) ) )
    {
        expr *r;
        if( CHK( r=p_num_expr(p) ) ) // TODO: parse as "line number"
            return expr_new_bin(p->mngr, l, r, TOK_THEN);
        else
            return expr_new_bin(p->mngr, l, 0, TOK_THEN);
    }
    return 0;
}

static expr *p_xio_expr(pstate *p)
{
    uint8_t *pos;
    expr *l, *r;
    if(  (l=p_num_expr(p)) &&  TOK_EXP2(TOK_COMMA, p_io_expr)
      && TOK_EXP2(TOK_COMMA, p_num_expr)
      && TOK_EXP2(TOK_COMMA, p_num_expr)
      && TOK_EXP2(TOK_COMMA, p_str_expr) )
        return l;
    return 0;
}

static expr *p_num_2_expr(pstate *p)
{
    uint8_t *pos;
    expr *e1,*e2;
    if( CHK(  (e1=p_num_expr(p)) && p_tok(p,TOK_COMMA) && (e2=p_num_expr(p)) ) )
    {
        e1 = expr_new_bin(p->mngr, e1, e2, TOK_COMMA);
        return e1;
    }
    return 0;
}

static expr *p_num_3_expr(pstate *p)
{
    uint8_t *pos;
    expr *e1,*e2;
    if( CHK(  (e1=p_num_2_expr(p)) && p_tok(p,TOK_COMMA) && (e2=p_num_expr(p)) ) )
    {
        e1 = expr_new_bin(p->mngr, e1, e2, TOK_COMMA);
        return e1;
    }
    return 0;
}

static expr *p_num_4_expr(pstate *p)
{
    uint8_t *pos;
    expr *l, *r;
    if( (l=p_num_3_expr(p)) && TOK_EXP2(TOK_COMMA, p_num_expr) )
        return l;
    return 0;
}

static expr *p_io_num_2_expr(pstate *p)
{
    uint8_t *pos;
    expr *l, *r;
    if(  (l=p_io_expr(p))
       &&  TOK_EXP2(TOK_COMMA, p_num_expr)
       &&  TOK_EXP2(TOK_COMMA, p_num_expr) )
        return l;
    return 0;
}

static expr *p_assign_expr(pstate *p)
{
    uint8_t *pos;
    expr *r;
    if(  CHK( r = p_str_assign_expr(p) )
      || CHK( r = p_num_assign_expr(p) ) )
        return r;
    return 0;
}

static expr *p_list_expr(pstate *p)
{
    uint8_t *pos;
    expr *l;
    if(  CHK( l=p_str_expr(p) ) )
    {
        expr *r;
        if( CHK( p_tok(p, TOK_COMMA) && (r=p_num_2_expr(p)) )
         || CHK( p_tok(p, TOK_COMMA) && (r=p_num_expr(p)) ) )
            return expr_new_bin(p->mngr, l, r, TOK_COMMA);
        else
            return l;
    }
    if( CHK( l=p_num_2_expr(p) )
     || CHK( l=p_num_expr(p) ) )
        return l;
    return expr_new_void(p->mngr);
}

static expr * p_label_or_lnum_expr(pstate *p)
{
    uint8_t *pos;
    expr *l;
    if( CHK( (l=p_sharp_label_expr(p)) )
     || CHK( (l=p_num_expr(p)) ) )   // TODO: line number
        return l;
    else
        return 0;
}

// Used for STATUS
static expr * p_io_varnum_expr(pstate *p)
{
    uint8_t *pos;
    expr *l, *r;
    if(  (l=p_io_expr(p)) &&  TOK_EXP2(TOK_COMMA, p_var_num_expr) )
        return l;
    return 0;
}

// Used for NOTE
static expr * p_io_varnum2_expr(pstate *p)
{
    uint8_t *pos;
    expr *l, *r;
    if(  (l=p_io_expr(p)) &&  TOK_EXP2(TOK_COMMA, p_var_num_expr) &&  TOK_EXP2(TOK_COMMA, p_var_num_expr) )
        return l;
    return 0;
}

// Used for LOCATE
static expr * p_locate_expr(pstate *p)
{
    uint8_t *pos;
    expr *l, *r;
    if(  (l=p_num_expr(p)) &&  TOK_EXP2(TOK_COMMA, p_num_expr) &&  TOK_EXP2(TOK_COMMA, p_var_num_expr) )
        return l;
    return 0;
}

static expr *p_varnum_comma_expr(pstate *p)
{
    uint8_t *pos;
    expr *l, *r;
    if( CHK( (l=p_var_num_expr(p)) ) )
        while( TOK_EXP2(TOK_COMMA, p_var_num_expr) );
    return l;
}

static expr *p_var_numstr_comma_expr(pstate *p)
{
    uint8_t *pos;
    expr *l, *r;
    if( CHK( (l=p_var_num_expr(p)) || (l=p_var_str_expr(p)) ) )
        while( CHK( p_tok(p, TOK_COMMA) && ((r=p_var_num_expr(p)) || (r=p_var_str_expr(p))) ) )
            l=expr_new_bin(p->mngr, l, r, TOK_COMMA);
    return l;
}

static expr *p_get_expr(pstate *p)
{
    uint8_t *pos;
    expr *l, *r;
    if( CHK( (l=p_io_expr(p)) && p_tok(p, TOK_COMMA) && (r=p_varnum_comma_expr(p)) ) )
        return expr_new_bin(p->mngr, l, r, TOK_COMMA);
    if( CHK( (l=p_varnum_comma_expr(p)) ) )
        return l;
    return 0;
}

static expr *p_put_expr(pstate *p)
{
    uint8_t *pos;
    expr *l, *r;
    if( CHK( (l=p_io_expr(p)) && p_tok(p, TOK_COMMA) && (r=p_num_comma_expr(p)) ) )
        return expr_new_bin(p->mngr, l, r, TOK_COMMA);
    if( CHK( (l=p_num_comma_expr(p)) ) )
        return l;
    return 0;
}

static expr *p_input_expr(pstate *p)
{
    uint8_t *pos;
    expr *l, *r;
    if( CHK( (l=p_io_expr(p)) && p_tok(p, TOK_COMMA) && (r=p_var_numstr_comma_expr(p)) ) )
        return expr_new_bin(p->mngr, l, r, TOK_COMMA);
    if( CHK( (l=p_io_expr(p)) && p_tok(p, TOK_SEMICOLON) && (r=p_var_numstr_comma_expr(p)) ) )
        return expr_new_bin(p->mngr, l, r, TOK_SEMICOLON);
    if( CHK( (l=p_cstring_expr(p)) && p_tok(p, TOK_COMMA) && (r=p_var_numstr_comma_expr(p)) ) )
        return expr_new_bin(p->mngr, l, r, TOK_COMMA);
    if( CHK( (l=p_cstring_expr(p)) && p_tok(p, TOK_SEMICOLON) && (r=p_var_numstr_comma_expr(p)) ) )
        return expr_new_bin(p->mngr, l, r, TOK_SEMICOLON);
    if( CHK( (l=p_var_numstr_comma_expr(p)) ) )
        return l;
    return 0;
}

static expr * p_open_expr(pstate *p)
{
    uint8_t *pos;
    expr *l, *r;
    if(  (l=p_io_expr(p))
       &&  TOK_EXP2(TOK_COMMA, p_num_expr)
       &&  TOK_EXP2(TOK_COMMA, p_num_expr)
       &&  TOK_EXP2(TOK_COMMA, p_str_expr) )
        return l;
    return 0;
}

static expr * p_text_expr(pstate *p)
{
    uint8_t *pos;
    expr *e1, *e2;
    if( CHK( (e1=p_num_2_expr(p)) && p_tok(p, TOK_COMMA) && (e2=p_str_expr(p)) ) )
        return expr_new_bin(p->mngr, e1, e2, TOK_COMMA);
    return 0;
}

expr *opt_parse_statement(program *pgm, expr_mngr *mngr, stmt *s, int fline)
{
    // get original statement data:
    enum enum_statements sn = stmt_get_statement(s);
    unsigned len  = stmt_get_token_len(s);
    uint8_t *data = stmt_get_token_data(s);

    // Parse statement tokens:
    pstate p;
    expr *ex = 0;
    p.pos = data;
    p.end = data + len;
    p.mngr = mngr;
    p.vrs = pgm_get_vars(pgm);
    switch(sn)
    {
        case STMT_REM:
        case STMT_REM_:
        case STMT_DATA:
        case STMT_BAS_ERROR:
        case STMT_IF:
            // Already handled
            fprintf(stderr,"OPTIMIZER ERROR: unexpected statement\n");
            return 0;
        case STMT_LET_INV:
        case STMT_LET:
            ex = p_assign_expr(&p);
            break;
        case STMT_BYE:
        case STMT_CLR:
        case STMT_CONT:
        case STMT_CLOAD:
        case STMT_CSAVE:
        case STMT_DEG:
        case STMT_DOS:
        case STMT_DO:
        case STMT_ELSE:
        case STMT_END:
        case STMT_ENDIF:
        case STMT_ENDPROC:
        case STMT_LOOP:
        case STMT_NEW:
        case STMT_WEND:
        case STMT_POP:
        case STMT_RAD:
        case STMT_RETURN:
        case STMT_REPEAT:
        case STMT_STOP:
        case STMT_TRACE:
        case STMT_F_F:
        case STMT_F_L:
        case STMT_F_B:
        case STMT_ENDIF_INVISIBLE:
            ex = expr_new_void(mngr);
            break;
        case STMT_BLOAD:
        case STMT_BRUN:
        case STMT_TIME_S:
        case STMT_DELETE:
        case STMT_ENTER:
        case STMT_LOAD:
        case STMT_LOCK:
        case STMT_RENAME:
        case STMT_SAVE:
        case STMT_UNLOCK:
            ex = p_str_expr(&p);
            break;
        case STMT_DIR:
        case STMT_DUMP:
        case STMT_RUN:
            if( 0 == (ex = p_str_expr(&p)) )
                ex = expr_new_void(mngr);
            break;
        case STMT_COLOR:
        case STMT_FCOLOR:
        case STMT_GRAPHICS:
        case STMT_IF_MULTILINE:
        case STMT_PAUSE:
        case STMT_UNTIL:
        case STMT_WHILE:
            ex = p_num_expr(&p);
            break;
        case STMT_EXIT:
            if( 0 == (ex = p_num_expr(&p)) )
                ex = expr_new_void(mngr);
            break;
        case STMT_DRAWTO:
        case STMT_DPOKE:
        case STMT_DEL:
        case STMT_FILLTO:
        case STMT_POKE:
        case STMT_PLOT:
        case STMT_POSITION:
        case STMT_PAINT:
            ex = p_num_2_expr(&p);
            break;
        case STMT_CIRCLE:
        case STMT_MOVE:
        case STMT_N_MOVE:
        case STMT_RENUM:
        case STMT_SETCOLOR:
            ex = p_num_3_expr(&p);
            break;
        case STMT_DSOUND:
        case STMT_SOUND:
            if( 0 == (ex = p_num_4_expr(&p)) )
                ex = expr_new_void(mngr);
            break;
        case STMT_GOTO:
        case STMT_GO_TO:
        case STMT_GOSUB:
            // TODO: should parse as "line number expression"
            ex = p_num_expr(&p);
            break;
        case STMT_EXEC:
        case STMT_GO_S:
        case STMT_LBL_S:
        case STMT_PROC:
            ex = p_label_expr(&p);
            break;
        case STMT_CLOSE:
        case STMT_CLS:
            if( 0 == (ex = p_io_expr(&p)) )
                ex = expr_new_void(mngr);
            break;
        case STMT_BPUT:
        case STMT_BGET:
        case STMT_POINT:
            ex = p_io_num_2_expr(&p);
            break;
        case STMT_COM:
        case STMT_DIM:
            ex = p_dim_expr(&p);
            break;
        case STMT_XIO:
            ex = p_xio_expr(&p);
            break;
        case STMT_FOR:
            ex = p_for_expr(&p);
            break;
        case STMT_LPRINT:
            ex = p_print_expr(&p);
            break;
        case STMT_PRINT:
        case STMT_PRINT_:
            ex = p_print_io_expr(&p);
            break;
        case STMT_ON:
            ex = p_on_expr(&p);
            break;
        case STMT_NEXT:
            ex = p_var_num_expr(&p);
            break;
        case STMT_IF_THEN:
        case STMT_IF_NUMBER:
            ex = p_if_then_expr(&p);
            break;
        case STMT_LIST:
            ex = p_list_expr(&p);
            break;
        case STMT_RESTORE:
            if( 0 == (ex = p_label_or_lnum_expr(&p)) )
                ex = expr_new_void(mngr);
            break;
        case STMT_TRAP:
            ex = p_label_or_lnum_expr(&p);
            break;
        case STMT_NOTE:
            ex = p_io_varnum2_expr(&p);
            break;
        case STMT_STATUS:
            ex = p_io_varnum_expr(&p);
            break;
        case STMT_LOCATE:
            ex = p_locate_expr(&p);
            break;
        case STMT_GET:
        case STMT_P_GET:
            ex = p_get_expr(&p);
            break;
        case STMT_PUT:
        case STMT_P_PUT:
            ex = p_put_expr(&p);
            break;
        case STMT_INPUT:
            ex = p_input_expr(&p);
            break;
        case STMT_OPEN:
            ex = p_open_expr(&p);
            break;
        case STMT_READ:
            ex = p_var_numstr_comma_expr(&p);
            break;
        case STMT_TEXT:
            ex = p_text_expr(&p);
            break;
        default:
            fprintf(stderr,"OPTIMIZER ERROR: unknown statement\n");
            break;
    }

    if( ex )
    {
        if(p.pos != p.end )
        {
            fprintf(stderr,"OPTIMIZER_ERROR:%d: parse ended at: %zd before end (%x)\n", fline, p.end-p.pos, *p.pos);
        }
        return ex;
    }
    else if(len)
    {
        fprintf(stderr,"OPTIMIZER_ERROR:%d: could not parse '%s'[$%x], length was %d (%u)\n",
                fline, statements[sn].stm_long, sn, len, stmt_get_token_len(s));
        return 0;
    }
    return expr_new_void(mngr);
}
