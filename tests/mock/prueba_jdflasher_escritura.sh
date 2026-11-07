#!/bin/sh
# flash-jdflasher: compuertas de ESCRITURA en línea de órdenes, sin aparato.
# Molde: prueba_jdflasher.sh de la LECTURA. Cada compuerta tiene que rechazar
# ANTES de abrir el puerto, no imprimir «Security disabled», y no dejar tx.log.
# La ESCRITURA de verdad la mide el aparato: aquí solo se protege la línea
# de órdenes y las dos envoltorios de entorno (CONFIRM y WRITE).
# Uso: tests/mock/prueba_jdflasher_escritura.sh   (tras tests/mock/build.sh)
R=$(cd "$(dirname "$0")/../.." && pwd)
T=${TMPDIR:-/tmp}/prueba_jdflasher_escritura.$$
mkdir -p "$T/backup"; cd "$T" || exit 1
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
fallos=0
ok()  { echo "  ok    $1"; }
mal() { echo "  MAL   $1"; fallos=$((fallos+1)); }

MOCK=$R/tests/mock/seftool-mock
[ -x "$MOCK" ] || { echo "falta $MOCK: corre tests/mock/build.sh antes"; cd /; rm -rf "$T"; exit 1; }
ln -s "$R/loader" loader; ln -s "$R/anycid" anycid; ln -s "$R/rest" rest

# Un fichero cualquiera: no se abre, porque todas las compuertas rechazan antes del puerto.
: > fw.bin

gate() {   # gate <qué> <patrón en stderr> <argumentos del seftool-mock…>
	que=$1; patron=$2; shift 2
	rm -f tx_g.log out_g.log err_g.log
	SEFTOOL_TX=tx_g.log SEFTOOL_GUION=/dev/null "$MOCK" -p mock -b 921600 "$@" > out_g.log 2> err_g.log; rc=$?
	# Nota: la pantalla de uso de seftool, que se imprime en el rechazo, menciona
	# «DRY-RUN» en la línea de ayuda de flash-jdflasher. Lo que vale para probar
	# que no se tocó el puerto es que tx_g.log NO exista.
	if [ $rc -eq 1 ] && grep -q "$patron" err_g.log && [ ! -e tx_g.log ] \
	   && ! grep -q "Security disabled" out_g.log err_g.log \
	   && ! grep -qE "AddressSanitizer|runtime error" err_g.log; then
		ok "$que"
	else
		mal "$que (rc $rc, tx existe: $([ -e tx_g.log ] && echo sí || echo no); $(head -n 1 err_g.log))"
	fi
}

IMEI_OK=35000000000000

echo "(G) compuertas de flash-jdflasher: cada una rechaza antes de abrir el puerto"

# flash-jdflasher fuerza --jdflasher en parse_args; así que, aunque el usuario
# NO teclee --jdflasher, las compuertas de --jdflasher se le aplican también a
# la escritura.
gate "flash-jdflasher sin --cid: se niega (misma compuerta que --jdflasher)" \
     "solo cablea CID52" \
     -a flash-jdflasher main fw.bin --imei $IMEI_OK

SEFTOOL_JDFLASHER_CONFIRM=1 \
gate "flash-jdflasher --cid 49: se niega (CID49 va por flash-anycid)" \
     "solo cablea CID52" \
     -a flash-jdflasher main fw.bin --cid 49 --imei $IMEI_OK

SEFTOOL_JDFLASHER_CONFIRM=1 \
gate "flash-jdflasher --cid 53: se niega (CID53 queda pendiente)" \
     "solo cablea CID52" \
     -a flash-jdflasher main fw.bin --cid 53 --imei $IMEI_OK

SEFTOOL_JDFLASHER_CONFIRM=1 \
gate "flash-jdflasher sin --imei: se niega" \
     "exige --imei" \
     -a flash-jdflasher main fw.bin --cid 52

: # Vaciado EXPLÍCITO, no basta con "no ponerlo": ver prueba_jdflasher.sh para el porqué
  # (bash 3.2 de macOS no restaura igual que bash 5 un `VAR=1 \` delante de una función).
