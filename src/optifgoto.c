/*
 *  Basic Parser - TurboBasic XL compatible parsing and transformation tool.
 *  Copyright (C) 2024 Daniel Serpell
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

#include "optifgoto.h"
#include "expr.h"
#include "dbg.h"
#include "parser.h"
#include <assert.h>
#include <stdlib.h>

// Simplify printing warnings
#define warn(...) \
    do { warn_print(expr_get_file_name(ex), expr_get_file_line(ex), __VA_ARGS__); } while(0)

static int check_endif(expr *ex)
{
    assert(ex && ex->type == et_stmt);
    return ex->stmt == STMT_ENDIF_INVISIBLE || ex->stmt == STMT_ENDIF;
}

static int check_goto(expr *ex)
{
    assert(ex);
    return ex->type == et_stmt && ex->stmt == STMT_GOTO;
}

static int check_then(expr *ex)
{
    return ex && ex->type == et_tok && ex->tok == TOK_THEN;
}

static int expr_is_cnum(expr *ex)
{
    return ex && (ex->type == et_c_number || ex->type == et_c_hexnumber);
}

static int do_check_stmt(expr *ex, int multiline)
{
    expr *gto;
    assert(ex && ex->type == et_stmt);

    if(!multiline && ex->stmt == STMT_IF_MULTILINE)
        return 0;

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
        case STMT_CONT:
        case STMT_CSAVE:
        case STMT_DATA:
        case STMT_DEG:
        case STMT_DEL:
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
        case STMT_FCOLOR:
        case STMT_FILLTO:
        case STMT_FOR:
        case STMT_F_B:
        case STMT_F_F:
        case STMT_F_L:
        case STMT_GET:
        case STMT_GOSUB:
        case STMT_GOTO:
        case STMT_GO_S:
        case STMT_GO_TO:
        case STMT_GRAPHICS:
        case STMT_INPUT:
        case STMT_LBL_S:
        case STMT_LET:
        case STMT_LET_INV:
        case STMT_LIST:
        case STMT_LOAD:
        case STMT_LOCATE:
        case STMT_LOCK:
        case STMT_LOOP:
        case STMT_LPRINT:
        case STMT_MOVE:
        case STMT_NEW:
        case STMT_NEXT:
        case STMT_NOTE:
        case STMT_N_MOVE:
        case STMT_ON:
        case STMT_OPEN:
        case STMT_PAINT:
        case STMT_PAUSE:
        case STMT_PLOT:
        case STMT_POINT:
        case STMT_POKE:
        case STMT_POP:
        case STMT_POSITION:
        case STMT_PRINT:
        case STMT_PRINT_:
        case STMT_PROC:
        case STMT_PROC_VAR:
        case STMT_PUT:
        case STMT_P_GET:
        case STMT_P_PUT:
        case STMT_RAD:
        case STMT_READ:
        case STMT_REM:
        case STMT_REM_:
        case STMT_REM_HIDDEN:
        case STMT_RENAME:
        case STMT_RENUM:
        case STMT_REPEAT:
        case STMT_RESTORE:
        case STMT_RETURN:
        case STMT_RUN:
        case STMT_SAVE:
        case STMT_SETCOLOR:
        case STMT_SOUND:
        case STMT_STATUS:
        case STMT_STOP:
        case STMT_TEXT:
        case STMT_TIME_S:
        case STMT_TRACE:
        case STMT_TRAP:
        case STMT_UNLOCK:
        case STMT_UNTIL:
        case STMT_WEND:
        case STMT_WHILE:
        case STMT_XIO:
            return 0;
        case STMT_IF_NUMBER:    // already in the converted form
            return 0;
        case STMT_IF:           // should not exist in the internal representation
            assert(ex->stmt == STMT_IF);
            return 1;
        case STMT_IF_THEN:      // the THEN should be part of current statement
            assert(check_then(ex->rgt));
            assert(!ex->rgt->rgt);
            // fall through
        case STMT_IF_MULTILINE:
            gto = ex->lft;
            if(!check_goto(gto))
                return 0;       // Not a GOTO
            // If we are in list output, check that the line number is a constant
            if(get_output_type() != out_binary && !expr_is_cnum(gto->rgt))
                return 0;
            // Ok, we have our GOTO, check that there are no more statements
            // in the IF up to the ENDIF
            if(!check_endif(gto->lft))
            {
                warn("Statements in IF after GOTO, probably ignored.");
                return 1;
            }
            // Check ENDIF is ok
            assert(!gto->lft->rgt);
            // Ok, we can replace our expression
            ex->lft = gto->lft->lft;
            // Change to IF-NUMBER
            ex->stmt = STMT_IF_NUMBER;
            if(check_then(ex->rgt))
            {
                // Patch existing THEN
                ex->rgt->rgt = gto->rgt;
            }
            else
            {
                // Build a new THEN expression
                ex->rgt = expr_new_bin(ex->mngr, ex->rgt, gto->rgt, TOK_THEN);
            }
            return 0;
    }
    return 1;
}

int opt_convert_then_goto(expr *prog, int multiline)
{
    // Search for IF/THEN statements
    int err = 0;
    for(expr *ex = prog; ex != 0; ex = ex->lft )
        if( ex->type == et_stmt )
            err |= do_check_stmt(ex, multiline);

    return err;
}
