#!/bin/sh
# read-flash --anycid, SIN teléfono y SIN simular ningún loader (regla dura del mantenedor):
#   (C) funciones puras con búferes sintéticos: ventana, plan de 1024 trozos, escritor atómico.
#   (B) sin --anycid, repitiendo la captura REAL del identify (cadena P3M): falla y explica la receta.
# Bajo ASan + UBSan + FORTIFY. Uso: tests/mock/prueba_readflash.sh   (tras tests/mock/build.sh)
R=$(cd "$(dirname "$0")/../.." && pwd)
T=${TMPDIR:-/tmp}/prueba_readflash.$$
mkdir -p "$T/backup"; cd "$T" || exit 1
SRC=$(find "$R/src" -name '*.c' ! -name main.c)
INC="-I$R/mac -I$R/src -I$R/src/core -I$R/src/utils -I$R/src/emp/common -I$R/src/eric/common -I$R/src/senew"
SAN="-std=gnu99 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_FORTIFY_SOURCE=3 -Wall -Wno-missing-braces -Wno-unused-parameter"
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
fallos=0
ok()  { echo "  ok    $1"; }
mal() { echo "  MAL   $1"; fallos=$((fallos+1)); }

# shellcheck disable=SC2086
gcc $SAN $INC "$R/tests/mock/prueba_readflash.c" $SRC "$R/tests/mock/sp_mock.c" -lminizip -lz -lbz2 -o rf 2> cc.log \
  || { echo "no compila el harness:"; cat cc.log; exit 1; }
grep -q "warning" cc.log && { echo "avisos del compilador:"; cat cc.log; fallos=$((fallos+1)); }

echo "(C) funciones puras: ventana, plan y escritor atómico"
./rf > c.log 2>&1; rc=$?
grep -v "^SHA1_" c.log
[ $rc -eq 0 ] && ok "el harness termina sin fallos" || mal "el harness falló (rc $rc)"
grep -qE "AddressSanitizer|runtime error" c.log && mal "ASan/UBSan saltó" || ok "sin errores de memoria"

# El SHA-1 que imprime seftool tiene que ser el de verdad (lo calcula sha1sum, no el código probado).
dado=$(sed -n 's/^SHA1_BUENO //p' c.log)
real=$(sha1sum backup/bueno.bin 2>/dev/null | cut -d' ' -f1)
if [ -n "$dado" ] && [ "$dado" = "$real" ]; then ok "SHA-1 del volcado == sha1sum ($real)"; else mal "SHA-1 impreso '$dado' != sha1sum '$real'"; fi

# ---- (B) y (A) hablan con seftool-mock: hace falta tests/mock/build.sh
MOCK=$R/tests/mock/seftool-mock
[ -x "$MOCK" ] || { echo "falta $MOCK: corre tests/mock/build.sh antes"; cd /; rm -rf "$T"; exit 1; }
ln -s "$R/loader" loader; ln -s "$R/anycid" anycid; ln -s "$R/rest" rest

echo "(B) read-flash SIN --anycid en un DB2020 RED, contra la captura REAL del identify (cadena P3M)"
python3 "$R/tests/mock/guion.py" real | head -10 > real10.hex
SEFTOOL_TX=tx_b.log SEFTOOL_GUION=real10.hex "$MOCK" -p mock -b 921600 -a read-flash start 0x44000000 size 0x4000000 > out_b.log 2>&1; rc=$?
[ $rc -eq 255 ] && ok "falla (rc 255)" || { mal "rc $rc (esperado 255)"; tail -5 out_b.log | sed 's/^/    /'; }
grep -q "ACTIVE CID:52 COLOR:RED" out_b.log && ok "imprime «ACTIVE CID:52 COLOR:RED»" || mal "no salió el CID/color activos"
grep -q "This cid & cert is not supported" out_b.log && ok "dice que ese CID/cert no está soportado" || mal "falta el aviso del fork"
grep -q "upload-anycid" out_b.log && grep -qe "--anycid" out_b.log && ok "explica la receta (upload-anycid, executer, --anycid)" || mal "no explica qué hacer"
grep -q "Security disabled" out_b.log && mal "no debería haber llegado a «Security disabled»" || ok "no cargó ningún loader de exploit"
n32=$(grep -c '^8932' tx_b.log); [ "$n32" = 0 ] && ok "ninguna orden de lectura 0x32 salió al teléfono" || mal "salieron $n32 órdenes 0x32"
grep -qE "AddressSanitizer|runtime error" out_b.log && mal "ASan/UBSan saltó" || ok "sin errores de memoria"

