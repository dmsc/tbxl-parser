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
#include "stmt.h"
#include "ataribcd.h"
#include "statements.h"
#include "tokens.h"
#include "vars.h"
#include "sbuf.h"
#include "dbg.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

static void memory_error(void)
{
    fprintf(stderr,"INTERNAL ERROR: memory allocation failure.\n");
    abort();
}

///////////////////////////////////////////////////////////////////////
// STATEMENTS

struct stmt_struct {
    int len;   // Statement length (tokens)
    uint8_t stmt;  // Statement
    uint8_t *data; // Data, one byte per statement and token
    unsigned size; // Available space for data.
    enum enum_tokens last_tok; // Last token in the statement
};

stmt *stmt_new(enum enum_statements sn)
{
    stmt *s = malloc(sizeof(stmt));
    s->len = 0;
    s->stmt = sn;
    s->data = malloc(128);
    s->size = 128;
    s->last_tok = TOK_LAST_TOKEN;
    if( !s->data )
        memory_error();
    return s;
}

void stmt_delete(stmt *s)
{
    free(s->data);
    free(s);
}

static void stmt_add_byte(stmt *s, uint8_t b)
{
    if( s->len == s->size )
    {
        s->size *= 2;
        if( !s->size )
            memory_error();
        s->data = realloc(s->data, s->size);
        if( !s->data )
            memory_error();
    }
    s->data[s->len] = b;
    s->len ++;
}

int stmt_is_text(const stmt *s)
{
    if( s->stmt == STMT_REM_ || s->stmt == STMT_REM ||
        s->stmt == STMT_BAS_ERROR || s->stmt == STMT_DATA )
        return 1;
    else
        return 0;
}

int stmt_can_skip(const stmt *s)
{
    if( s->stmt == STMT_REM_ || s->stmt == STMT_REM ||
        s->stmt == STMT_BAS_ERROR || s->stmt == STMT_ENDIF_INVISIBLE )
        return 1;
    else
        return 0;
}

void stmt_add_token(stmt *s, enum enum_tokens tok)
{
    // Don't add tokens to REMs or DATAs
    if( stmt_is_text(s) )
    {
        fprintf(stderr,"INTERNAL ERROR: token not allowed.\n");
        return;
    }

    stmt_add_byte(s, 0x10 + (int)tok);
    s->last_tok = tok;
}

static int stmt_add_colon(stmt *s)
{
    // Don't needed in REMs or DATAs
    if( stmt_is_text(s) )
        return 0;

    // If last token was a COLON or THEN, skip
    if( s->last_tok == TOK_COLON || s->last_tok == TOK_THEN )
        return 0;

    stmt_add_byte(s, 0x10 + TOK_COLON);
    s->last_tok = TOK_COLON;
    return 1;
}

void stmt_add_var(stmt *s, int id)
{
    if( id < 0 || id > 255 )
    {
        fprintf(stderr, "INTERNAL ERROR: more than 256 variables\n");
        abort();
    }
    if( id > 127 )
        stmt_add_byte(s, 0);
    stmt_add_byte(s, id ^ 0x80);
    s->last_tok = TOK_LAST_TOKEN;
}

static int is_hex_digit(char c)
{
    if( c >= '0' && c <= '9' ) return 1;
    if( c >= 'A' && c <= 'F' ) return 1;
    return 0;
}

