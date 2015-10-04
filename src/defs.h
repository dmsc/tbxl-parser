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

typedef struct defs_struct defs;

defs * defs_new(void);
void defs_delete(defs *);

// Returns ID of definition named "name", or -1 if not found.
int defs_search(defs *, const char *name);
// Creates a new definition named "name", returns ID.
int defs_new_def(defs *, const char *name, const char *file_name, int file_line);

// Sets string data to a definition
void defs_set_string(defs *, int id, const char *data, int len);

// Sets numeric value to a definition
void defs_set_numeric(defs *, int id, const double val);

// Gets string data from a definition
int defs_get_string(defs *, int id, const char **data, int *len);

// Gets value from a definition
int defs_get_numeric(defs *, int id, double *val);

