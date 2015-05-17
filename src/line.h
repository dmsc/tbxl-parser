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

// Forwards
enum enum_statements;
enum enum_tokens;
typedef struct stmt_struct stmt;

// Represents a line of code
// A program line can be a statement or a line number
typedef struct line_struct line;

// Create a new line
line *line_new_linenum(int num);
line *line_new_statement(enum enum_statements s);

// Check line type
int line_is_num(const line *l);
int line_is_statement(const line *l);

// Get content
int   line_get_num(const line *l);
stmt *line_get_statement(const line *l);

// Delete line
void line_delete(line *l);

