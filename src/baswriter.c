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
#include "parser.h"
#include "program.h"
#include "sbuf.h"
#include "dbg.h"
#include "statements.h"
#include "tokens.h"
#include <assert.h>
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
    unsigned max_len, num_lines;
    int max_num;
    // TOK buffer
    string_buf *toks;
};

// Build a binary line from a list of concatenated statements
static int bas_add_line(struct bw *bw, int num, int valid, string_buf *tok_line, unsigned len,
                        int replace_colon, const char *fname, int file_line)
{
    if( !len && !valid )
        return 0; // Skip

    // Verify line number
    if( num < 0 || num >= 32768 )
    {
        sb_erase(tok_line, 0, len);
        err_print(fname, file_line, "line number %d invalid\n", num);
        return 1;
    }

    // Check for empty lines
    if( !len )
    {
        // Output at least an empty comment to preserve the line number
        // as we don't know if it is used in some GOTO
        if( parser_get_dialect() == parser_dialect_turbo )
        {
            // In TurboBasic XL, it is smaller to use the '--' comment.
            sb_put(tok_line, 1);
            sb_put(tok_line, STMT_REM_);
            len = 2;
        }
        else
        {
            sb_put(tok_line, 2);
            sb_put(tok_line, STMT_REM);
            sb_put(tok_line, '\x9B');
            len = 3;
        }
    }
    // Transform last COLON to EOL if needed
    // TODO: this should be done at statement build time, here it is a hack
    else if( replace_colon )
        sb_set_char(tok_line, len - 1, 0x10 + TOK_EOL);

    // Write the complete line
    if( len > 0xFF - 3 )
    {
        sb_erase(tok_line, 0, len);
        err_print(fname, file_line, "line %d too long: %d\n", num, len);
        return 1;
    }
    sb_put(bw->toks, num & 0xFF);
    sb_put(bw->toks, (num >> 8)&0xFF);
    sb_put(bw->toks, len + 3);

    // Concatenate tokens:
    const uint8_t *data = (const uint8_t *)sb_data(tok_line);
    for(unsigned i = 0; i < len; i = i + data[i] + 1)
    {
        sb_put(bw->toks, data[i] + i + 4);
        sb_write(bw->toks, &data[i + 1], data[i]);
    }

    if( len + 3 > bw->max_len )
    {
        bw->max_len = len + 3;
        bw->max_num = num;
    }
    bw->num_lines ++;
    // Remove tokens from output
    sb_erase(tok_line, 0, len);
    return 0;
}

