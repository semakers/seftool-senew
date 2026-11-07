#!/bin/sh
# Compila seftool en macOS con clang, sin Homebrew ni cmake.
# libusb y minizip se compilan de las fuentes en deps/ (libusb-1.0.27,
# zlib-1.3.1); zlib es la del sistema. Uso: mac/build.sh <dir-deps>
set -eu
RAIZ=$(cd "$(dirname "$0")/.." && pwd)
DEPS=${1:-$HOME/dev/seftool-mac/deps}
OBJ=$RAIZ/build-mac
mkdir -p "$OBJ/inc"

cd "$DEPS"
[ -d libusb-1.0.27 ] || tar xjf libusb.tar.bz2
[ -d zlib-1.3.1 ] || tar xzf zlib.tar.gz
ln -sfn "$DEPS/zlib-1.3.1/contrib/minizip" "$OBJ/inc/minizip"

U=$DEPS/libusb-1.0.27/libusb
if [ ! -f "$OBJ/libusb.a" ]; then
	(cd "$OBJ" && clang -c -O2 -I"$DEPS/libusb-1.0.27/Xcode" -I"$U" \
		"$U/core.c" "$U/descriptor.c" "$U/hotplug.c" "$U/io.c" "$U/strerror.c" \
		"$U/sync.c" "$U/os/darwin_usb.c" "$U/os/events_posix.c" "$U/os/threads_posix.c" &&
		ar rcs libusb.a core.o descriptor.o hotplug.o io.o strerror.o sync.o \
			darwin_usb.o events_posix.o threads_posix.o)
fi

M=$DEPS/zlib-1.3.1/contrib/minizip
FUENTES=$(find "$RAIZ/src" -name '*.c')
clang -std=gnu99 -O2 -Wall -Wno-missing-braces -Wno-unused-parameter \
	-I"$RAIZ/mac" -I"$OBJ/inc" -I"$U" \
	-I"$RAIZ/src" -I"$RAIZ/src/core" -I"$RAIZ/src/utils" -I"$RAIZ/src/emp/common" \
	-I"$RAIZ/src/eric/common" -I"$RAIZ/src/senew" \
	$FUENTES "$RAIZ/mac/sp_usb.c" "$M/unzip.c" "$M/ioapi.c" \
	"$OBJ/libusb.a" -lz -lobjc \
	-framework IOKit -framework CoreFoundation -framework Security \
	-o "$RAIZ/seftool"
echo "OK: $RAIZ/seftool"
