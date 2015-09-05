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
#include "lister.h"
#include "line.h"
#include "program.h"
#include "stmt.h"
#include "sbuf.h"
#include "dbg.h"
#include <string.h>
#include <stdlib.h>

typedef struct lister {
    int indent;
} lister;

void lister_list_program_long(FILE *f, program *pgm, int conv_ascii)
{
    int indent = 0;
    int skip_colon = 1;
    line **lp;
    // For each line:
    for( lp = pgm_get_lines(pgm); *lp != 0; ++lp )
    {
        line *l = *lp;
        // Test if it is a line number or a statement
        if( line_is_num(l) )
        {
            // Number
            if( line_get_num(l) >= 0 )
                fprintf(f, "%d\n", line_get_num(l) );
            skip_colon = 1;
        }
        else
        {
            // Statement
            skip_colon = 0;
            stmt *s = line_get_statement(l);
            string_buf *sb = stmt_print_long(s, pgm_get_vars(pgm), &indent, &skip_colon, conv_ascii);
            if( sb && sb->len )
            {
                putc('\t', f);
                fwrite(sb->data, sb->len, 1, f);
                putc('\n', f);
            }
            sb_delete(sb);
        }
    }
}

// Holds short-lister status
struct ls {
    // Summary variables
    int max_len, max_num, num_lines;
    // Out buffer
    string_buf *out;
    // Current line
    int cur_line;
    // Tokenized length
    int tok_len;
    // File
    FILE *f;
};

static void ls_write_line(struct ls *ls, int len)
{
    // Update summary info
    if( len > ls->max_len )
    {
        ls->max_len = len;
        ls->max_num = ls->cur_line;
    }
    ls->num_lines++;
    fwrite(ls->out->data, len, 1, ls->f);
    fputc(0x9b, ls->f);
}

void lister_list_program_short(FILE *f, program *pgm)
{
    struct ls ls;
    ls.max_len = ls.max_num = ls.num_lines = 0;
    ls.out = sb_new();
    ls.tok_len = 0;
    ls.f = f;
    ls.cur_line = -1;
    int skip_colon = 1;
    int last_split = 0, last_tok_len = 0, need_line = -1;

    // For each line:
    line **lp;
    for( lp = pgm_get_lines(pgm); *lp != 0; ++lp )
    {
        line *l = *lp;

        // Adds a new statement (if any)
        if( !line_is_num(l) )
        {
            // Statement
            int last_skip = skip_colon;
            stmt *s = line_get_statement(l);
            string_buf *sb = stmt_print_short(s, pgm_get_vars(pgm), &skip_colon);
            if( sb->len )
            {
                // If we have statements before any line, start at '0'
                if( ls.cur_line < 0 )
                {
                    ls.cur_line = 0;
                    sb_put(ls.out,'0');
                }
                if( !last_skip )
                    sb_put(ls.out, ':');
                sb_cat(ls.out, sb);
                // Update the tokenized length by converting to BAS
                int end_colon = 0;
                string_buf *tok = stmt_get_bas(s, pgm_get_vars(pgm), &end_colon);
                ls.tok_len += 1 + tok->len;
                if( tok->len >= 0xFB )
                {
                    string_buf *prn = stmt_print_alone(s, pgm_get_vars(pgm));
                    err_print("statement too long at line %d:\nerror:  %s\n", ls.cur_line, prn->data);
                    sb_delete(prn);
                }
                sb_delete(tok);
            }
            sb_delete(sb);
            // If current line is too long or if last statement was a label,
            // we need a new line number.
            if( (ls.tok_len > 0xFC && last_split) || (ls.out->len >= 120 && last_split) || (!last_skip && stmt_is_label(s)) )
                need_line = ls.cur_line + 1;
            else if( !last_skip )
            {
                last_split = ls.out->len;
                last_tok_len = ls.tok_len;
            }
        }
        else
        {
            need_line = line_get_num(l);
            if( need_line == -1 )
            {
                // This is a "fake" line, added because we had a DATA statement.
                // We simply advance the line counter if the next statement is not a line number
                if( lp[1] && !line_is_num(lp[1]) )
                    need_line = ls.cur_line + 1;
            }
            last_split = ls.out->len;
        }

        // Test if we need to output a new line number
        if( need_line >= 0 )
        {
            // Check if needed line number is valid
            if( need_line <= ls.cur_line )
            {
                err_print("line number %d is already used at %d.\n", need_line, ls.cur_line);
                need_line = ls.cur_line +1;
            }
            if( ls.out->len )
            {
                if( !last_split )
                {
                    err_print("line number %d can not be split to shorter size.\n", ls.cur_line);
                    last_split = ls.out->len;
                }
                // Output the part of the line
                ls_write_line(&ls, last_split);
            }

            // Go to next line
            ls.cur_line = need_line;
            string_buf *nbuf = sb_new();
            sb_put_dec(nbuf, ls.cur_line);

            // Copy the last part of last line here
            if( ls.out->len - last_split > 1 )
                sb_write(nbuf, (unsigned char *)ls.out->data + last_split + 1, ls.out->len - last_split - 1);
            else
                skip_colon = 1;

            // Replace with new line
            sb_delete(ls.out);
            ls.out = nbuf;

            // Redo size
            ls.tok_len -= last_tok_len;

            need_line = -1;
            last_split = 0;
            last_tok_len = 0;
        }
    }
    // Output last line
    if( ls.out->len )
        ls_write_line(&ls, ls.out->len);

    sb_delete(ls.out);
    // Output summary info
    if( do_debug )
    {
        fprintf(stderr,"Short list information:\n"
                       " Number of lines written: %d\n"
                       " Maximum line length: %d bytes at line %d\n",
                       ls.num_lines, ls.max_len, ls.max_num );
    }
}
