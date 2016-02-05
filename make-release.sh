#!/bin/sh
#
# Script to build release tarballs
# --------------------------------
#
# Compiles for three different platforms:
# - For Win32, using the mingw-w64 compiler,
# - For Linux 64bit PC, using GCC,
# - For Linux 32bit PC, using GCC.
#
# Uses link-time-optimizations and disable asserts.


out="basicParser-$(git describe --long --dirty)"

rm -rf build/
make CROSS=i686-w64-mingw32- EXT=.exe CFLAGS='-Wall -std=c99 -O2 -flto -DNDEBUG' dist
mv build/${out}.zip ../${out}-win32.zip

rm -rf build/
make CROSS=x86_64-apple-darwin15- EXT= CFLAGS='-Wall -O2 -flto -DNDEBUG' dist
mv build/${out}.zip ../${out}-maxosx.zip

rm -rf build/
make CROSS= EXT= CFLAGS='-Wall -O2 -flto -DNDEBUG' dist
mv build/${out}.zip ../${out}-linux64.zip

rm -rf build/
make CROSS= EXT= CFLAGS='-Wall -m32 -O2 -flto -DNDEBUG' dist
mv build/${out}.zip ../${out}-linux32.zip

