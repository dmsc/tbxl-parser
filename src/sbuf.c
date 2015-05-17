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
#include <stdlib.h>
#include <stdio.h>

static void memory_error()
{
    fprintf(stderr,"INTERNAL ERROR: memory allocation failure.\n");
    abort();
}

string_buf *sb_new(void)
{
    string_buf *s = malloc(sizeof(string_buf));
    if( !s )
        memory_error();
    sb_init(s);
    return s;
}

void sb_init(string_buf *s)
{
    s->len = 0;
    s->size = 256;
    s->data = malloc(256);
    if( !s->data )
        memory_error();
}

void sb_delete(string_buf *s)
{
    free(s->data);
    free(s);
}

static void sb_check_size(string_buf *s)
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
}

void sb_put(string_buf *s, char c)
{
    sb_check_size(s);
    s->data[s->len] = c;
    s->len++;
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