echo "(I) --imei: otro teléfono se rechaza en cuanto el primer loader dice su IMEI, antes de cargar nada más"
# La captura REAL del identify trae el piloader P3M y su OTP (IMEI sintético 35000000000000). Con ese IMEI esperado
# la orden sigue hasta cargar el CS loader (y se acaba el guion: rc 77); con otro se queda justo tras el piloader.
SEFTOOL_TX=tx_i1.log SEFTOOL_GUION=real10.hex "$MOCK" -p mock -b 921600 -a upload-anycid --imei 35000000000000 > o_i1.log 2> e_i1.log; rc1=$?
SEFTOOL_TX=tx_i2.log SEFTOOL_GUION=real10.hex "$MOCK" -p mock -b 921600 -a upload-anycid --imei 99999999999999 > o_i2.log 2> e_i2.log; rc2=$?
n1=$(grep -c . tx_i1.log); n2=$(grep -c . tx_i2.log)
if [ $rc1 -eq 77 ] && grep -q "ACTIVE CID:52" o_i1.log && ! grep -q "imei-check" e_i1.log; then ok "con el IMEI esperado la orden sigue (rc 77: se acabó el guion al cargar el siguiente loader)"; else mal "con el IMEI esperado (rc $rc1; $(head -n 1 e_i1.log))"; fi
if [ $rc2 -eq 255 ] && tail -n 1 e_i2.log | grep -q '^imei-check: REFUSED' && grep -q "35000000000000" e_i2.log && ! grep -q "ACTIVE CID" o_i2.log; then ok "con otro IMEI: rc 255, «imei-check: REFUSED» y dice cuál era"; else mal "con otro IMEI (rc $rc2; $(tail -n 1 e_i2.log))"; fi
[ "$n2" -lt "$n1" ] && ok "y se queda corto: $n2 escrituras frente a $n1 (no se cargó el siguiente loader)" || mal "con otro IMEI salieron $n2 escrituras (con el bueno, $n1)"
grep -qE "AddressSanitizer|runtime error" e_i1.log e_i2.log && mal "ASan/UBSan saltó" || ok "sin errores de memoria"
for malo in "" "1234567890123456" "35 000" "abc-def" "_" "______________"; do
	rm -f tx_i3.log
	SEFTOOL_TX=tx_i3.log SEFTOOL_GUION=real10.hex "$MOCK" -p mock -b 921600 -a identify --imei "$malo" > o_i3.log 2> e_i3.log; rc=$?
	if [ $rc -eq 1 ] && grep -q "Error: --imei" e_i3.log && [ ! -e tx_i3.log ]; then ok "--imei «$malo»: rechazado por el parser"; else mal "--imei «$malo» (rc $rc; $(head -n 1 e_i3.log))"; fi
done

echo "(A) las compuertas rechazan ANTES de mandar un solo byte de loader (solo el saludo real: 3 escrituras)"
python3 "$R/tests/mock/guion.py" real | head -3 > hs3.hex
rechaza() {   # rechaza <qué> <texto que debe decir stderr> <argumentos de read-flash…>
	que=$1; patron=$2; shift 2
	SEFTOOL_TX=tx_a.log SEFTOOL_GUION=hs3.hex "$MOCK" -p mock -b 921600 -a read-flash "$@" > out_a.log 2> err_a.log; rc=$?
	if [ $rc -eq 255 ] && tail -n 1 err_a.log | grep -q '^read-flash --anycid: REFUSED' && grep -q "$patron" err_a.log \
	   && [ "$(grep -c . tx_a.log)" = 3 ] && ! grep -q '^51483030' tx_a.log \
	   && ! grep -qE "AddressSanitizer|runtime error" err_a.log; then
		ok "$que"
	else
		mal "$que (rc $rc, escrituras $(grep -c . tx_a.log), última línea: $(tail -n 1 err_a.log))"
	fi
}
rechaza "sin --cid: REFUSED, ni un byte de loader"        "falta --cid"       start 0x44000000 size 0x4000000 --anycid
rechaza "--cid 53 (sin verificar): REFUSED"                "sin verificar"     start 0x44000000 size 0x4000000 --anycid --cid 53
rechaza "--cid 99 (CID desconocido): REFUSED"              "49, 51, 52"        start 0x44000000 size 0x4000000 --anycid --cid 99
rechaza "start 0x43FF0000: empieza por debajo de la NOR"   "debajo"            start 0x43FF0000 size 0x20000 --anycid --cid 52
rechaza "start 0x47FF0000 size 0x20000: se sale por arriba" "más allá"          start 0x47FF0000 size 0x20000 --anycid --cid 52
rechaza "start 0xFFFF0000 size 0x20000: da la vuelta"      "32 bits"           start 0xFFFF0000 size 0x20000 --anycid --cid 52

