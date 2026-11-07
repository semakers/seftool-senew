#!/bin/sh
# read-gdfs y write-gdfs contra el teléfono de guion (CS loader SIMULADO, solo
# lectura; ver guion.py). Comprueba, con ASan/UBSan:
#   1. read-gdfs recorre la sesión entera, autoverifica el volcado y deja el
#      archivo idéntico al esperado, con el nombre del modelo.
#   2. Una variable > 0x600 se avisa en la lectura.
#   3. write-gdfs RECHAZA volcados malos antes de mandar nada al teléfono
#      (en el TX solo queda el saludo del EROM: ?, K, S7).
#   4. write-gdfs acepta uno bueno (llega hasta subir el loader y ahí acaba el guion).
# Uso: tests/mock/prueba_gdfs.sh   (tras tests/mock/build.sh)
R=$(cd "$(dirname "$0")/../.." && pwd)
MOCK=$R/tests/mock/seftool-mock
T=${TMPDIR:-/tmp}/prueba_gdfs.$$
mkdir -p "$T"; cd "$T" || exit 1
ln -s "$R/loader" loader; ln -s "$R/anycid" anycid; ln -s "$R/rest" rest
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
fallos=0
ok()  { echo "  ok    $1"; }
mal() { echo "  MAL   $1"; fallos=$((fallos+1)); }
DUMP=GDFS_W580i_35000000000000.bin

echo "1. read-gdfs"
python3 "$R/tests/mock/guion.py" readgdfs normal esperado.bin > g1.hex
SEFTOOL_GUION=g1.hex "$MOCK" -p mock -b 921600 -a read-gdfs > l1.log 2>&1; rc=$?
[ $rc -eq 77 ] && ok "recorre la sesión hasta el final del guion (rc 77)" || mal "rc=$rc (esperado 77)"
cmp -s "backup/$DUMP" esperado.bin && ok "el volcado es idéntico al esperado ($(wc -c < esperado.bin) bytes)" || mal "el volcado difiere"
grep -q "GDFS dump verified" l1.log && ok "se autoverifica" || mal "no se autoverificó"
[ ! -e "backup/$DUMP.INCOMPLETO" ] && ok "no deja .INCOMPLETO tras una lectura buena" || mal "quedó un .INCOMPLETO"
grep -qE "runtime error|AddressSanitizer" l1.log && mal "ASan/UBSan saltó" || ok "sin errores de memoria"

echo "1b. una segunda lectura que falla NO destruye la buena"
sha1=$(cksum < "backup/$DUMP")
python3 -c "import sys; l=open('g1.hex').read().split(); print(chr(10).join(l[:-6]+['00']))" > g1b.hex   # el flujo se rompe: llega un trozo basura de 1 byte (head -n -6 no existe en macOS)
SEFTOOL_GUION=g1b.hex "$MOCK" -p mock -b 921600 -a read-gdfs > l1b.log 2>&1; rc=$?
[ "$(cksum < "backup/$DUMP")" = "$sha1" ] && ok "el respaldo bueno sigue intacto (rc $rc)" || mal "la lectura fallida destruyó el respaldo bueno"
[ -e "backup/$DUMP.INCOMPLETO" ] && ok "lo parcial queda aparte, como .INCOMPLETO" || mal "no quedó .INCOMPLETO"
grep -q "INCOMPLETE" l1b.log && ok "avisa de que está incompleto" || mal "no avisó"
rm -f "backup/$DUMP.INCOMPLETO"

echo "1c. un teléfono que declara 0 variables no es un respaldo"
python3 "$R/tests/mock/guion.py" readgdfs cero cero.bin > g1c.hex 2>/dev/null
SEFTOOL_GUION=g1c.hex "$MOCK" -p mock -b 921600 -a read-gdfs > l1c.log 2>&1
grep -q "reports 0 GDFS variables" l1c.log && ok "0 variables: rechazado, no se guarda nada" || mal "aceptó 0 variables"

