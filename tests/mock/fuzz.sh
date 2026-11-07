#!/bin/sh
# Fuzzing de seftool contra el teléfono de guion.
# Uso: [FUZZ_MODO=cs] tests/mock/fuzz.sh <acción> <desde> <hasta> [args extra de seftool]
#   FUZZ_MODO=cs usa la sesión simulada del CS loader (read-gdfs, upload-anycid).
#   AVISO: no editar este script mientras corre una campaña; lanzar una copia.
# Cada semilla: K trozos reales del W580 (K=semilla%15) + basura. Guarda en
# $SALIDA/fallos/ los guiones que hacen saltar ASan/UBSan o una señal.
R=$(cd "$(dirname "$0")/../.." && pwd)
ACCION=$1; DESDE=$2; HASTA=$3; shift 3
SALIDA=${SALIDA:-/tmp/seftool-fuzz}
# macOS no trae timeout: perl alarm hace lo mismo.
if command -v timeout >/dev/null 2>&1; then TO="timeout 20"; else TO="perl -e alarm(20);exec(@ARGV); --"; fi
W=$SALIDA/trabajo-$ACCION-$DESDE; mkdir -p "$W" "$SALIDA/fallos"
cd "$W"; ln -sfn "$R/loader" loader; ln -sfn "$R/anycid" anycid; ln -sfn "$R/rest" rest
export ASAN_OPTIONS=exitcode=99:detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:exitcode=98:print_stacktrace=1
s=$DESDE; malos=0
while [ $s -le $HASTA ]; do
  case "${FUZZ_MODO:-eromcs}" in
    cs) python3 "$R/tests/mock/guion.py" fuzzcs $s > g.hex ;;                  # sesión del CS loader alterada
    *)  python3 "$R/tests/mock/guion.py" fuzz $s $((s % 15)) > g.hex ;;        # traza real + basura
  esac
  SEFTOOL_GUION=g.hex $TO "$R/tests/mock/seftool-mock" -p mock -b 921600 -a $ACCION "$@" > out.log 2>&1
  rc=$?
  case $rc in
    0|1|77|78|255) ;;
    124) echo "$ACCION semilla $s: COLGADO 20 s"; cp g.hex "$SALIDA/fallos/colgado-$ACCION-$s.hex";;
    *) malos=$((malos+1)); cp g.hex "$SALIDA/fallos/$ACCION-$s.hex"; cp out.log "$SALIDA/fallos/$ACCION-$s.log"
       echo "$ACCION semilla $s rc=$rc: $(grep -m1 -E 'ERROR: AddressSanitizer|runtime error|SUMMARY' out.log)";;
  esac
  rm -rf backup
  s=$((s+1))
done
echo "$ACCION $DESDE-$HASTA: $malos fallos"
