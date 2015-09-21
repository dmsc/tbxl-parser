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

#include <stdio.h>
extern int do_debug;

#define dprintf(n, ...) if( do_debug > n ) fprintf(stderr, __VA_ARGS__ )

#define debug_print(err, file, line, msg, ...) fprintf(stderr, err ": %s(%d): " msg, file, line, ## __VA_ARGS__ )
#define err_print(file, line, ...)  debug_print("error", file, line, __VA_ARGS__ )
#define warn_print(file, line, ...) if( do_debug > 0 ) debug_print("warning", file, line, __VA_ARGS__ )
#define info_print(file, line, ...) if( do_debug > 1 ) debug_print("info", file, line, __VA_ARGS__ )

