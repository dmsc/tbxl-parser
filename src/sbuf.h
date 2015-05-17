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
#pragma once

// A string buffer, used for printing
typedef struct string_buf {
    int len;
    int size;
    char *data;
} string_buf;

// Creates a new string buffer
string_buf * sb_new(void);
// Inits an allocated string buffer
void sb_init(string_buf *s);
// Deletes an string buffer
void sb_delete(string_buf *s);

// Appends one char
void sb_put(string_buf *s, char c);
// Appends many chars
void sb_write(string_buf *s, const unsigned char *buf, unsigned cnt);
// Appends one string_buf in another
void sb_cat(string_buf *s, const string_buf *src);
// Appends a nul terminated string
void sb_puts(string_buf *s, const char *c);
// Appends a nul terminated string, converting to lower case
void sb_puts_lcase(string_buf *s, const char *c);
// Appends a decimal number
void sb_put_dec(string_buf *s, int n);
// Appends a hexadecimal number of "dig" digits.
void sb_put_hex(string_buf *s, int n, int dig);

