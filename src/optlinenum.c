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

#include "optlinenum.h"
#include "expr.h"
#include "dbg.h"
#include <assert.h>
#include <stdlib.h>

// Simplify printing warnings
#define warn(...) \
    do { warn_print(expr_get_file_name(ex), expr_get_file_line(ex), __VA_ARGS__); } while(0)

static void bitmap_set(uint8_t *bmp, int n)
{
    assert(n>=0 && n<32768);
    bmp[n>>3] |= (1 << (n&7));
}

static int bitmap_get(uint8_t *bmp, int n)
{
    assert(n>=0 && n<32768);
    return 0 != (bmp[n>>3] & (1 << (n&7)));
}

static int expr_is_cnum(expr *ex)
{
    return ex && (ex->type == et_c_number || ex->type == et_c_hexnumber);
}

// Check target line number, mark for "keep" if valid.
// if "range", use full range up to 65535, if "ignore", ignore if line
// is not in the program.
static int verify_target_line(expr *ex, uint8_t *keep, uint8_t *avail, int range, int ignore)
{
    assert(ex != 0);
    if( expr_is_cnum(ex) )
    {
        double limit = range ? 65535.5 : 32767.5;
        double lnum = ex->num;
        if( lnum < 0 || lnum >= limit )
            warn("invalid target line number %g, ignored.\n", lnum);
        else if( lnum < 32767.5 )
        {
            int inum = (int)(lnum + 0.5);
            if( !bitmap_get(avail, inum) )
                warn("target line number %d not in the program.\n", inum);
            bitmap_set(keep, inum);
        }
        return 0;
    }
    else
    {
        warn("target line number not constant, disabling line number optimization.\n");
        return 1;
    }
}

static int do_search_stmt(expr *ex, uint8_t *keep, uint8_t *avail)
{
    assert(ex && ex->type == et_stmt);

    switch(ex->stmt)
    {
        case STMT_BAS_ERROR:
        case STMT_BGET:
        case STMT_BLOAD:
        case STMT_BPUT:
        case STMT_BRUN:
        case STMT_BYE:
        case STMT_CIRCLE:
        case STMT_CLOAD:
        case STMT_CLOSE:
        case STMT_CLR:
        case STMT_CLS:
        case STMT_COLOR:
        case STMT_COM:
        case STMT_CSAVE:
        case STMT_DATA:
        case STMT_DEG:
        case STMT_DELETE:
        case STMT_DIM:
        case STMT_DIR:
        case STMT_DO:
        case STMT_DOS:
        case STMT_DPOKE:
        case STMT_DRAWTO:
        case STMT_DSOUND:
        case STMT_DUMP:
        case STMT_ELSE:
        case STMT_END:
        case STMT_ENDIF:
        case STMT_ENDIF_INVISIBLE:
        case STMT_ENDPROC:
        case STMT_ENTER:
        case STMT_EXEC:
        case STMT_EXEC_PAR:
        case STMT_EXIT:
        case STMT_F_B:
        case STMT_FCOLOR:
        case STMT_F_F:
        case STMT_FILLTO:
        case STMT_F_L:
        case STMT_FOR:
        case STMT_GET:
        case STMT_GO_S:
        case STMT_GRAPHICS:
        case STMT_IF:
        case STMT_IF_MULTILINE:
        case STMT_IF_THEN:
        case STMT_INPUT:
        case STMT_LBL_S:
        case STMT_LET:
        case STMT_LET_INV:
        case STMT_LOAD:
        case STMT_LOCATE:
        case STMT_LOCK:
        case STMT_LOOP:
        case STMT_LPRINT:
        case STMT_MOVE:
        case STMT_NEW:
        case STMT_NEXT:
        case STMT_N_MOVE:
        case STMT_NOTE:
        case STMT_OPEN:
        case STMT_PAINT:
        case STMT_PAUSE:
        case STMT_P_GET:
        case STMT_PLOT:
        case STMT_POINT:
        case STMT_POKE:
        case STMT_POP:
        case STMT_POSITION:
        case STMT_P_PUT:
        case STMT_PRINT_:
        case STMT_PRINT:
        case STMT_PROC:
        case STMT_PROC_VAR:
        case STMT_PUT:
        case STMT_RAD:
        case STMT_READ:
        case STMT_REM_:
        case STMT_REM:
        case STMT_RENAME:
        case STMT_REPEAT:
        case STMT_RETURN:
        case STMT_RUN:
        case STMT_SAVE:
        case STMT_SETCOLOR:
        case STMT_SOUND:
        case STMT_STATUS:
        case STMT_TEXT:
        case STMT_TIME_S:
        case STMT_TRACE:
        case STMT_UNLOCK:
        case STMT_UNTIL:
        case STMT_WEND:
        case STMT_WHILE:
        case STMT_XIO:
            // Nothing to do
            return 0;

        case STMT_STOP:
        case STMT_CONT:
            warn("%s alter program execution, can cause problem with line number removal.\n",
                 statements[ex->stmt].stm_long);
            return 0;

        case STMT_DEL:
        case STMT_RENUM:
        case STMT_LIST:
            warn("%s depends on line numbers, avoid using with line number removal.\n",
                 statements[ex->stmt].stm_long);
            return 0;

        case STMT_RESTORE:
            // Skip if no argument
            if( !ex->rgt)
                return 0;
        case STMT_TRAP:
            // Skip if argument is label
            if( ex->rgt && ex->rgt->type == et_tok && ex->rgt->tok == TOK_SHARP )
                return 0;
            // Fall through
        case STMT_GOTO:
        case STMT_GO_TO:
        case STMT_GOSUB:
            // Extract argument, should be a constant number
            return verify_target_line(ex->rgt, keep, avail, ex->stmt == STMT_TRAP,
                                      ex->stmt == STMT_RESTORE );
        case STMT_ON:
            assert(ex->rgt && ex->rgt->type == et_tok);
            if( ex->rgt->tok == TOK_ON_GOTO || ex->rgt->tok == TOK_ON_GOSUB )
            {
                int ret = 0;
                expr *l = ex->rgt->lft;
                expr *r = ex->rgt->rgt;
                if( expr_is_cnum(l) )
                {
                    // TODO
                    warn("'ON GOTO' with constant value %g, should optimize.\n", l->num);
                }
                while( r && r->type == et_tok && r->tok == TOK_COMMA )
                {
                    ret |= verify_target_line(r->rgt, keep, avail, 0, 0);
                    r = r->lft;
                }
                ret |= verify_target_line(r, keep, avail, 0, 0);
                return ret;
            }
            return 0;
        case STMT_IF_NUMBER:
            assert(ex->rgt && ex->rgt->type == et_tok && ex->rgt->tok == TOK_THEN);
            return verify_target_line(ex->rgt->rgt, keep, avail, 0, 0);
    }
    return 1;
}