echo "2. variable grande"
python3 "$R/tests/mock/guion.py" readgdfs grande grande.bin > g2.hex
mkdir -p b2 && cd b2 && ln -s "$R/loader" loader && ln -s "$R/anycid" anycid && ln -s "$R/rest" rest
SEFTOOL_GUION=../g2.hex "$MOCK" -p mock -b 921600 -a read-gdfs > ../l2.log 2>&1; cd ..
grep -q "NOTE: the largest variable (02/1234" l2.log && ok "avisa de la variable > 0x600 y de qué opciones hacen falta" || mal "no avisó"

echo "3. write-gdfs rechaza volcados malos sin tocar el teléfono"
python3 - <<'PY'
import struct
b=open("esperado.bin","rb").read()
open("trunc.bin","wb").write(b[:len(b)//2])                 # cortado
open("cuenta.bin","wb").write(struct.pack("<I",61)+b[4:])   # cuenta de más
open("corto.bin","wb").write(b"\x01\x00")                   # < 4 bytes
open("vacio.bin","wb").write(b"")
g=open("grande.bin","rb").read(); open("grande_ok.bin","wb").write(g)  # válido pero con var > 0x600
PY
python3 "$R/tests/mock/guion.py" real | head -3 > g3.hex
for f in trunc cuenta corto vacio grande_ok; do
  rm -f tx.log
  SEFTOOL_TX=tx.log SEFTOOL_GUION=g3.hex "$MOCK" -p mock -b 921600 -a write-gdfs $f.bin > l3.log 2>&1; rc=$?
  if grep -q "Refusing to restore\|can't read" l3.log && [ $rc -ne 0 ] && [ $rc -ne 77 ]; then
    n=$(grep -c . tx.log 2>/dev/null)
    [ "${n:-0}" -le 3 ] && ok "$f.bin rechazado (rc $rc), TX = $n paquetes de saludo" || mal "$f.bin: se mandaron $n paquetes"
  else mal "$f.bin no fue rechazado (rc $rc)"; fi
done

python3 "$R/tests/mock/guion.py" real | head -10 > g4.hex
echo "3b. unidades por encima del tope: negarse, o decidirlo a propósito"
for a in "" "--gdfs-max 0x800" "--skip-big" "--gdfs-max 0x20000"; do
  rm -f tx.log
  SEFTOOL_TX=tx.log SEFTOOL_GUION=g4.hex "$MOCK" -p mock -b 921600 -a write-gdfs grande_ok.bin $a > l3b.log 2>&1; rc=$?
  case "$a" in
    "") grep -q "Refusing to restore" l3b.log && grep -q -e "--gdfs-max" l3b.log && ok "por omisión se niega y explica las dos salidas" || mal "por omisión no se negó bien";;
    "--gdfs-max 0x800") grep -q "Dump verified: 61 variables, largest 1792" l3b.log && ok "--gdfs-max 0x800: pasa la comprobación (unidad de 1792 B)" || mal "--gdfs-max 0x800 no pasó";;
    "--skip-big") grep -q "will be SKIPPED: the restore will be PARTIAL" l3b.log && ok "--skip-big: avisa de que será PARCIAL" || mal "--skip-big no avisó";;
    *) grep -q "above the" l3b.log && ok "--gdfs-max absurdo: rechazado" || mal "aceptó un --gdfs-max absurdo";;
  esac
  grep -qE '^8904....03' tx.log 2>/dev/null && mal "con $a se mandó una escritura de variable" || true
done

echo "4. write-gdfs acepta uno bueno (solo hasta subir el loader)"
rm -f tx.log
python3 "$R/tests/mock/guion.py" real | head -10 > g4.hex
SEFTOOL_TX=tx.log SEFTOOL_GUION=g4.hex "$MOCK" -p mock -b 921600 -a write-gdfs esperado.bin > l4.log 2>&1
grep -q "Dump verified: 60 variables" l4.log && ok "el volcado bueno pasa la comprobación" || mal "no pasó"
if grep -qE '^8904....03' tx.log 2>/dev/null; then mal "se mandó una escritura de variable (WRITEVAR)"; else ok "no se mandó ninguna escritura de variable"; fi

cd /; rm -rf "$T"
[ $fallos -eq 0 ] && echo "TODO BIEN" || { echo "$fallos FALLO(S)"; exit 1; }
