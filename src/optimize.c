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

#include "optimize.h"
#include "optexpr.h"
#include "optparse.h"
#include "optconst.h"
#include "vars.h"
#include "line.h"
#include "program.h"
#include "stmt.h"
#include "statements.h"
#include <stdio.h>

static stmt * optimize_statement(program *pgm, stmt *s, int fline)
{
    // Create output statement
    stmt *ret = stmt_new(stmt_get_statement(s));

    if( stmt_is_text(s) )
    {
        // Text statement, simply copy over
        stmt_add_data(ret, (const char *)stmt_get_token_data(s), stmt_get_token_len(s));
        return ret;
    }

    // Parse statement tokens:
    expr_mngr *mngr = expr_mngr_new(pgm);
    expr_mngr_set_file_name(mngr, pgm_get_file_name(pgm));
    expr_mngr_set_file_line(mngr, fline);
    expr *ex = opt_parse_statement(pgm, mngr, s, fline);

    if( ex )
    {
        opt_constprop(ex);
        opt_convert_tok(ex);
        expr_to_tokens(ex, ret);
#if 0
        enum enum_statements sn = stmt_get_statement(s);
        unsigned len  = stmt_get_token_len(s);
        uint8_t *data = stmt_get_token_data(s);
        enum enum_statements osn = stmt_get_statement(ret);
        unsigned olen  = stmt_get_token_len(ret);
        uint8_t *odata = stmt_get_token_data(ret);
        unsigned i;
        if( len != olen || memcmp(data,odata,len) || sn != osn )
        {
            fprintf(stderr,"in: %02x:", sn);
            for(i=0; i<len; i++)
                fprintf(stderr," %02x", data[i]&0xFF);
            fprintf(stderr,"\n");

            fprintf(stderr,"ot: %02x:", stmt_get_statement(ret));
            for(i=0; i<olen; i++)
                fprintf(stderr," %02x", odata[i]&0xFF);
            fprintf(stderr,"\n");
        }
#endif
    }

    expr_mngr_delete(mngr);
    return ret;
}

program *optimize_program(program *pgm, int level)
{
    program *ret = program_new(pgm_get_file_name(pgm));
    // For each line/statement:
    line **lp;
    for( lp = pgm_get_lines(pgm); *lp != 0; ++lp )
    {
        line *l = *lp;
        if( line_is_num(l) )
        {
            // Duplicate and add to new program
            line * nl = line_new_linenum(line_get_num(l), line_get_file_line(l));
            pgm_add_line(ret,nl);
        }
        else
        {
            // Serialize statement
            int fl = line_get_file_line(l);
            stmt *s = line_get_statement(l);
            stmt *ns = optimize_statement(pgm, s, fl);
            line * nl = line_new_from_stmt(ns, fl);
            pgm_add_line(ret,nl);
        }
    }
    // Copy variables
    vars *v = pgm_get_vars(pgm);
    vars *vret = pgm_get_vars(ret);
    int i;
    for(i=0; i<256; i++) // Up to 256 vars
    {
        // We write all variables undimmed, BASIC fills the
        // correct values on RUN.
        enum var_type t = vars_get_type(v, i);
        if( t == vtNone )
            break;
        const char *l_name = vars_get_long_name(v, i);
        vars_new_var(vret, l_name, t, "", -1);
    }

    return ret;
}
