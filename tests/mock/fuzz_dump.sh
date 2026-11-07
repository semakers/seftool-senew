#!/bin/sh
# write-gdfs con volcados MUTADOS (cortados, con la cuenta o un tamaño cambiado,
# bytes alterados) contra el mock. Esperado: rechazo limpio (rc 255) o, si el
# volcado mutado sigue siendo coherente, que llegue a subir el loader (guion
# agotado, rc 77). Nunca un error de ASan/UBSan, y NUNCA una escritura
# (WRITEVAR) en el TX. Uso: fuzz_dump.sh <desde> <hasta>
R=$(cd "$(dirname "$0")/../.." && pwd)
DESDE=$1; HASTA=$2
SALIDA=${SALIDA:-/tmp/seftool-fuzzdump}
W=$SALIDA/trabajo-$DESDE; mkdir -p "$W" "$SALIDA/fallos"; cd "$W" || exit 1
ln -sfn "$R/loader" loader; ln -sfn "$R/anycid" anycid; ln -sfn "$R/rest" rest
if command -v timeout >/dev/null 2>&1; then TO="timeout 20"; else TO="perl -e alarm(20);exec(@ARGV); --"; fi
export ASAN_OPTIONS=exitcode=99:detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:exitcode=98
python3 "$R/tests/mock/guion.py" readgdfs normal base.bin > /dev/null
python3 "$R/tests/mock/guion.py" real | head -10 > g.hex
s=$DESDE; malos=0
while [ $s -le $HASTA ]; do
  python3 - "$s" <<'PY'
import random, struct, sys
r = random.Random(int(sys.argv[1]))
b = bytearray(open("base.bin", "rb").read())
for _ in range(r.randrange(1, 5)):
    t = r.random()
    if t < 0.25:
        b = b[: r.randrange(0, len(b) + 1)]
    elif t < 0.45 and len(b) >= 4:
        b[0:4] = struct.pack("<I", r.choice([0, 1, 59, 61, 0xFFFFFFFF, r.randrange(1 << 32)]))
    elif t < 0.75 and len(b) > 8:
        i = r.randrange(4, len(b) - 4)
        b[i : i + 4] = struct.pack("<I", r.choice([0, 1, 0x5FF, 0x600, 0x601, 0xFFFF, 0xFFFFFFFF, r.randrange(1 << 32)]))
    elif len(b):
        b[r.randrange(len(b))] = r.randrange(256)
open("m.bin", "wb").write(bytes(b))
PY
  rm -f tx.log
  SEFTOOL_TX=tx.log SEFTOOL_GUION=g.hex $TO "$R/tests/mock/seftool-mock" -p mock -b 921600 -a write-gdfs m.bin > out.log 2>&1
  rc=$?
  bad=""
  case $rc in 0|1|77|255) ;; *) bad="rc=$rc";; esac
  grep -qE 'AddressSanitizer|runtime error' out.log && bad="$bad sanitizer"
  grep -qE '^8904....03' tx.log 2>/dev/null && bad="$bad WRITEVAR"
  if [ -n "$bad" ]; then malos=$((malos+1)); cp m.bin "$SALIDA/fallos/dump-$s.bin"; cp out.log "$SALIDA/fallos/dump-$s.log"; echo "write-gdfs semilla $s: $bad"; fi
  s=$((s+1))
done
echo "write-gdfs(dump) $DESDE-$HASTA: $malos fallos"
