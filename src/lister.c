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
#include "listexpr.h"
#include "basexpr.h"
#include "expr.h"
#include "program.h"
#include "sbuf.h"
#include "dbg.h"
#include <string.h>
#include <stdlib.h>

typedef struct lister {
    int indent;
} lister;

int lister_list_program_long(FILE *f, program *pgm, int conv_ascii)
{
    int indent = 0;

    // Print all used defs
    {
        string_buf *sb = expr_print_used_defs(pgm_get_expr(pgm));
        if( sb )
        {
            fprintf(f, "\t' Definitions\n");
            sb_fwrite(sb, f);
            putc('\n', f);
            sb_delete(sb);
        }
    }

    // For each line/statement:
    for(const expr *ex = pgm_get_expr(pgm); ex != 0 ; ex = ex->lft)
    {
        // Test if it is a line number or a statement
        if( ex->type == et_lnum )
        {
            // Number
            if( ex->num >= 0 )
                fprintf(f, "%.0f\n", ex->num );
        }
        else
        {
            // Statement
            string_buf *sb = expr_print_long(ex, &indent, conv_ascii);
            if( sb && sb_len(sb) )
            {
                putc('\t', f);
                sb_fwrite(sb, f);
                putc('\n', f);
            }
            sb_delete(sb);
        }
    }
    return 0;
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
    // Original file name
    const char *fname;
    // Original file line
    int file_line;
    // File
    FILE *f;
};

static int ls_set_linenum(struct ls *ls, int num)
{
    if( num < ls->cur_line )
    {
        err_print(ls->fname, ls->file_line,
                  "line number %d already in use, current free number is %d\n", num, ls->cur_line);
        return 1;
    }
    ls->cur_line = num;
    // We can use scientific notation for line numbers
    if( num > 9999 && 0 == num % 10000 )
        ls->num_len = 3;
    else if( num > 999 && 0 == num % 1000 )
        ls->num_len = num > 9999 ? 4 : 3;
    else
        ls->num_len  = num > 9999 ? 5 :
                       num >  999 ? 4 :
                       num >   99 ? 3 :
                       num >    9 ? 2 : 1;
    return 0;
}

static void ls_write_line(struct ls *ls, int len, int tok_len)
{
    if( len < 0 )
    {
        // Full line
        len = sb_len(ls->out);
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
    if( ls->cur_line > 9999 && ls->cur_line % 10000 == 0 )
        fprintf(ls->f, "%dE4", ls->cur_line / 10000);
    else if( ls->cur_line > 999 && ls->cur_line % 1000 == 0 )
        fprintf(ls->f, "%dE3", ls->cur_line / 1000);
    else
        fprintf(ls->f, "%d",ls->cur_line);
    fwrite(sb_data(ls->out), len, 1, ls->f);
    if( !len )
        fputs(" .", ls->f); // Write a REM in an otherwise empty line
    fputc(0x9b, ls->f);

    // Delete from buffer and unset line number
    if( sb_len(ls->out) - len > 1 )
        sb_erase(ls->out, 0, len + 1);
    else
        sb_clear(ls->out);
    ls_set_linenum(ls, ls->cur_line + 1);
    ls->user_num = 0;
    ls->last_colon = 0;
    ls->tok_len -= tok_len;
    if( ls->tok_len < 0 )
        ls->tok_len = 0;
}

int lister_list_program_short(FILE *f, program *pgm, unsigned max_line_len)
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
    ls.fname = pgm_get_file_name(pgm);
    ls.file_line = 0;
    int no_split = 0;
    int last_split = 0, last_tok_len = 0;
    int return_error = 0;

    // For each line/statement:
    for(const expr *ex = pgm_get_expr(pgm); ex != 0 ; ex = ex->lft)
    {
        // Adds a new statement (if any)
        if( ex->type != et_lnum )
        {
            // Statement
            int skip_colon = 0;
            string_buf *sb = expr_print_short(ex, &skip_colon, &no_split);
            if( sb_len(sb) )
            {
                // If we have statements before any line, start at '0'
                if( ls.cur_line < 0 )
                {
                    ls_set_linenum(&ls, 0);
                    ls.user_num = 1;
                }

                if( !skip_colon )
                    sb_put(sb, ':');

                ls.file_line = ex->file_line;
                // Get tokenized length
                unsigned bas_len = expr_get_bas_len(ex);
                unsigned maxlen = expr_get_bas_maxlen(ex);
                if( bas_len + 4 >= maxlen )
                {
                    string_buf *prn = expr_print_alone(ex);
                    err_print(ls.fname, ls.file_line, "statement too long at line %d:\n", ls.cur_line);
                    err_print(ls.fname, ls.file_line, "'%.*s'\n", sb_len(prn), sb_data(prn));
                    sb_delete(prn);
                    return_error = 1;
                }

                // Split before a label (write full curren line)
                if( expr_is_label(ex) && sb_len(ls.out) )
                    ls_write_line(&ls, -1, ls.tok_len);
                // See if line is too big to join with last line
                else if( ((ls.tok_len + 4 + bas_len) > maxlen) ||
                         ((sb_len(ls.out) + ls.num_len - !skip_colon + sb_len(sb)) > max_line_len) )
                {
                    if( !last_split )
                    {
                        err_print(ls.fname, ls.file_line,
                                  "can't split line %d to shorter size (current len %d chars, %d bytes)\n",
                                  ls.cur_line, sb_len(ls.out) + sb_len(sb), ls.tok_len + 1 + bas_len);
                        return_error = 1;
                    }
                    else
                    {
                        ls_write_line(&ls, last_split, last_tok_len);
                        last_split = 0;
                    }
                }
                // Add statement
                sb_cat(ls.out, sb);
                ls.tok_len += 1 + bas_len;
                ls.last_colon = !skip_colon;
                if( !no_split )
                {
                    last_split = sb_len(ls.out) - ls.last_colon;
                    last_tok_len = ls.tok_len;
                }
            }
            sb_delete(sb);
        }
        else
        {
            // A line break, (full) output current line
            ls_write_line(&ls, -1, ls.tok_len);
            // Update file line
            ls.file_line = ex->file_line;
            // Get new line number
            int need_line = ex->num;
            if( need_line >= 0 )
            {
                if( ls_set_linenum( &ls, need_line ) )
                    return_error = 1;
                ls.user_num = 1;
            }
            // Set split point to current position
            last_split = sb_len(ls.out);
            last_tok_len = ls.tok_len;
        }
    }
    // Output last line
    if( sb_len(ls.out) || ls.user_num )
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
    return return_error;
}
