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
#include "ataribcd.h"
#include <string.h>

static uint8_t tobcd(int n)
{
    return (n/10)*16 + (n%10);
}

// Converts a double to an Atari BCD representation
atari_bcd atari_bcd_from_double(double x)
{
    static const double expTab[99] = {
        1e-98, 1e-96, 1e-94, 1e-92, 1e-90, 1e-88, 1e-86, 1e-84, 1e-82, 1e-80,
        1e-78, 1e-76, 1e-74, 1e-72, 1e-70, 1e-68, 1e-66, 1e-64, 1e-62, 1e-60,
        1e-58, 1e-56, 1e-54, 1e-52, 1e-50, 1e-48, 1e-46, 1e-44, 1e-42, 1e-40,
        1e-38, 1e-36, 1e-34, 1e-32, 1e-30, 1e-28, 1e-26, 1e-24, 1e-22, 1e-20,
        1e-18, 1e-16, 1e-14, 1e-12, 1e-10, 1e-08, 1e-06, 1e-04, 1e-02, 1e+00,
        1e+02, 1e+04, 1e+06, 1e+08, 1e+10, 1e+12, 1e+14, 1e+16, 1e+18, 1e+20,
        1e+22, 1e+24, 1e+26, 1e+28, 1e+30, 1e+32, 1e+34, 1e+36, 1e+38, 1e+40,
        1e+42, 1e+44, 1e+46, 1e+48, 1e+50, 1e+52, 1e+54, 1e+56, 1e+58, 1e+60,
        1e+62, 1e+64, 1e+66, 1e+68, 1e+70, 1e+72, 1e+74, 1e+76, 1e+78, 1e+80,
        1e+82, 1e+84, 1e+86, 1e+88, 1e+90, 1e+92, 1e+94, 1e+96, 1e+98
    };

    atari_bcd ret;
    int i;

    ret.exp = 0;
    memset(ret.dig,0,5);

    if( !x )
        return ret;

    if( x<0 )
    {
        ret.exp = 0x80;
        x = -x;
    }

    if( x < 1e-99 )
        return ret; // Underflow
    if( x >= 1e+98 )
    {
        ret.exp |= 0x71;
        memset(ret.dig,0x99,5);
        return ret;
    }

    ret.exp |= 0x0E;
    for(i=0; i<99; i++, ret.exp++)
    {
        if( x < expTab[i] )
        {
            uint64_t n = (uint64_t)(0.5 + x * 10000000000.0 / expTab[i]);
            ret.dig[4] = tobcd(n % 100); n /= 100;
            ret.dig[3] = tobcd(n % 100); n /= 100;
            ret.dig[2] = tobcd(n % 100); n /= 100;
            ret.dig[1] = tobcd(n % 100); n /= 100;
            ret.dig[0] = tobcd(n);
            break;
        }
    }
    return ret;
}

// Converts a double to an Atari BCD representation
double atari_bcd_to_double(atari_bcd n)
{
    static const double expTab[128] = {
        1e-136,1e-134,1e-132,1e-130,1e-128,1e-126,1e-124,1e-122,1e-120,
        1e-118,1e-116,1e-114,1e-112,1e-110,1e-108,1e-106,1e-104,1e-102,1e-100,
        1e-98, 1e-96, 1e-94, 1e-92, 1e-90, 1e-88, 1e-86, 1e-84, 1e-82, 1e-80,
        1e-78, 1e-76, 1e-74, 1e-72, 1e-70, 1e-68, 1e-66, 1e-64, 1e-62, 1e-60,
        1e-58, 1e-56, 1e-54, 1e-52, 1e-50, 1e-48, 1e-46, 1e-44, 1e-42, 1e-40,
        1e-38, 1e-36, 1e-34, 1e-32, 1e-30, 1e-28, 1e-26, 1e-24, 1e-22, 1e-20,
        1e-18, 1e-16, 1e-14, 1e-12, 1e-10, 1e-08, 1e-06, 1e-04, 1e-02, 1e+00,
        1e+02, 1e+04, 1e+06, 1e+08, 1e+10, 1e+12, 1e+14, 1e+16, 1e+18, 1e+20,
        1e+22, 1e+24, 1e+26, 1e+28, 1e+30, 1e+32, 1e+34, 1e+36, 1e+38, 1e+40,
        1e+42, 1e+44, 1e+46, 1e+48, 1e+50, 1e+52, 1e+54, 1e+56, 1e+58, 1e+60,
        1e+62, 1e+64, 1e+66, 1e+68, 1e+70, 1e+72, 1e+74, 1e+76, 1e+78, 1e+80,
        1e+82, 1e+84, 1e+86, 1e+88, 1e+90, 1e+92, 1e+94, 1e+96, 1e+98, 1e+100,
        1e+102,1e+104,1e+106,1e+108,1e+110,1e+112,1e+114,1e+116,1e+118
    };

    if( n.exp == 0 )
        return 0.0;
    else if( n.exp == 0x80 )
        return -0.0;

    double x;
    x = (n.dig[0]>>4) * 10 + (n.dig[0]&0x0F);
    x = x * 100 + (n.dig[1]>>4) * 10 + (n.dig[1]&0x0F);
    x = x * 100 + (n.dig[2]>>4) * 10 + (n.dig[2]&0x0F);
    x = x * 100 + (n.dig[3]>>4) * 10 + (n.dig[3]&0x0F);
    x = x * 100 + (n.dig[4]>>4) * 10 + (n.dig[4]&0x0F);

    x = x * expTab[ n.exp & 0x7F ];
    if( n.exp & 0x80 )
        return -x;
    else
        return x;
}

// Prints a double in a format suitable for BASIC input
void atari_bcd_print(atari_bcd n, string_buf *sb)
{
    // Check for '0'
    if( (n.exp & 0x7F) == 0 )
    {
        sb_put(sb,'0');
        return;
    }

    // Transform mantisa to decimal
    char buf[12];
    char *dig = buf;
    int i;
    for(i=0; i<5; i++)
    {
        dig[2*i]   = '0' + (n.dig[i] >> 4);
        dig[2*i+1] = '0' + (n.dig[i] & 0x0F);
    }
    dig[10] = 0;

    // Extract exp and sign
    int exp = (n.exp & 0x7F) * 2 - 136;
    int sgn = n.exp & 0x80;

    // Remove zeroes at end
    for(i=9; i>0; i--)
    {
        if( dig[i] != '0' )
            break;
        dig[i] = 0;
        exp ++;
    }
    if( sgn )
        sb_put(sb, '-');

    if( *dig == '0' )
    {
        dig++;
        i--;
    }

    if( exp < 0 && exp >= -i-1 )
    {
        while( exp > -i-1 )
        {
            sb_put(sb, *dig);
            dig++;
            exp--;
        }
        sb_put(sb, '.');
        sb_puts(sb, dig);
    }
    else if( exp+2 == -i )
    {
        sb_put(sb, '.');
        sb_put(sb, '0');
        sb_puts(sb, dig);
    }
    else if( exp == 0 )
        sb_puts(sb, dig);
    else if( exp == 1 )
    {
        sb_puts(sb, dig);
        sb_put(sb, '0');
    }
    else if( exp == 2 )
    {
        sb_puts(sb, dig);
        sb_put(sb, '0');
        sb_put(sb, '0');
    }
    else
    {
        sb_puts(sb, dig);
        sb_put(sb, 'E');
        if( exp < 0 )
        {
            sb_put(sb, '-');
            exp = -exp;
        }
        if( exp > 9 )
            sb_put(sb, '0' + (exp/10));
        sb_put(sb, '0' + (exp % 10));
    }
}

