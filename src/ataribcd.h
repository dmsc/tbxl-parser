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

#include <stdint.h>
#include "sbuf.h"

typedef struct {
    uint8_t exp;
    uint8_t dig[5];
} atari_bcd;

// Converts a double to an Atari BCD representation
atari_bcd atari_bcd_from_double(double x);

// Prints a double in a format suitable for BASIC input
void atari_bcd_print(atari_bcd n, string_buf *sb);

// Prints a double in hexadecimal format. The value must be from 0 to 65535.
void atari_bcd_print_hex(atari_bcd n, string_buf *sb);

