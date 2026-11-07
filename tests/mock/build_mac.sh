#!/bin/sh
# Banco del teléfono de guion en macOS (clang + ASan + UBSan + FORTIFY),
# con el minizip de las fuentes de zlib. Uso: tests/mock/build_mac.sh [dir-deps]
set -eu
R=$(cd "$(dirname "$0")/../.." && pwd)
DEPS=${1:-$HOME/dev/seftool-mac/deps}
M=$DEPS/zlib-1.3.1/contrib/minizip
mkdir -p "$R/build-mac/inc"; ln -sfn "$M" "$R/build-mac/inc/minizip"
clang -std=gnu99 -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=undefined \
	-D_FORTIFY_SOURCE=2 -Wall -Wno-missing-braces -Wno-unused-parameter \
	-I"$R/mac" -I"$R/build-mac/inc" -I"$R/src" -I"$R/src/core" -I"$R/src/utils" \
	-I"$R/src/emp/common" -I"$R/src/eric/common" -I"$R/src/senew" \
	$(find "$R/src" -name '*.c') "$R/tests/mock/sp_mock.c" "$M/unzip.c" "$M/ioapi.c" \
	-lz -o "$R/tests/mock/seftool-mock"
echo "OK: $R/tests/mock/seftool-mock"