SEFTOOL_JDFLASHER_CONFIRM= \
gate "flash-jdflasher sin SEFTOOL_JDFLASHER_CONFIRM=1: se niega" \
     "SEFTOOL_JDFLASHER_CONFIRM=1 obligatorio" \
     -a flash-jdflasher main fw.bin --cid 52 --imei $IMEI_OK

SEFTOOL_JDFLASHER_CONFIRM=1 \
gate "flash-jdflasher + --anycid: mutuamente excluyentes" \
     "mutuamente excluyentes" \
     -a flash-jdflasher main fw.bin --anycid --cid 52 --imei $IMEI_OK

SEFTOOL_JDFLASHER_CONFIRM=1 \
gate "flash-jdflasher + --break-rsa: mutuamente excluyentes" \
     "mutuamente excluyentes" \
     -a flash-jdflasher main fw.bin --break-rsa --cid 52 --imei $IMEI_OK

# El parser niega 'cda' explícitamente (molde flash-anycid): aceptarlo callando
# daría la impresión de que se había escrito.
SEFTOOL_JDFLASHER_CONFIRM=1 \
gate "flash-jdflasher cda <f>: rechazado por el parser" \
     "flash-jdflasher does not flash 'cda'" \
     -a flash-jdflasher main fw.bin cda fw.bin --cid 52 --imei $IMEI_OK

# Sin main / fs: el parser pide al menos uno (molde flash-anycid).
SEFTOOL_JDFLASHER_CONFIRM=1 \
gate "flash-jdflasher sin main ni fs: rechazado por el parser" \
     "flash-jdflasher requires at least one" \
     -a flash-jdflasher --cid 52 --imei $IMEI_OK

# El entorno «1» exacto para CONFIRM: cualquier otra cosa falla igual.
for mal_env in "" "0" "yes" "true" "1 "; do
	SEFTOOL_JDFLASHER_CONFIRM="$mal_env" \
	gate "SEFTOOL_JDFLASHER_CONFIRM='$mal_env': no es «1» exacto, se niega" \
	     "SEFTOOL_JDFLASHER_CONFIRM=1 obligatorio" \
	     -a flash-jdflasher main fw.bin --cid 52 --imei $IMEI_OK
done

# Y sin --jdflasher ni flash-jdflasher, el entorno de escritura no tiene efecto:
# que SEFTOOL_JDFLASHER_WRITE=1 esté puesto no debe cambiar nada en una orden
# que no lo pida.
rm -f tx_s.log out_s.log err_s.log
SEFTOOL_TX=tx_s.log SEFTOOL_JDFLASHER_WRITE=1 SEFTOOL_GUION=/dev/null "$MOCK" \
	-p mock -b 921600 -a read-flash start 0x44000000 size 0x4000000 > out_s.log 2> err_s.log; rc=$?
if grep -qiE "jdflasher|JDFLASHER_WRITE" err_s.log out_s.log; then
	mal "sin flash-jdflasher, SEFTOOL_JDFLASHER_WRITE no debe disparar la rama (rc $rc; $(head -n 1 err_s.log))"
else
	ok "sin flash-jdflasher, SEFTOOL_JDFLASHER_WRITE=1 no cambia nada"
fi

# Documento vivo: la ESCRITURA de verdad la prueba el aparato, no el harness.
# Aquí lo único medible sin aparato son las compuertas y el parser; el
# flashloader R2A005 solo se puede probar al otro lado de la línea.
echo "(F) la acción está registrada: 'flash-jdflasher' no es «unknown action»"
rm -f out_f.log err_f.log
SEFTOOL_GUION=/dev/null "$MOCK" -p mock -b 921600 -a flash-jdflasher --cid 52 --imei $IMEI_OK > out_f.log 2> err_f.log
if grep -q "flash-jdflasher requires at least one" err_f.log && ! grep -q "unknown or unsupported action" err_f.log; then
	ok "flash-jdflasher cae en el parser, no en «unknown action»"
else
	mal "flash-jdflasher parece sin registrar: $(head -n 1 err_f.log)"
fi

cd /; rm -rf "$T"
[ $fallos -eq 0 ] && echo "TODO BIEN" || { echo "$fallos FALLO(S)"; exit 1; }