void opt_remove_line_num(expr *prog)
{
    // Search all line numbers and mark available ones
    uint8_t *avail = calloc(32768/8,1);
    uint8_t *keep = calloc(32768/8,1);
    int err = 0;
    for(expr *ex = prog; ex != 0; ex = ex->lft )
        if( ex->type == et_lnum && ex->num != -1 )
        {
            if( ex->num < 0 || ex->num >= 32767.5 )
            {
                warn("invalid source line number %g.\n", ex->num);
                err |= 1;
            }
            else
                bitmap_set(avail, (int)(ex->num + 0.5));
        }

    // Search goto/gosub/trap/restore targets in the program
    for(expr *ex = prog; ex != 0; ex = ex->lft )
        if( ex->type == et_stmt )
            err |= do_search_stmt(ex, keep, avail);

    // Bail out on any error
    if( err )
    {
        free(avail);
        free(keep);
        return;
    }

    // Now, remove all line numbers *not* marked as keep:
    for(expr *ex = prog; ex != 0; ex = ex->lft )
        if( ex->type == et_lnum && ex->num != -1 )
        {
            int inum = (int)(ex->num+0.5);
            assert(inum>=0 && inum<32768);
            if( !bitmap_get(keep, inum) )
            {
                char buf[256];
                int len = sprintf(buf, ". old line %d", inum);
                expr *rem = expr_new_data(ex->mngr, (const uint8_t *)buf, len);
                ex->type = et_stmt;
                ex->num = 0;
                ex->stmt = STMT_REM;
                ex->rgt = rem;
                info_print(expr_get_file_name(ex), expr_get_file_line(ex),
                           "removing line number %d.\n", inum);

            }
        }

    free(avail);
    free(keep);
}
