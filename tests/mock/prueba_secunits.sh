#!/bin/sh
# La copia de las 5 unidades de seguridad: o se hace entera, o no queda un archivo
# que identify dé por bueno. Uso: tests/mock/prueba_secunits.sh
R=$(cd "$(dirname "$0")/../.." && pwd)
T=${TMPDIR:-/tmp}/prueba_secunits.$$
mkdir -p "$T"; cd "$T" || exit 1
SRC=$(find "$R/src" -name '*.c' ! -name main.c)
gcc -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_FORTIFY_SOURCE=3 \
  -I"$R/mac" -I"$R/src" -I"$R/src/core" -I"$R/src/utils" -I"$R/src/emp/common" -I"$R/src/eric/common" -I"$R/src/senew" \
  "$R/tests/mock/prueba_secunits.c" $SRC "$R/tests/mock/sp_mock.c" -lminizip -lz -lbz2 -o su 2>/dev/null || { echo "no compila"; exit 1; }
python3 - "$R/tests/mock" <<'PY'
import sys
sys.path.insert(0, sys.argv[1]); sys.argv = ["x", "real"]
import guion
tam = [426, 106, 292, 1, 10]                       # lo que dejó el W580 real
bien = [guion.paquete(0x1D, b"\x00" + bytes([0xA0 + i]) * n).hex() for i, n in enumerate(tam)]
open("bien.hex", "w").write("\n".join(bien) + "\n")
mal = bien[:2] + [guion.paquete(0x1D, b"\x01").hex()] + bien[3:]   # la 3.ª: estado de error
open("mal.hex", "w").write("\n".join(mal) + "\n")
PY
export ASAN_OPTIONS=detect_leaks=0
fallos=0
SEFTOOL_GUION=bien.hex ./su sec_bien.txt > o1.log 2>&1; rc=$?
if [ $rc -eq 0 ] && [ "$(wc -l < sec_bien.txt)" = "5" ] && [ ! -e sec_bien.txt.part ]; then echo "  ok    cinco unidades leídas: archivo con 5 líneas y sin .part"; else echo "  MAL   lectura buena (rc $rc)"; cat o1.log | tail -3; fallos=$((fallos+1)); fi
SEFTOOL_GUION=mal.hex ./su sec_mal.txt > o2.log 2>&1; rc=$?
if [ $rc -ne 0 ] && [ ! -e sec_mal.txt ] && [ -e sec_mal.txt.part ]; then echo "  ok    una unidad con estado de error: NO queda archivo final (queda .part; identify lo reintentará)"; else echo "  MAL   lectura con error (rc $rc, final: $(ls sec_mal.txt 2>&1 | head -1))"; fallos=$((fallos+1)); fi
grep -qE "AddressSanitizer|runtime error" o1.log o2.log && { echo "  MAL   error de memoria"; fallos=$((fallos+1)); } || echo "  ok    sin errores de memoria"
cd /; rm -rf "$T"; exit $fallos
