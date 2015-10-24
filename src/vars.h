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

typedef struct vars_struct vars;

// Types of variables
enum var_type {
    vtNone,
    vtFloat,
    vtString,
    vtArray,
    vtLabel,
    vtMaxType
};

vars * vars_new(void);
void vars_delete(vars *v);

// Returns ID of variable named "name" of type "type", or -1 if not found.
int vars_search(vars *v, const char *name, enum var_type type);
// Creates a new variable named "name" of type "type", or if already exists,
// returns the ID of the existing variable.
int vars_new_var(vars *v, const char *name, enum var_type type, const char *file_name, int file_line);

// Gets the number of variables of type
int vars_get_count(vars *v, enum var_type type);

// Gets the total number of variables
int vars_get_total(const vars *v);

// Gets the "long name" of the variable (the name passed on constructor)
const char *vars_get_long_name(vars *v, int id);
// Gets a short unique name for the variable.
const char *vars_get_short_name(vars *v, int  id);
// Gets the type of the variable.
enum var_type vars_get_type(vars *v, int id);

// Returns a printable name for the var_type.
const char *var_type_name(enum var_type);

// Shows a summary of renamed variables
void vars_show_summary(vars *v, enum var_type t, int bin);
