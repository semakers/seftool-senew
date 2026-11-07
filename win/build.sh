#!/bin/sh
# Compila seftool en Windows con MinGW-w64 (MSYS2, shell MINGW64), SIN libserialport:
# habla USB directo con libusb (mac/sp_usb.c, el mismo del Mac), así que el puerto
# es «usb». Hace falta el driver WinUSB asignado al «SEMC USB Flash» (0fce:adde).
#
#   win/build.sh [dir-libusb]       (por omisión /c/dev/libusb-1.0.27: el .7z oficial
#                                    de libusb ya extraído, con include/ y MinGW64/)
set -eu
RAIZ=$(cd "$(dirname "$0")/.." && pwd)
LIBUSB=${1:-/c/dev/libusb-1.0.27}
[ -f "$LIBUSB/include/libusb.h" ] || { echo "no encuentro $LIBUSB/include/libusb.h"; exit 1; }

FUENTES=$(find "$RAIZ/src" -name '*.c')
gcc -std=gnu99 -O2 -Wall -Wno-missing-braces -Wno-unused-parameter \
	-I"$RAIZ/mac" -I"$LIBUSB/include" \
	-I"$RAIZ/src" -I"$RAIZ/src/core" -I"$RAIZ/src/utils" -I"$RAIZ/src/emp/common" \
	-I"$RAIZ/src/eric/common" -I"$RAIZ/src/senew" \
	$FUENTES "$RAIZ/mac/sp_usb.c" \
	"$LIBUSB/MinGW64/static/libusb-1.0.a" \
	-lminizip -lbz2 -lz -lsetupapi -lole32 -luuid -lcfgmgr32 -ladvapi32 \
	-static -o "$RAIZ/seftool.exe"
echo "OK: $RAIZ/seftool.exe"
