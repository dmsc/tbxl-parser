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
#include "hash.h"

/* Based on code by Bob Jenkins, May 2006, Public Domain. */
#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))
#define mix(a,b,c) \
{ \
    a -= c;  a ^= rot(c, 4);  c += b; b -= a;  b ^= rot(a, 6);  a += c; \
    c -= b;  c ^= rot(b, 8);  b += a; a -= c;  a ^= rot(c,16);  c += b; \
    b -= a;  b ^= rot(a,19);  a += c; c -= b;  c ^= rot(b, 4);  b += a; \
}

#define final(a,b,c) \
{ \
    c ^= b; c -= rot(b,14); a ^= c; a -= rot(c,11); b ^= a; b -= rot(a,25); \
    c ^= b; c -= rot(b,16); a ^= c; a -= rot(c,4);  b ^= a; b -= rot(a,14); \
    c ^= b; c -= rot(b,24); \
}

/* Hashes 32-bit aligned data:
 * k:       the key, an array of uint32_t values,
 * length:  the length of the key, in uint32_t,
 * initval: the previous hash, or an arbitrary value. */
static uint32_t hash_u32( const uint32_t *k, size_t length)
{
    uint32_t a,b,c;

    /* Set up the internal state */
    a = b = c = 0xdeadbeef + (((uint32_t)length)<<2);

    while (length > 3)
    {
        a += k[0];
        b += k[1];
        c += k[2];
        mix(a,b,c);
        length -= 3;
        k += 3;
    }

    switch(length) /* all the case statements fall through */
    {
        case 3 : c+=k[2];
        case 2 : b+=k[1];
        case 1 : a+=k[0];
                 final(a,b,c);
        case 0: /* case 0: nothing left to add */
        default:
                 return c;
    }
}

static uint32_t read32(const uint8_t *k)
{
    return k[0] | (k[1] << 8) | (k[2] << 16) | (k[3] << 24);
}

static uint32_t hash_u8( const void *key, size_t length )
{
    uint32_t a,b,c;
    const uint8_t *k = (const uint8_t *)key;

    /* Set up the internal state */
    a = b = c = 0xdeadbeef + ((uint32_t)length);

    while (length > 12)
    {
        a += read32(k);
        b += read32(k+4);
        c += read32(k+8);
        mix(a,b,c);
        length -= 12;
        k += 12;
    }

    switch(length)                   /* all the case statements fall through */
    {
        case 12: c+=((uint32_t)k[11])<<24;
        case 11: c+=((uint32_t)k[10])<<16;
        case 10: c+=((uint32_t)k[9])<<8;
        case 9 : c+=k[8];
        case 8 : b+=((uint32_t)k[7])<<24;
        case 7 : b+=((uint32_t)k[6])<<16;
        case 6 : b+=((uint32_t)k[5])<<8;
        case 5 : b+=k[4];
        case 4 : a+=((uint32_t)k[3])<<24;
        case 3 : a+=((uint32_t)k[2])<<16;
        case 2 : a+=((uint32_t)k[1])<<8;
        case 1 : a+=k[0];
                 final(a,b,c);
        case 0 :
        default:
                 return c;
    }
}

uint32_t hash_any(const void *key, size_t length)
{
    union { const void *ptr; size_t i; } u;
    u.ptr = key;
    if ( ((length & 3) == 0) && ((u.i & 0x3) == 0) )
        return hash_u32((const uint32_t *)key, length >> 2);
    else
        return hash_u8(key, length);
}
