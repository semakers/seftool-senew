#!/bin/sh
# Analizador de scripts GDFS con líneas hostiles (ver prueba_script.c).
R=$(cd "$(dirname "$0")/../.." && pwd)
T=${TMPDIR:-/tmp}/prueba_script.$$
mkdir -p "$T"; cd "$T" || exit 1
SRC=$(find "$R/src" -name '*.c' ! -name main.c)
gcc -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_FORTIFY_SOURCE=3 \
  -I"$R/mac" -I"$R/src" -I"$R/src/core" -I"$R/src/utils" -I"$R/src/emp/common" -I"$R/src/eric/common" -I"$R/src/senew" \
  "$R/tests/mock/prueba_script.c" $SRC "$R/tests/mock/sp_mock.c" -lminizip -lz -lbz2 -o ps 2>/dev/null || { echo "no compila"; exit 1; }
echo "00" > nada.hex
python3 - <<'PY'
hexa = lambda n: "AB" * (n // 2)
L = []
L.append("gdfswrite:0002" + "0100" + hexa(6000))          # > 4095: línea partida
L.append("gdfswrite:0002" + "0100" + hexa(3400))          # entre 3328 y 4095 dígitos
L.append("gdfswrite:0002" + "0100" + "ABC")               # dígitos impares
L.append("gdfswrite:ZZZZ")                                 # basura
L.append("gdfswrite:")                                     # vacía
L.append("gdfsread:ZZZ")
L.append("lo que sea")
L.append("gdfswrite:0002" + "0100" + hexa(3328 + 2))       # un byte de más sobre el tope
open("hostil.txt", "w").write("\n".join(L) + "\n")
open("bueno.txt", "w").write("gdfswrite:00020100" + "AB" * 16 + "\n")
PY
ASAN_OPTIONS=detect_leaks=0 SEFTOOL_GUION=nada.hex ./ps hostil.txt > o1.log 2>&1; rc=$?
fallos=0
if grep -qE "AddressSanitizer|runtime error" o1.log; then echo "  MAL   líneas hostiles: error de memoria"; grep -m3 -E "ERROR|runtime error|#[0-3] " o1.log; fallos=1
else echo "  ok    líneas hostiles: sin errores de memoria (rc $rc)"; fi
grep -c "skipped\|too long\|longer than" o1.log | sed 's/^/        líneas rechazadas con aviso: /'
grep -q "WITH .* ERROR" o1.log && echo "  ok    el script termina como FALLIDO (no 'successfully')" || { echo "  MAL   no marcó el script como fallido"; fallos=1; }
ASAN_OPTIONS=detect_leaks=0 SEFTOOL_GUION=nada.hex ./ps bueno.txt > o2.log 2>&1
grep -q "Writing GDFS var 0002/0100" o2.log && echo "  ok    una línea buena sí se interpreta (llega a escribir)" || { echo "  MAL   la línea buena no llegó a escribir"; fallos=1; }
cd /; rm -rf "$T"; exit $fallos
