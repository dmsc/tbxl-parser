/*
 *  hash - fast hash of data
 *  Copyright (C) 2019 Daniel Serpell
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
 *  Copyright (C) 2019 Rusty Russell <rusty@rustcorp.com.au>
 *
 */
/* Based on code by Bob Jenkins, May 2006, Public Domain. */
#pragma once
#include <stdint.h>
#include <stdlib.h>

/**
 * hashl - fast 32-bit hash of an array for internal use
 * @p: the array or pointer to first element
 * @num: the number of elements to hash
 */
#define hashl(p, num) hash_any((p), (num)*sizeof(*(p)))
uint32_t hash_any(const void *key, size_t length);

