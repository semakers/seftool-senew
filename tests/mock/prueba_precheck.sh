#!/bin/sh
# La comprobación previa de flash-anycid con firmware real, una imagen de
# senew (convert raw2babe) y archivos rotos. Uso: prueba_precheck.sh <main.mbn> <fs.fbn>
R=$(cd "$(dirname "$0")/../.." && pwd)
MAIN=${1:?uso: prueba_precheck.sh MAIN.mbn FS.fbn (ficheros de firmware que debes aportar tú)}
FS=${2:?uso: prueba_precheck.sh MAIN.mbn FS.fbn}
T=${TMPDIR:-/tmp}/prueba_precheck.$$
mkdir -p "$T"; cd "$T" || exit 1
SRC=$(find "$R/src" -name '*.c' ! -name main.c)
gcc -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_FORTIFY_SOURCE=3 \
  -I"$R/mac" -I"$R/src" -I"$R/src/core" -I"$R/src/utils" -I"$R/src/emp/common" -I"$R/src/eric/common" -I"$R/src/senew" \
  "$R/tests/mock/prueba_precheck.c" $SRC "$R/tests/mock/sp_mock.c" -lminizip -lz -lbz2 -o pc 2>/dev/null || { echo "no compila"; exit 1; }
export ASAN_OPTIONS=detect_leaks=0 SEFTOOL_GUION=/dev/null
fallos=0
espera() { # nombre archivo rc_esperado texto_esperado [cid [force]]
  out=$(./pc "$2" $5 2>&1); rc=$?
  if echo "$out" | grep -qE "AddressSanitizer|runtime error"; then echo "  MAL   $1: error de memoria"; fallos=$((fallos+1)); return; fi
  if [ "$rc" = "$3" ] && echo "$out" | grep -q "$4"; then echo "  ok    $1"; else echo "  MAL   $1 (rc $rc, esperado $3, texto «$4»)"; echo "$out" | sed 's/^/          /' | head -4; fallos=$((fallos+1)); fi
}
espera "main real del W580: SHA-1 verificado" "$MAIN" 0 "460 blocks, SHA-1 verified"
espera "fs real del W580: SHA-1 verificado" "$FS" 0 "209 blocks, SHA-1 verified"
espera "main real con --cid 52 (el de su cabecera): pasa" "$MAIN" 0 "SHA-1 verified" "52"
espera "main real con --cid 51: rechazado (CID distinto)" "$MAIN" 1 "CID differs" "51"
espera "main real con --cid 51 forzado: pasa con aviso" "$MAIN" 0 "WARNING (the image CID differs" "51 force"
espera "el main real cae dentro de la ventana de la NOR" "$MAIN" 0 "writes NOR 0x44140000..0x45e00000"
python3 - "$MAIN" <<'PY'
import sys, struct, random
b = open(sys.argv[1], "rb").read()
open("trunc.mbn", "wb").write(b[: len(b) // 2])                       # cortado a la mitad
open("poco.mbn", "wb").write(b[:100])                                  # menos que la cabecera
c = bytearray(b); c[1000000] ^= 0xFF; open("bit.mbn", "wb").write(c)   # un byte alterado dentro de un bloque
d = bytearray(b); d[744:748] = struct.pack("<I", 459); open("cuenta.mbn", "wb").write(d)  # cuenta de bloques cambiada
r = random.Random(3); open("raw.bin", "wb").write(bytes(r.randrange(256) for _ in range(200000)))
PY
espera "main cortado a la mitad: rechazado" trunc.mbn 1 "Bad BABE\|header declares\|not full\|Can not"
espera "archivo menor que la cabecera: rechazado" poco.mbn 1 "Can not read"
espera "un byte alterado dentro de un bloque: rechazado" bit.mbn 1 "Bad BABE"
espera "cuenta de bloques alterada: rechazada" cuenta.mbn 1 "Bad BABE\|header declares\|not full\|Can not"
# imágenes de senew a distintas direcciones
for a in 0x44000000 0x44140100 0x47FF0000; do
  rm -f raw.bin.ssw; "$R/tests/mock/seftool-mock" -a convert raw2babe raw.bin $a > /dev/null 2>&1
  [ -f raw.bin.ssw ] && cp raw.bin.ssw "ssw_$a.ssw"
done
espera "senew en 0x44000000 (zona de arranque): rechazada" ssw_0x44000000.ssw 1 "outside the main/fs window"
espera "senew en 0x44140100 (sin alinear): rechazada" ssw_0x44140100.ssw 1 "not aligned"
espera "senew que se sale de los 64 MiB: rechazada" ssw_0x47FF0000.ssw 1 "outside the main/fs window"
espera "senew en 0x47FF0000 forzada: pasa con aviso" ssw_0x47FF0000.ssw 0 "WARNING (writes outside" "0 force"
rm -f raw.bin.ssw; "$R/tests/mock/seftool-mock" -a convert raw2babe raw.bin 0x44140000 > /dev/null 2>&1 || true
if [ -f raw.bin.ssw ]; then
  espera "imagen de senew a 0x44140000 (como SENUS): estructura verificada" raw.bin.ssw 0 "block table verified"
  espera "imagen de senew a 0x44140000 con --cid 52: pasa (cabecera sin CID que comparar)" raw.bin.ssw 0 "block table verified" "52"
  # determinista: dos conversiones iguales dan archivos idénticos (antes salía basura del montón)
  cp raw.bin.ssw uno.ssw; rm raw.bin.ssw
  "$R/tests/mock/seftool-mock" -a convert raw2babe raw.bin 0x44140000 > /dev/null 2>&1
  cmp -s uno.ssw raw.bin.ssw && echo "  ok    raw2babe es determinista (dos conversiones idénticas)" || { echo "  MAL   raw2babe no es determinista"; fallos=$((fallos+1)); }
else echo "  MAL   convert raw2babe no produjo .ssw"; fallos=$((fallos+1)); fi
# Las 9 imágenes del MANIFIESTO de senew: pasan todas la comprobación previa
# con --cid 52, bajo UBSan sin recuperación. Antes del arreglo de babe.c:2373
# (`get_word` en vez del cast desalineado), UBSan abortaba en cada una.
APARATO=${APARATO:?APARATO = carpeta build/aparato de senew-w580}
if [ -d "$APARATO" ]; then
  echo "- las 9 imágenes del MANIFIESTO (precheck con --cid 52, UBSan estricto)"
  for H in hito_2 hito_6 hito_3 hito_3_todas hito_4 hito_5 \
           paso_7_sin_escritura sistema_sin_escritura sistema; do
    bin="$APARATO/${H}_44140000.bin"
    if [ ! -f "$bin" ]; then echo "  (saltado) $bin no está"; continue; fi
    rm -f "${H}.ssw"
    "$R/tests/mock/seftool-mock" -a convert raw2babe "$bin" 0x44140000 > conv_$H.log 2>&1 \
      && [ -f "${bin}.ssw" ] && cp "${bin}.ssw" "${H}.ssw" \
      || { echo "  MAL   convert raw2babe $H"; fallos=$((fallos+1)); continue; }
    espera "$H: precheck --cid 52 pasa sin UBSan" "${H}.ssw" 0 "block table verified" "52"
  done
else
  echo "- (saltado) no está $APARATO; sin las 9 imágenes del MANIFIESTO"
fi
cd /; rm -rf "$T"; [ $fallos -eq 0 ] && echo "TODO BIEN" || { echo "$fallos FALLO(S)"; exit 1; }