void stmt_add_string(stmt *s, const char *data, unsigned len)
{
    // First pass, calculate real length of string
    unsigned i, rlen = 0;
    for(i=0; i<len; i++, rlen++)
    {
        if( i+1 < len && data[i] == '"' && data[i+1] == '"' )
            i++;
        else if( i+2 < len && data[i] == '\\' &&
                 is_hex_digit(data[i+1]) && is_hex_digit(data[i+2]) )
            i+=2;
    }
    if( rlen > 255 )
    {
        fprintf(stderr, "ERROR: truncating string constant, max length 256 bytes.\n");
        rlen = 255;
    }
    // Second pass, stores into statement
    stmt_add_byte(s, 0x0F);
    stmt_add_byte(s, rlen);
    for( ; rlen; rlen-- )
    {
        if( len>1 && data[0] == '"' && data[1] == '"' )
        {
            stmt_add_byte(s, '"');
            data += 2;
            len  -= 2;
        }
        else if( len>2 && data[0] == '\\' &&
                 is_hex_digit(data[1]) && is_hex_digit(data[2]) )
        {
            int c1 = data[1] > '9' ? data[1] - 'A' + 10 : data[1] - '0';
            int c2 = data[2] > '9' ? data[2] - 'A' + 10 : data[2] - '0';
            stmt_add_byte(s, c1 * 16 + c2);
            data += 3;
            len  -= 3;
        }
        else if( len>1 && data[0] == '\\' && data[1] == '\\' )
        {
            stmt_add_byte(s, '\\');
            data += 2;
            len  -= 2;
        }
        else
        {
            stmt_add_byte(s, *data);
            data += 1;
            len  -= 1;
        }
    }
    s->last_tok = TOK_LAST_TOKEN;
}

// Holds a table of names to atascii codes:
struct
{
    const char *name;
    unsigned char code;
} atascii_names[] = {
    { "heart", 0 },
    { "rbranch", 1 },
    { "rline", 2 },
    { "tlcorner", 3 },
    { "lbranch", 4 },
    { "blcorner", 5 },
    { "udiag", 6 },
    { "ddiag", 7 },
    { "rtriangle", 8 },
    { "brblock", 9 },
    { "ltriangle", 10 },
    { "trblock", 11 },
    { "tlblock", 12 },
    { "tline", 13 },
    { "bline", 14 },
    { "blblock", 15 },
    { "clubs", 16 },
    { "brcorner", 17 },
    { "hline", 18 },
    { "cross", 19 },
    { "ball", 20 },
    { "bbar", 21 },
    { "lline", 22 },
    { "bbranch", 23 },
    { "tbranch", 24 },
    { "lbar", 25 },
    { "trcorner", 26 },
    { "esc", 27 },
    { "up", 28 },
    { "down", 29 },
    { "left", 30 },
    { "right", 31 },
    { "diamond", 96 },
    { "spade", 123 },
    { "vline", 124 },
    { "clr", 125 },
    { "del", 126 },
    { "ins", 127 },
    { "tbar", 21+128 },
    { "rbar", 25+128 },
    { "eol", 155 },
    { "bell", 253 }
};
#define atascii_names_len (sizeof(atascii_names)/sizeof(atascii_names[0]))

