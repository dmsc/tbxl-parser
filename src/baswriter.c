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
#include "baswriter.h"
#include "vars.h"
#include "expr.h"
#include "basexpr.h"
#include "listexpr.h"
#include "program.h"
#include "sbuf.h"
#include "dbg.h"
#include "statements.h"
#include "tokens.h"
#include <string.h>
#include <stdlib.h>

static void put16(FILE *f, unsigned n)
{
    putc(n&0xFF, f);
    putc(n>>8, f);
}

// Holds bas-writer status
struct bw {
    // Summary variables
    int max_len, max_num, num_lines;
    // TOK buffer
    string_buf *toks;
};

static int bas_add_line(struct bw *bw, int num, int valid, string_buf *tok_line, int replace_colon,
                        const char *fname, int file_line)
{
    if( !tok_line->len && !valid )
        return 0; // Skip

    // Verify line number
    if( num < 0 || num >= 32768 )
    {
        err_print(fname, file_line, "line number %d invalid\n", num);
        return 1;
    }

    // Check for empty lines
    if( !tok_line->len )
    {
        // Output at least a single "--" comment to preserve the line number
        // as we don't know if it is used in some GOTO
        sb_put(tok_line, 5);
        sb_put(tok_line, STMT_REM_);
    }
    // Transform last COLON to EOL if needed
    // TODO: this should be done at statement build time, here it is a hack
    else if( replace_colon )
        tok_line->data[tok_line->len-1] = 0x10 + TOK_EOL;

    // Write the complete line
    unsigned ln = tok_line->len + 3;
    if( ln > 0xFF )
    {
        err_print(fname, file_line, "line %d too long\n", num);
        return 1;
    }
    sb_put(bw->toks, num & 0xFF);
    sb_put(bw->toks, (num >> 8)&0xFF);
    sb_put(bw->toks, ln);
    sb_cat(bw->toks, tok_line);

    if( ln > bw->max_len )
    {
        bw->max_len = ln;
        bw->max_num = num;
    }
    bw->num_lines ++;
    return 0;
}