# --restore-boot: la nota «boot-area repair is OFF» sale sin él y desaparece con él.
SEFTOOL_TX=tx_a.log SEFTOOL_GUION=hs3.hex "$MOCK" -p mock -b 921600 -a read-flash start 0x44000000 size 0x4000000 --anycid --cid 53 > o1.log 2> e1.log
SEFTOOL_TX=tx_a.log SEFTOOL_GUION=hs3.hex "$MOCK" -p mock -b 921600 -a read-flash start 0x44000000 size 0x4000000 --anycid --cid 53 --restore-boot > o2.log 2> e2.log
grep -q "boot-area repair is OFF" o1.log && ok "sin --restore-boot: avisa «boot-area repair is OFF»" || mal "falta la nota sin --restore-boot"
grep -q "boot-area repair is OFF" o2.log && mal "con --restore-boot no debería decir OFF" || ok "con --restore-boot la nota desaparece"

echo "(P) el parser de read-flash rechaza lo que no cabe en 32 bits, antes de abrir ningún puerto"
parser() {   # parser <qué> <texto en stderr> <argumentos de read-flash…>
	que=$1; patron=$2; shift 2
	rm -f tx_p.log
	SEFTOOL_TX=tx_p.log SEFTOOL_GUION=hs3.hex "$MOCK" -p mock -b 921600 -a read-flash "$@" > out_p.log 2> err_p.log; rc=$?
	if [ $rc -eq 1 ] && grep -q "$patron" err_p.log && [ ! -e tx_p.log ] && ! grep -qE "AddressSanitizer|runtime error" err_p.log; then
		ok "$que"
	else
		mal "$que (rc $rc, tx.log existe: $([ -e tx_p.log ] && echo sí || echo no); $(head -n 1 err_p.log))"
	fi
}
parser "size 0 → rc 1, sin abrir el puerto"                         "read-flash"        start 0x44000000 size 0
parser "start 0x144000000 (no cabe en 32 bits)"                      "invalid start"     start 0x144000000 size 0x1000
parser "size 4294967296 (2^32)"                                      "invalid size"      start 0x44000000 size 4294967296
parser "size con basura al final (0x1000x)"                          "invalid size"      start 0x44000000 size 0x1000x
parser "start no numérico"                                           "invalid start"     start abc size 0x1000
parser "block 65536 desborda (×64 KiB)"                              "invalid block"     start 0x44000000 block 65536
parser "block negativo"                                              "invalid block"     start 0x44000000 block -1

echo "(A2) bflash_repair_boot con need_rest=0 no escribe ni un byte (y la forma de medirlo no está vacía)"
: > vacio.hex
SEFTOOL_GUION=vacio.hex SEFTOOL_TX=tx_rep.log ./rf repair > rep.log 2>&1; rcr=$?
SEFTOOL_GUION=vacio.hex SEFTOOL_TX=tx_ctl.log ./rf control > ctl.log 2>&1
[ $rcr -eq 0 ] && grep -q "rc=0" rep.log && [ ! -s tx_rep.log ] && ok "need_rest=0: rc 0 y 0 bytes al puerto" || mal "need_rest=0 escribió algo o falló (rc $rcr, $(wc -c < tx_rep.log) bytes)"
[ -s tx_ctl.log ] && ok "control: una escritura sí queda registrada" || mal "el control no registró la escritura: la medida no vale"

cd /; rm -rf "$T"
[ $fallos -eq 0 ] && echo "TODO BIEN" || { echo "$fallos FALLO(S)"; exit 1; }
