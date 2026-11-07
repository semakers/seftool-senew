#!/bin/sh
# seftool contra el teléfono de guion, con ASan + UBSan + FORTIFY (lo que
# en macOS aborta, aquí también). Uso: tests/mock/build.sh → tests/mock/seftool-mock
set -eu
R=$(cd "$(dirname "$0")/../.." && pwd)
gcc -std=gnu99 -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=undefined \
	-D_FORTIFY_SOURCE=3 -Wall -Wno-missing-braces -Wno-unused-parameter \
	-I"$R/mac" -I"$R/src" -I"$R/src/core" -I"$R/src/utils" -I"$R/src/emp/common" \
	-I"$R/src/eric/common" -I"$R/src/senew" \
	$(find "$R/src" -name '*.c') "$R/tests/mock/sp_mock.c" \
	-lminizip -lz -lbz2 -o "$R/tests/mock/seftool-mock"
echo "OK: $R/tests/mock/seftool-mock"