int bas_write_program(FILE *f, program *pgm, int variables)
{
    // Input file name, used for debugging
    const char *fname = pgm_get_file_name(pgm);

    // Main program info
    struct bw bw;

    bw.max_len = 0;
    bw.max_num = 0;
    bw.num_lines = 0;
    bw.toks = sb_new();

    // BAS file has 4 parts:
    // 1) HEADER
    // 2) Variable Name Table (VNT)
    // 3) Variable Value Table (VVT)
    // 4) Tokens

    // First, serialize variables
    vars *v = pgm_get_vars(pgm);
    string_buf *vnt = sb_new();
    string_buf *vvt = sb_new();
    int i, nvar = vars_get_total(v);
    for(i=0; i<nvar; i++)
    {
        // We write all variables undimmed, BASIC fills the
        // correct values on RUN.
        enum var_type t = vars_get_type(v, i);
        if( t == vtNone )
            continue;
        switch( t )
        {
            case vtNone:
            case vtMaxType:
                break;     // Skip
            case vtFloat:
                sb_put(vvt, 0x00); // Type
                sb_put(vvt, i);    // Number
                sb_put(vvt, 0);    // BCD (1)
                sb_put(vvt, 0);    // BCD (2)
                sb_put(vvt, 0);    // BCD (3)
                sb_put(vvt, 0);    // BCD (4)
                sb_put(vvt, 0);    // BCD (5)
                sb_put(vvt, 0);    // BCD (6)
                break;
            case vtString:
                sb_put(vvt, 0x80); // Type  (80 = un-dimmed, 81 = dimmed)
                sb_put(vvt, i);    // Number
                sb_put(vvt, 0);    // ADDRESS (LO)
                sb_put(vvt, 0);    // ADDRESS (HI)
                sb_put(vvt, 0);    // LENGTH  (LO)
                sb_put(vvt, 0);    // LENGTH  (HI)
                sb_put(vvt, 0);    // SIZE    (LO)
                sb_put(vvt, 0);    // SIZE    (HI)
                break;
            case vtArray:
                sb_put(vvt, 0x40); // Type  (80 = un-dimmed, 81 = dimmed)
                sb_put(vvt, i);    // Number
                sb_put(vvt, 0);    // ADDRESS  (LO)
                sb_put(vvt, 0);    // ADDRESS  (HI)
                sb_put(vvt, 0);    // LENGTH 1 (LO)
                sb_put(vvt, 0);    // LENGTH 1 (HI)
                sb_put(vvt, 0);    // LENGTH 2 (LO)
                sb_put(vvt, 0);    // LENGTH 2 (HI)
                break;
            case vtLabel:
                sb_put(vvt, 0xC0); // Type (C0 = missing, C1 = #label, C2 = PROC)
                sb_put(vvt, i);    // Number
                sb_put(vvt, 0);    // ADDRESS (LO)
                sb_put(vvt, 0);    // ADDRESS (HI)
                sb_put(vvt, 0);    // -
                sb_put(vvt, 0);    // -
                sb_put(vvt, 0);    // -
                sb_put(vvt, 0);    // -
                break;
        }
        if( variables >= 0 )
        {
            // Write NAME
            const char *name;
            if( !variables )
                name = vars_get_short_name(v, i);
            else
                name = vars_get_long_name(v, i);
            while( name[1] )
            {
                sb_put(vnt, *name );
                name++;
            }
            if( t == vtArray )
            {
                sb_put(vnt, *name);
                sb_put(vnt, '(' | 0x80);
            }
            else if( t == vtString )
            {
                sb_put(vnt, *name);
                sb_put(vnt, '$' | 0x80);
            }
            else
                sb_put(vnt, *name | 0x80);
        }
    }
    // VNT must terminate with a 0
    sb_put(vnt, 0);
    // Serialize statements
    int cur_line = 0;
    int line_valid = 0;
    int last_colon = 0;
    int no_split = 0;
    int file_line = 0;
    string_buf *bin_line = sb_new();
    // For each line/statement:
    for(const expr *ex = pgm_get_expr(pgm); ex != 0 ; ex = ex->lft)
    {
        if( ex->type == et_lnum )
        {
            // Append old line
            int old_len = bin_line->len;
            if( bas_add_line(&bw, cur_line, line_valid, bin_line, last_colon, fname, file_line) )
                return 1;
            sb_delete(bin_line);
            file_line = ex->file_line;
            bin_line = sb_new();
            if( ex->num < 0 )
            {
                // This is a fake DATA line.
                if( line_valid || old_len )
                    cur_line ++;
                line_valid = 0;
            }
            else if( (old_len || line_valid) && ex->num <= cur_line )
            {
                err_print(fname, ex->file_line,
                          "line number %.0f already in use, current free number is %d\n",
                          ex->num, 1 + cur_line);
                sb_delete(bin_line);
                sb_delete(vnt);
                sb_delete(vvt);
                sb_delete(bw.toks);
                return 1;
            }
            else
            {
                cur_line = ex->num;
                line_valid = 1;
            }
            last_colon = 0;
        }
        else
        {
            // Serialize statement
            int old_last_colon = last_colon;
            string_buf *sb = expr_get_bas(ex, &last_colon, &no_split);
            if( sb->len >= 0xFB )
            {
                string_buf *prn = expr_print_alone(ex);
                err_print(fname, ex->file_line, "statement too long at line %d:\n", cur_line);
                err_print(fname, ex->file_line, "'%.*s'\n", prn->len, prn->data);
                sb_delete(bin_line);
                sb_delete(prn);
                sb_delete(sb);
                sb_delete(vnt);
                sb_delete(vvt);
                sb_delete(bw.toks);
                return 1;
            }
            if( sb->len )
            {
                unsigned ln = sb->len + bin_line->len + 4;
                if( ln > 0xFF || (expr_is_label(ex) && bin_line->len>0) )
                {
                    if( no_split > 0 )
                    {
                        err_print(fname, ex->file_line, "forcing splitting of line %d will produce invalid code.\n",
                                  cur_line);
                        err_print(fname, ex->file_line, "please, retry after manually inserting a line.\n");
                    }
                    // We can't add this statement to the current line,
                    // write the old line and create a new line
                    if( bas_add_line(&bw, cur_line, line_valid, bin_line, old_last_colon, fname, file_line) )
                    {
                        sb_delete(bin_line);
                        sb_delete(sb);
                        sb_delete(vnt);
                        sb_delete(vvt);
                        sb_delete(bw.toks);
                        return 1;
                    }
                    sb_delete(bin_line);
                    file_line = ex->file_line;
                    bin_line = sb_new();
                    cur_line = cur_line + 1;
                    line_valid = 0;
                    ln = sb->len + bin_line->len + 4;
                }
                if( sb->len )
                {
                    sb_put(bin_line, ln);
                    sb_cat(bin_line, sb);
                }
            }
            sb_delete(sb);
        }
    }
    // Append last line
    if( bas_add_line(&bw, cur_line, line_valid, bin_line, last_colon, fname, file_line) )
        return 1;
    sb_delete(bin_line);
    // Now, adds a standard immediate line: SAVE "D:X"
    sb_write(bw.toks, (unsigned char *)"\x00\x80\x0b\x0b\x19\x0f\x03\x44\x3a\x58\x16", 11);
    // Verify sizes
    if( vvt->len + vnt->len + bw.toks->len > 0x9500 )
    {
        unsigned len = vvt->len + vnt->len + bw.toks->len;
        err_print(fname, 0, "program too big, %d bytes ($%04X)\n", len, len);
        err_print(fname, 0, "VNT SIZE:%u\n", vnt->len);
        err_print(fname, 0, "VVT SIZE:%u\n", vvt->len);
        err_print(fname, 0, "TOK SIZE:%u\n", bw.toks->len);
        return 1;
    }
    // Write
    put16(f, 0);
    put16(f, 0x100);
    put16(f, 0x0FF + vnt->len);
    put16(f, 0x100 + vnt->len);
    put16(f, 0x100 + vnt->len + vvt->len);
    put16(f, 0x100 + vnt->len + vvt->len + bw.toks->len - 11);
    put16(f, 0x100 + vnt->len + vvt->len + bw.toks->len);
    fwrite( vnt->data, vnt->len, 1, f);
    fwrite( vvt->data, vvt->len, 1, f);
    fwrite( bw.toks->data, bw.toks->len, 1, f);

    // Output summary info
    if( do_debug )
    {
        fprintf(stderr,"Binary Tokenized output information:\n"
                       " Number of lines written: %d\n"
                       " Maximum line length: %d bytes at line %d\n"
                       " VNT (variable name table) : %u bytes\n"
                       " VVT (variable value table): %u bytes\n"
                       " TOK (tokenized program)   : %u bytes\n"
                       " Total program size: %d bytes\n",
                       bw.num_lines, bw.max_len, bw.max_num,
                       vnt->len, vvt->len, bw.toks->len,
                       14 + vnt->len + vvt->len + bw.toks->len);
    }
    sb_delete(vnt);
    sb_delete(vvt);
    sb_delete(bw.toks);
    return 0;
}