int bas_write_program(FILE *f, program *pgm, int variables, unsigned max_line_len)
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
    int nvar = vars_get_total(v);
    if( nvar > 128 && parser_get_dialect() != parser_dialect_turbo )
    {
        err_print(fname, 0, "too many variables, Atari BAS format support only 128.\n");
        return 1;
    }
    if( nvar > 256 )
    {
        err_print(fname, 0, "too many variables, Turbo BAS format support only 256.\n");
        return 1;
    }
    string_buf *vnt = sb_new();
    string_buf *vvt = sb_new();
    for(int i=0; i<nvar; i++)
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
                sb_put(vnt, '(' | '\x80');
            }
            else if( t == vtString )
            {
                sb_put(vnt, *name);
                sb_put(vnt, '$' | '\x80');
            }
            else
                sb_put(vnt, *name | '\x80');
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
    // Currently assembled line
    string_buf *bin_line = sb_new();
    // Last position where the line can be split
    unsigned last_split = 0;
    // Error to return at end
    unsigned error_return = 0;
    // For each line/statement:
    for(const expr *ex = pgm_get_expr(pgm); ex != 0 ; ex = ex->lft)
    {
        if( ex->type == et_lnum )
        {
            // Append complete line, ignore splitting point
            int old_len = sb_len(bin_line);
            if( bas_add_line(&bw, cur_line, line_valid, bin_line, old_len, last_colon, fname, file_line) )
                error_return = 1;
            last_split = 0;
            file_line = ex->file_line;
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
                error_return = 1;
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
            // Add the new data to the pending part
            string_buf *sb = expr_get_bas(ex, &last_colon, &no_split);
            unsigned maxlen = expr_get_bas_maxlen(ex);
            if( maxlen > max_line_len )
                maxlen = max_line_len;
            // Check: tokens + 4 (line number (2) + length + EOL) >= max line len.
            if( sb_len(sb) + 4 > maxlen )
            {
                string_buf *prn = expr_print_alone(ex);
                err_print(fname, ex->file_line, "statement too long at line %d:\n", cur_line);
                err_print(fname, ex->file_line, "'%.*s'\n", sb_len(prn), sb_data(prn));
                error_return = 1;
                sb_clear(sb);
            }
            // Add statement
            if( sb_len(sb) )
            {
                sb_put(bin_line, sb_len(sb));
                sb_cat(bin_line, sb);
            }
            sb_delete(sb);
            // Total length = tokens + 3 (line number + length)
            if( sb_len(bin_line) + 3 > maxlen ||
                    (expr_is_label(ex) && last_split > 0) )
            {
                // We can't add this statement to the current line,
                // write the old line and create a new line
                if( !last_split )
                {
                    err_print(fname, ex->file_line,
                            "can't split line %d to shorter size (current size %d bytes)\n",
                            cur_line, sb_len(bin_line) + 3);
                    sb_clear(bin_line);
                    error_return = 1;
                }
                else
                {
                    if( bas_add_line(&bw, cur_line, line_valid, bin_line, last_split,
                                old_last_colon, fname, file_line) )
                        error_return = 1;
                    last_split = 0;
                    file_line = ex->file_line;
                    cur_line = cur_line + 1;
                    line_valid = 0;
                }
            }
            if( !no_split )
                last_split = sb_len(bin_line);
        }
    }
    assert(last_split == sb_len(bin_line));
    // Append last line
    if( bas_add_line(&bw, cur_line, line_valid, bin_line, last_split, last_colon, fname, file_line) )
        error_return = 1;
    sb_delete(bin_line);
    // Now, adds a standard immediate line: CSAVE
    static unsigned char immediate_line[] = { 0x00, 0x80, 0x06, 0x06, 0x34, 0x16 };
    sb_write(bw.toks, immediate_line, sizeof(immediate_line));
    // Verify sizes
    if( sb_len(vvt) + sb_len(vnt) + sb_len(bw.toks) > 0x9500 )
    {
        unsigned len = sb_len(vvt) + sb_len(vnt) + sb_len(bw.toks);
        err_print(fname, 0, "program too big, %d bytes ($%04X)\n", len, len);
        err_print(fname, 0, "VNT SIZE:%u\n", sb_len(vnt));
        err_print(fname, 0, "VVT SIZE:%u\n", sb_len(vvt));
        err_print(fname, 0, "TOK SIZE:%u\n", sb_len(bw.toks));
        error_return = 1;
    }
    // Write
    put16(f, 0);
    put16(f, 0x100);
    put16(f, 0x0FF + sb_len(vnt));
    put16(f, 0x100 + sb_len(vnt));
    put16(f, 0x100 + sb_len(vnt) + sb_len(vvt));
    put16(f, 0x100 + sb_len(vnt) + sb_len(vvt) + sb_len(bw.toks) - sizeof(immediate_line));
    put16(f, 0x100 + sb_len(vnt) + sb_len(vvt) + sb_len(bw.toks));
    sb_fwrite(vnt, f);
    sb_fwrite(vvt, f);
    sb_fwrite(bw.toks, f);

    // Output summary info
    if( do_debug && !error_return )
    {
        fprintf(stderr,"Binary Tokenized output information:\n"
                       " Number of lines written: %u\n"
                       " Maximum line length: %u bytes at line %d\n"
                       " VNT (variable name table) : %u bytes\n"
                       " VVT (variable value table): %u bytes\n"
                       " TOK (tokenized program)   : %u bytes\n"
                       " Total program size: %d bytes\n",
                       bw.num_lines, bw.max_len, bw.max_num,
                       sb_len(vnt), sb_len(vvt), sb_len(bw.toks),
                       14 + sb_len(vnt) + sb_len(vvt) + sb_len(bw.toks));
    }
    sb_delete(vnt);
    sb_delete(vvt);
    sb_delete(bw.toks);
    return error_return;
}
