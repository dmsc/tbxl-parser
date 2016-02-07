#!/bin/sh
#
# Script to build release tarballs
# --------------------------------
#
# Compiles for three different platforms:
# - For Win32, using the mingw-w64 compiler,
# - For OSX 32 and 64 bits, using osxcross and GCC
# - For Linux 64bit PC, using GCC,
# - For Linux 32bit PC, using GCC.
#
# Uses link-time-optimizations and disable asserts.


out="basicParser-$(git describe --long --dirty)"

# Compile with mingw-w64 cross compiler to 32bit:
rm -rf build/
make CROSS=i686-w64-mingw32- EXT=.exe CFLAGS='-Wall -std=c99 -O2 -flto -DNDEBUG' dist
mv build/${out}.zip ../${out}-win32.zip

# Compile FAT binary for OSX.
# Note that this is simpler with CLANG, but it produces a binary slower and twice the size!
rm -rf build/
#  First compile to 64bit:
make CROSS=x86_64-apple-darwin15- EXT= CFLAGS='-Wall -O2 -flto -DNDEBUG'
mv build/basicParser build/basicParser_m64
#  Clean and compile to 32bit:
make clean
make CROSS=x86_64-apple-darwin15- EXT= CFLAGS='-Wall -m32 -O2 -flto -DNDEBUG'
mv build/basicParser build/basicParser_m32
#  Build the fat binary with "LIPO":
x86_64-apple-darwin15-lipo -create \
    build/basicParser_m32 build/basicParser_m64 \
    -output build/basicParser
#  Pack
make CROSS=x86_64-apple-darwin15- EXT= dist
mv build/${out}.zip ../${out}-maxosx.zip

# Compile for 64bit Linux
rm -rf build/
make CROSS= EXT= CFLAGS='-Wall -O2 -flto -DNDEBUG' dist
mv build/${out}.zip ../${out}-linux64.zip

# Compile for 32bit Linux
rm -rf build/
make CROSS= EXT= CFLAGS='-Wall -m32 -O2 -flto -DNDEBUG' dist
mv build/${out}.zip ../${out}-linux32.zip

