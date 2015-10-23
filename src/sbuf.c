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
#include "sbuf.h"
#include "darray.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

darray_struct(char, string_buf);

string_buf *sb_new(void)
{
    return darray_new(char, 256);
}

void sb_delete(string_buf *s)
{
    darray_free(s);
}

void sb_put(string_buf *s, char c)
{
    darray_add(s, c);
}

void sb_write(string_buf *s, const unsigned char *buf, unsigned cnt)
{
    while(cnt)
    {
        sb_put(s, *buf);
        cnt--;
        buf++;
    }
}

void sb_cat(string_buf *s, const string_buf *src)
{
    sb_write(s, (const unsigned char *)src->data, src->len);
}

void sb_puts(string_buf *s, const char *c)
{
    while(*c)
    {
        sb_put(s, *c);
        c++;
    }
}

void sb_puts_lcase(string_buf *s, const char *c)
{
    while(*c)
    {
        if( *c >= 'A' && *c <= 'Z' )
            sb_put(s, *c - 'A' + 'a');
        else
            sb_put(s, *c);
        c++;
    }
}

void sb_put_dec(string_buf *s, int n)
{
    if( n < 0 )
    {
        sb_put(s, '-');
        n = -n;
    }
    if( !n )
        sb_put(s, '0');
    else
    {
        int m = 1000000000;
        while( m > n )
            m /= 10;
        while( m )
        {
            int d = n/m;
            n = n - d * m;
            sb_put(s, '0' + d);
            m /= 10;
        }
    }
}

void sb_put_hex(string_buf *s, int n, int dig)
{
    static const char hx[] = "0123456789ABCDEF";
    for( ; dig>0; dig-- )
        sb_put(s, hx[ (n >> (4*dig-4)) & 0x0F ]);
}

unsigned sb_len(const string_buf *s)
{
    return s->len;
}

const char *sb_data(const string_buf *s)
{
    return s->data;
}

void sb_set_char(string_buf *s, int pos, char c)
{
    if( pos < 0 )
        s->data[s->len + pos] = c;
    else
        s->data[pos] = c;
}

void sb_clear(string_buf *s)
{
    s->len = 0;
}

void sb_erase(string_buf *s, unsigned start, unsigned end)
{
    memmove(s->data+start, s->data+end, s->len - end);
    s->len -= end - start;
}

void sb_insert(string_buf *s, int pos, const string_buf *src)
{
    if( pos < 0 )
        pos = s->len - pos;
    // Grow
    darray_grow(s,sizeof(s->data[0]), s->len + src->len);
    // Move
    memmove(s->data + pos + src->len, s->data + pos, s->len - pos);
    // Copy
    memcpy(s->data + pos, src->data, src->len);
    s->len += src->len;
}

unsigned sb_fwrite(string_buf *s, FILE *f)
{
    return fwrite( sb_data(s), sb_len(s), 1, f);
}
