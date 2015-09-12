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
        }
        else
        {
            // Statement
            stmt *s = line_get_statement(l);
            string_buf *sb = stmt_print_long(s, pgm_get_vars(pgm), &indent, conv_ascii);
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
    // Current line number
    int cur_line;
    // Current line number length
    int num_len;
    // Current line number is user defined
    int user_num;
    // We added a ':' at the end
    int last_colon;
    // Tokenized length
    int tok_len;
    // File
    FILE *f;
};

static void ls_set_linenum(struct ls *ls, int num)
{
    ls->cur_line = num;
    ls->num_len  = num > 9999 ? 5 :
                   num >  999 ? 4 :
                   num >   99 ? 3 :
                   num >    9 ? 2 : 1;
}

static void ls_write_line(struct ls *ls, int len, int tok_len)
{
    if( len < 0 )
    {
        // Full line
        len = ls->out->len;
        if( len && ls->last_colon )
            len --;
    }
    if( ! len && !ls->user_num )
        return;

    // Update summary info
    if( len + ls->num_len > ls->max_len )
    {
        ls->max_len = len + ls->num_len;
        ls->max_num = ls->cur_line;
    }
    ls->num_lines++;

    // Write line to output
    fprintf(ls->f, "%d",ls->cur_line);
    fwrite(ls->out->data, len, 1, ls->f);
    if( !len )
        fputc('.', ls->f); // Write a REM in an otherwise empty line
    fputc(0x9b, ls->f);

    // Delete from buffer and unset line number
    if( ls->out->len - len > 1 )
    {
        memmove(ls->out->data, ls->out->data + len + 1, ls->out->len - len - 1);
        ls->out->len -= (len + 1);
    }
    else
        ls->out->len = 0;
    ls_set_linenum(ls, ls->cur_line + 1);
    ls->user_num = 0;
    ls->last_colon = 0;
    ls->tok_len -= tok_len;
    if( ls->tok_len < 0 )
        ls->tok_len = 0;
}

void lister_list_program_short(FILE *f, program *pgm, int max_line_len)
{
    struct ls ls;
    ls.max_len = ls.max_num = ls.num_lines = 0;
    ls.out = sb_new();
    ls.num_len = 0;
    ls.tok_len = 0;
    ls.f = f;
    ls.cur_line = -1;
    ls.last_colon = 0;
    ls.user_num = 0;
    int no_split = 0;
    int last_split = 0, last_tok_len = 0;

    // For each line:
    line **lp;
    for( lp = pgm_get_lines(pgm); *lp != 0; ++lp )
    {
        line *l = *lp;

        // Adds a new statement (if any)
        if( !line_is_num(l) )
        {
            // Statement
            stmt *s = line_get_statement(l);
            int skip_colon = 0;
            string_buf *sb = stmt_print_short(s, pgm_get_vars(pgm), &skip_colon, &no_split);
            if( sb->len )
            {
                // If we have statements before any line, start at '0'
                if( ls.cur_line < 0 )
                {
                    ls_set_linenum(&ls, 0);
                    ls.user_num = 1;
                }

                if( !skip_colon )
                    sb_put(sb, ':');

                // Get tokenized length
                int bas_len = stmt_get_bas_len(s);
                if( bas_len >= 0xFB )
                {
                    string_buf *prn = stmt_print_alone(s, pgm_get_vars(pgm));
                    err_print("statement too long at line %d:\nerror:  %s\n", ls.cur_line, prn->data);
                    sb_delete(prn);
                }

                // Split before a label (write full curren line)
                if( stmt_is_label(s) && ls.out->len )
                    ls_write_line(&ls, -1, ls.tok_len);
                // See if line is too big to join with last line
                else if( ((ls.tok_len + 1 + bas_len) > 0xFC) ||
                         ((ls.out->len + ls.num_len - !skip_colon + sb->len) > max_line_len) )
                {
                    if( !last_split )
                    {
                        err_print("line number %d can not be split to shorter size (current len %d).\n", ls.cur_line, ls.out->len + sb->len);
                    }
                    else
                        ls_write_line(&ls, last_split, last_tok_len);
                }
                // Add statement
                sb_cat(ls.out, sb);
                ls.tok_len += 1 + bas_len;
                ls.last_colon = !skip_colon;
                if( !no_split )
                {
                    last_split = ls.out->len - ls.last_colon;
                    last_tok_len = ls.tok_len;
                }
            }
            sb_delete(sb);
        }
        else
        {
            // A line break, (full) output current line
            ls_write_line(&ls, -1, ls.tok_len);
            // Get new line number
            int need_line = line_get_num(l);
            if( need_line >= 0 )
            {
                ls_set_linenum( &ls, need_line );
                ls.user_num = 1;
            }
            // Set split point to current position
            last_split = ls.out->len;
            last_tok_len = ls.tok_len;
        }
    }
    // Output last line
    if( ls.out->len || ls.user_num )
        ls_write_line(&ls, -1, ls.tok_len);

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
