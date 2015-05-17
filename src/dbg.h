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

#define err_print(...)  fprintf(stderr,"error: " __VA_ARGS__ )
#define warn_print(...) if( do_debug > 0 ) fprintf(stderr, "warning: " __VA_ARGS__ )
#define info_print(...) if( do_debug > 1 ) fprintf(stderr, "info: " __VA_ARGS__ )