int stmt_add_extended_string(stmt *s, const char *data, unsigned len, const char *fname, int file_line)
{
    // Interprets the string:
    unsigned i, rlen = 0;
    unsigned char buf[256];
    int state = 0, inverse = 0, count = 0, nameStart = 0, hex = 0;

    for(i=0; i<len && rlen < 256; i++)
    {
        char c = data[i];
        switch( state )
        {
            case 5: // CR
                if( c != '\n' )
                {
                    buf[rlen++] = '\r' ^ inverse;
                    if( rlen >= 255 )
                        break;
                }
                state = 0;
                // Fall through to normal character
            case 0: // Normal characters
                if( c == '{' )
                {
                    state = 1;
                    count = 0;
                }
                else if( c == '~' )
                    inverse ^= 0x80;
                else if( c == '\\' )
                    state = 3;
                else if( c == '\r' )
                    state = 5;  // Consume CR before LF
                else if( c == '\n' )
                    buf[rlen++] = 0x9B;
                else
                    buf[rlen++] = c ^ inverse;
                break;
            case 1: // Special character - count
                if( c >= '0' && c <= '9' )
                    count = count * 10 + (c - '0');
                else if( c == '*' )
                {
                    state = 2;
                    nameStart = i + 1;
                }
                else
                {
                    state = 2;
                    nameStart = i;
                }
                break;
            case 2:
                if( c == '}' )
                {
                    // End of special character name
                    unsigned len = i - nameStart;
                    unsigned j;
                    if( count == 0 )
                        count = 1;
                    for(j=0; j<atascii_names_len; j++)
                        if( strlen(atascii_names[j].name) == len &&
                            memcmp(atascii_names[j].name, data+nameStart, len) == 0 )
                            break;
                    if( j >= atascii_names_len )
                    {
                        err_print(fname, file_line, "invalid character name inside extended string '%.*s'\n",
                                  len, data + nameStart);
                        return 1;
                    }
                    else if( count > 0xFF || count + rlen > 0xFF )
                    {
                        err_print(fname, file_line, "too many characters inside extended string '%.*s'\n",
                                  len, data);
                        return 1;
                    }
                    else
                    {
                        while( count-- )
                            buf[rlen++] = atascii_names[j].code ^ inverse;
                    }
                    count = 0;
                    state = 0;
                }
                break;
            case 3: // Escaped character
                if( (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') )
                {
                    hex = c > '9' ? c - 'A' + 10 : c - '0';
                    state = 4;
                }
                else
                {
                    buf[rlen++] = c ^ inverse;
                    state = 0;
                }
                break;
            case 4: // Escaped character hex 2
                if( (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') )
                    buf[rlen++] = hex * 16 + (c > '9' ? c - 'A' + 10 : c - '0');
                else
                {
                    err_print(fname, file_line, "invalid escape ('\\%c') inside extended string\n", c);
                    return 1;
                }
                state = 0;
                break;
        }
    }
    if( rlen > 255 )
    {
        err_print(fname, file_line, "too many characters inside extended string, truncating '%.*s'\n", len, data);
        rlen = 255;
    }
    // Stores into statement
    stmt_add_byte(s, 0x0F);
    stmt_add_byte(s, rlen);
    for(i=0; i<rlen; i++)
        stmt_add_byte(s, buf[i]);
    s->last_tok = TOK_LAST_TOKEN;
    return 0;
}

void stmt_add_number(stmt *s, double d)
{
    atari_bcd n = atari_bcd_from_double(d);
    stmt_add_byte(s, 0x0E);
    stmt_add_byte(s, n.exp);
    stmt_add_byte(s, n.dig[0]);
    stmt_add_byte(s, n.dig[1]);
    stmt_add_byte(s, n.dig[2]);
    stmt_add_byte(s, n.dig[3]);
    stmt_add_byte(s, n.dig[4]);
    s->last_tok = TOK_LAST_TOKEN;
}

void stmt_add_hex_number(stmt *s, double d)
{
    atari_bcd n = atari_bcd_from_double(d);
    stmt_add_byte(s, 0x0D);
    stmt_add_byte(s, n.exp);
    stmt_add_byte(s, n.dig[0]);
    stmt_add_byte(s, n.dig[1]);
    stmt_add_byte(s, n.dig[2]);
    stmt_add_byte(s, n.dig[3]);
    stmt_add_byte(s, n.dig[4]);
    s->last_tok = TOK_LAST_TOKEN;
}

void stmt_add_data(stmt *s, const char *txt, unsigned len)
{
    int i;
    if( len > 254 )
        len = 254;
    for(i=0; i<len; i++)
        stmt_add_byte(s, txt[i]);
    s->last_tok = TOK_LAST_TOKEN;
}

void stmt_add_comment(stmt *s, const char *txt, unsigned len)
{
    stmt_add_data(s,txt,len);
}

static void print_string_long(const char *str, int len, string_buf *s)
{
    const char *p;
    sb_put(s, '"');
    for(p=str; len>0; p++, len--)
    {
        if( *p < 32 || *p > 126 )
        {
            sb_put(s, '\\');
            sb_put_hex(s, (uint8_t)(*p), 2);
        }
        else if( *p == '"' )
        {
            sb_put(s, '"');
            sb_put(s, '"');
        }
        else
            sb_put(s, *p);
    }
    sb_put(s, '"');
}

static void print_string_short(const char *str, int len, string_buf *s)
{
    const char *p;
    sb_put(s, '"');
    for(p=str; len>0; p++, len--)
    {
        if( *p == '"' )
            sb_put(s, '"');
        sb_put(s, *p);
    }
    sb_put(s, '"');
}

static void print_var_long(string_buf *s, vars *v, int id)
{
    sb_puts(s, vars_get_long_name(v, id));
    enum var_type type = vars_get_type(v, id);
    if( type == vtString )
        sb_put(s, '$');
    else if( type == vtArray )
        sb_puts(s, "( ");
}

static int print_var_short(string_buf *s, vars *v, int id)
{
    sb_puts(s, vars_get_short_name(v, id));
    enum var_type type = vars_get_type(v, id);
    switch(type)
    {
        case vtMaxType:
        case vtNone:
        case vtFloat:
        case vtLabel:
            return 1;
        case vtString:
            sb_put(s, '$');
            return 0;
        case vtArray:
            sb_put(s, '(');
            return 0;
    }
    return 0;
}

static int check_add_indent(enum enum_statements s)
{
    switch(s)
    {
        case STMT_DO:
        case STMT_ELSE:
        case STMT_FOR:
        case STMT_IF_MULTILINE:
        case STMT_IF_THEN:
        case STMT_PROC:
        case STMT_REPEAT:
        case STMT_WHILE:
            return 1;
        default:
            return 0;
    }
}

static int check_del_indent(enum enum_statements s)
{
    switch(s)
    {
        case STMT_ELSE:
        case STMT_ENDIF:
        case STMT_ENDPROC:
        case STMT_LOOP:
        case STMT_NEXT:
        case STMT_UNTIL:
        case STMT_WEND:
        case STMT_ENDIF_INVISIBLE:
            return 1;
        default:
            return 0;
    }
}

static void print_indent(string_buf *s, int i)
{
    for(; i>0; --i)
        sb_put(s, '\t');
}

// Prints a comment to the string_buf
static void print_comment(string_buf *b, const uint8_t *txt, unsigned len)
{
    // See if the text already has a "REM" or "'":
    if( ( !len  || (txt[0] != '\'' && txt[0] != '.') ) &&
        ( len<2 || strncasecmp("r.", (const char *)txt, 2) ) &&
        ( len<3 || strncasecmp("rem", (const char *)txt, 3) ) )
        sb_puts(b, "rem ");
    sb_write(b, txt, len);
}

// Print comment converting to ASCII
static void print_comment_ascii(string_buf *b, const uint8_t *txt, unsigned len)
{
    // First, convert to ASCII
    string_buf *tmp = sb_new();
    for( ; len > 0; txt++, len--)
    {
        // Conversion table for values 0x00 to 0x1F
        static const char convAscii[32] = {
            '*', '|', '[', '\'', '|', ',', '/', '\\',
            '/', '.', '\\', '\'', '\'', '^', '_', '.',
            '$', ',', '-', '+', 'o', '_', ']', '-',
            '-', '|', '\'', 'e', '^', 'v', '<', '>'
        };
        // Remove "inverse video" and test each non-printable character
        int c = 0x7F & (*txt);
        if( c < 0x20 )
            c = convAscii[c];
        else if( c == 0x60 )
            c = '*'; // Diamonds
        else if( c == 0x7b )
            c = '*'; // Spades
        else if( c == 0x7d )
            c = '^'; // Up-Left arrow
        else if( c == 0x7e )
            c = '<'; // Delete
        else if( c == 0x7f )
            c = '>'; // Insert
        sb_put(tmp, c);
    }
    // Print
    print_comment(b, (uint8_t *)tmp->data, tmp->len);
}

string_buf *stmt_print_long(stmt *s, vars *varl, int *indent, int conv_ascii)
{
    string_buf *b = sb_new();

    if( check_del_indent(s->stmt) && *indent)
        (*indent)--;

    int pind = *indent;

    *indent += check_add_indent(s->stmt);

    print_indent(b, pind);
    if( s->stmt == STMT_REM_ )
    {
        int i;
        for(i=0; i<30; i++)
            sb_put(b, '-');
    }
    else if( s->stmt == STMT_REM )
    {
        if( conv_ascii )
            print_comment_ascii(b, s->data, s->len);
        else
            print_comment(b, s->data, s->len);
    }
    else if( s->stmt == STMT_DATA )
    {
        sb_puts(b, "data ");
        sb_write(b, s->data, s->len);
    }
    else if( s->stmt == STMT_BAS_ERROR )
    {
        sb_puts(b, "ERROR - ");
        if( s->len )
        {
            sb_write(b, s->data, s->len-1);
            // Skip last extra COLON
            char c = s->data[s->len-1];
            if( c != 0x10 + TOK_COLON )
                sb_put(b, c);
        }
    }
    else
    {
        const char *st = statements[s->stmt].stm_long;
        if( s->stmt == STMT_ENDIF_INVISIBLE )
            st = "ENDIF";
        sb_puts_lcase(b, st);
        if( *st )
            sb_put(b, ' ');
        int l = s->len;
        uint8_t *d = s->data;
        for( ; l>0 ; --l, ++d)
        {
            int tk = *d;
            if( tk == 0x0D || tk == 0x0E )
            {
                // number
                atari_bcd n;
                n.exp    = d[1];
                n.dig[0] = d[2];
                n.dig[1] = d[3];
                n.dig[2] = d[4];
                n.dig[3] = d[5];
                n.dig[4] = d[6];
                d += 6;
                l -= 6;
                if( tk == 0x0D )
                {
                    sb_put(b, '$');
                    atari_bcd_print_hex(n, b);
                }
                else
                    atari_bcd_print(n, b);
            }
            else if( tk == 0x0F )
            {
                int ln = d[1];
                print_string_long( (const char *)(d+2), ln, b );
                l -= ln+1;
                d += ln+1;
            }
            else if( tk >= 0x10 && tk < 0x10 + TOK_LAST_TOKEN )
            {
                enum enum_tokens te = tk - 0x10;
                // Skip THEN if converting an IF/THEN
                if( s->stmt != STMT_IF_THEN || te != TOK_THEN )
                    sb_puts(b, tokens[te].tok_long);
            }
            else if( !tk )
            {
                // Variable > 127
                d ++;
                l --;
                print_var_long(b, varl, *d + 128);
            }
            else if( tk > 127 )
            {
                // Variable < 128
                print_var_long(b, varl, *d - 128);
            }
            else
            {
                // Unknown token!!
                sb_puts(b, " ERROR_TOK_");
                sb_put_hex(b, tk, 2);
                sb_puts(b, " ");
            }
        }
    }
    return b;
}

string_buf *stmt_print_alone(stmt *s, vars *varl)
{
    int indent = 0;
    return stmt_print_long(s, varl, &indent, 1);
}

string_buf *stmt_print_short(stmt *s, vars *varl, int *skip_colon, int *no_split)
{
    string_buf *b = sb_new();

    if( s->stmt == STMT_ENDIF_INVISIBLE )
    {
        (*no_split) --;
        return b;
    }
    else if( s->stmt == STMT_REM_ || s->stmt == STMT_REM || s->stmt == STMT_BAS_ERROR )
        return b;

    *skip_colon = 0;
    if( s->stmt == STMT_DATA )
    {
        sb_puts(b, "D.");
        sb_write(b, s->data, s->len);
    }
    else
    {
        if( s->stmt == STMT_PRINT )
            sb_put(b, '?');
        else
            sb_puts(b, statements[s->stmt].stm_short);
        // Check if it is an IF/THEN, can't be split:
        if( s->stmt == STMT_IF_THEN )
        {
            *skip_colon = 1;
            (*no_split) ++; // Can't split the "THEN" part
        }
        int add_space = 0;
        int l = s->len;
        uint8_t *d = s->data;
        for( ; l>0 ; --l, ++d)
        {
            int tk = *d;
            if( tk == 0x0D || tk == 0x0E )
            {
                // number
                atari_bcd n;
                n.exp    = d[1];
                n.dig[0] = d[2];
                n.dig[1] = d[3];
                n.dig[2] = d[4];
                n.dig[3] = d[5];
                n.dig[4] = d[6];
                d += 6;
                l -= 6;
                atari_bcd_print(n, b);
                add_space = 0;
            }
            else if( tk == 0x0F )
            {
                int ln = d[1];
                print_string_short( (const char *)(d+2), ln, b );
                l -= ln+1;
                d += ln+1;
                add_space = 0;
            }
            else if( tk >= 0x10 && tk < 0x10 + TOK_LAST_TOKEN )
            {
                enum enum_tokens te = tk - 0x10;
                const char *t = tokens[te].tok_short;
                if( add_space && ( (t[0] >= 'A' && t[0] <= 'Z') || t[0] == '_' ) )
                    sb_put(b, ' ');
                sb_puts(b, t);
                add_space = 0;
            }
            else if( !tk )
            {
                // Variable > 127
                l --;
                d ++;
                add_space = print_var_short(b, varl, *d + 128);
            }
            else if( tk > 127 )
            {
                // Variable < 128
                add_space = print_var_short(b, varl, *d - 128);
            }
            else
            {
                // Unknown token!!
                sb_puts(b, " ERROR_TOK_");
                sb_put_hex(b, tk, 2);
                sb_puts(b, " ");
            }
        }
    }
    return b;
}

string_buf *stmt_get_bas(stmt *s, vars *varl, int *end_colon, int *no_split)
{
    string_buf *b = sb_new();

    if( s->stmt == STMT_REM_ || s->stmt == STMT_REM || s->stmt == STMT_BAS_ERROR )
    {
        return b;
    }
    else if( s->stmt == STMT_DATA )
    {
        sb_put(b, s->stmt);
        sb_write(b, s->data, s->len);
        sb_put(b, 155);
        *end_colon = 0;
        return b;
    }
    else if( s->stmt == STMT_ENDIF_INVISIBLE )
    {
        (*no_split) --;
        return b;
    }
    else if( s->stmt == STMT_IF_THEN )
    {
        (*no_split) ++; // Can't split the "THEN" part
    }

    *end_colon = stmt_add_colon(s);
    if( s->stmt == STMT_IF_THEN || s->stmt == STMT_IF_MULTILINE || s->stmt == STMT_IF_NUMBER )
        sb_put(b, STMT_IF);
    else
        sb_put(b, s->stmt);

    sb_write(b, s->data, s->len);
    return b;
}

int stmt_get_bas_len(stmt *s)
{
    if( s->stmt == STMT_REM_ || s->stmt == STMT_REM || s->stmt == STMT_BAS_ERROR )
        return 0;
    else if( s->stmt == STMT_DATA )
        return 2 + s->len; // DATA + text + EOL
    else if( s->stmt == STMT_ENDIF_INVISIBLE )
        return 0;
    else
    {
        stmt_add_colon(s);
        return 1 + s->len; // STATEMENT + TOKENS
    }
}

int stmt_is_valid(const stmt *s)
{
    if( !s || s->stmt >= STMT_ENDIF_INVISIBLE )
        return 0;
    return 1;
}

int stmt_is_label(const stmt *s)
{
    return ( s->stmt == STMT_LBL_S || s->stmt == STMT_PROC );
}
