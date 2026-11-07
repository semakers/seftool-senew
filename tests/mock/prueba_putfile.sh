#!/bin/sh
# upload-anycid contra el teléfono de guion. Dos pasadas:
#   (a) DORADA: todas las respuestas FSX con data[1]=0 → upload sale entero.
#   (b) ENOSPC: inyecta data[1]=0x12 en el chunk 10 (el PUTFILE[0] del jar) →
#       el fork avisa «Putfile refused by phone: cmd 0x03 status 0x12» y aborta
#       antes de mandar más chunks (bytes salientes < pasada dorada).
# Uso: tests/mock/prueba_putfile.sh    (tras tests/mock/build.sh)
R=$(cd "$(dirname "$0")/../.." && pwd)
MOCK=$R/tests/mock/seftool-mock
T=${TMPDIR:-/tmp}/prueba_putfile.$$
mkdir -p "$T"; cd "$T" || exit 1
ln -s "$R/loader" loader; ln -s "$R/anycid" anycid; ln -s "$R/rest" rest
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
fallos=0
ok()  { echo "  ok    $1"; }
mal() { echo "  MAL   $1"; fallos=$((fallos+1)); }

echo "(a) dorada: todas las respuestas OK, upload entero"
python3 "$R/tests/mock/guion.py" putfile ok > ok.hex
SEFTOOL_TX=tx_ok.log SEFTOOL_GUION=ok.hex "$MOCK" -p mock -b 921600 -a upload-anycid --anycid > out_ok.log 2>&1; rc=$?
# El guion acaba limpio con _exit(77) tras agotarse: tratamos 77 como éxito.
if [ "$rc" = 0 ] || [ "$rc" = 77 ]; then ok "upload-anycid OK (rc $rc)"; else mal "rc $rc (esperado 0 ó 77)"; sed 's/^/    /' out_ok.log | tail -6; fi
grep -qE "AddressSanitizer|runtime error" out_ok.log && mal "ASan/UBSan saltó" || ok "sin errores de memoria"
grep -q "Putfile refused" out_ok.log && mal "no debería avisar aquí" || ok "no hay aviso de rechazo"
# contar las escrituras CMD_CS_FSX (líneas que empiezan por 8903): esperamos 65.
fsx_ok=$(grep -cE '^8903' tx_ok.log)
# El binario emite las escrituras concatenadas por write; cada envío = ACK+paquete en dos líneas separadas → cuento solo 8903.
[ "$fsx_ok" = 65 ] && ok "65 escrituras FSX (coincide con la cuenta de la auditoría)" || mal "FSX = $fsx_ok (esperado 65)"

# PUTFILE del XML: FSX #7 (chunk 0) y #8 (chunk 1). PUTFILE del jar: #13 (chunk 0)
# y #14..#56 (chunks 1..43). Elegimos #7: el PRIMER chunk del PRIMER archivo. Sin
# el arreglo A-1 (csloader.c:1227 solo miraba repl.cmd), un `data[1]!=0` aquí
# pasaba por bueno y la subida seguía escribiendo basura. Con el arreglo, aborta.
echo "(b) ENOSPC en la FSX #7 (PUTFILE[0] del XML)"
python3 "$R/tests/mock/guion.py" putfile 7 > nospc.hex
SEFTOOL_TX=tx_mal.log SEFTOOL_GUION=nospc.hex "$MOCK" -p mock -b 921600 -a upload-anycid --anycid > out_mal.log 2>&1; rc=$?
[ "$rc" != 0 ] && [ "$rc" != 77 ] && ok "upload-anycid abortó (rc $rc)" || mal "no abortó (rc $rc)"
grep -qE "AddressSanitizer|runtime error" out_mal.log && mal "ASan/UBSan saltó" || ok "sin errores de memoria"
grep -q "Putfile refused by phone: cmd 0x03 status 0x12" out_mal.log && ok "aviso con cmd+status esperados" || { mal "no salió el aviso"; sed 's/^/    /' out_mal.log | tail -5; }
fsx_mal=$(grep -cE '^8903' tx_mal.log)
[ "$fsx_mal" -lt "$fsx_ok" ] && ok "la subida se cortó: $fsx_mal FSX < $fsx_ok" || mal "mandó tantos como la dorada ($fsx_mal)"

cd /; rm -rf "$T"
[ $fallos -eq 0 ] && echo "TODO BIEN" || { echo "$fallos FALLO(S)"; exit 1; }
