#!/bin/sh
# --jdflasher: compuertas de LECTURA en línea de órdenes, sin aparato.
# Molde: prueba_readflash.sh pero sin cablear el protocolo del loader (eso lo mide el aparato).
# Comprueba que CADA compuerta rechaza antes de abrir el puerto, no imprime
# «Security disabled», y no deja ningún tx.log. Bajo ASan+UBSan+FORTIFY.
# Uso: tests/mock/prueba_jdflasher.sh   (tras tests/mock/build.sh)
R=$(cd "$(dirname "$0")/../.." && pwd)
T=${TMPDIR:-/tmp}/prueba_jdflasher.$$
mkdir -p "$T/backup"; cd "$T" || exit 1
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
fallos=0
ok()  { echo "  ok    $1"; }
mal() { echo "  MAL   $1"; fallos=$((fallos+1)); }

MOCK=$R/tests/mock/seftool-mock
[ -x "$MOCK" ] || { echo "falta $MOCK: corre tests/mock/build.sh antes"; cd /; rm -rf "$T"; exit 1; }
ln -s "$R/loader" loader; ln -s "$R/anycid" anycid; ln -s "$R/rest" rest

: > vacio.hex   # el guion no se abre: todas las compuertas rechazan antes del puerto

# gate <qué> <patrón en stderr> <argumentos del seftool-mock...>
gate() {
	que=$1; patron=$2; shift 2
	rm -f tx_g.log out_g.log err_g.log
	SEFTOOL_TX=tx_g.log SEFTOOL_GUION=vacio.hex "$MOCK" -p mock -b 921600 "$@" > out_g.log 2> err_g.log; rc=$?
	if [ $rc -eq 1 ] && grep -q "$patron" err_g.log && [ ! -e tx_g.log ] \
	   && ! grep -q "Security disabled" out_g.log err_g.log \
	   && ! grep -qE "AddressSanitizer|runtime error" err_g.log; then
		ok "$que"
	else
		mal "$que (rc $rc, tx existe: $([ -e tx_g.log ] && echo sí || echo no); $(head -n 1 err_g.log))"
	fi
}

ARGS_RF="-a read-flash start 0x44000000 size 0x4000000"
IMEI_OK=35000000000000

echo "(G) compuertas de --jdflasher: cada una rechaza antes de abrir el puerto"

gate "--jdflasher + --anycid: mutuamente excluyentes" \
     "mutuamente excluyentes" \
     $ARGS_RF --jdflasher --anycid --cid 52 --imei $IMEI_OK

SEFTOOL_JDFLASHER_CONFIRM=1 \
gate "--jdflasher + --break-rsa: mutuamente excluyentes (--break-rsa es DB2010)" \
     "mutuamente excluyentes" \
     $ARGS_RF --jdflasher --break-rsa --cid 52 --imei $IMEI_OK

SEFTOOL_JDFLASHER_CONFIRM=1 \
gate "--jdflasher sin --cid: se niega" \
     "solo cablea CID52" \
     $ARGS_RF --jdflasher --imei $IMEI_OK

SEFTOOL_JDFLASHER_CONFIRM=1 \
gate "--jdflasher --cid 49: se niega (CID49 va por --anycid)" \
     "solo cablea CID52" \
     $ARGS_RF --jdflasher --cid 49 --imei $IMEI_OK

SEFTOOL_JDFLASHER_CONFIRM=1 \
gate "--jdflasher --cid 53: se niega (CID53 queda pendiente)" \
     "solo cablea CID52" \
     $ARGS_RF --jdflasher --cid 53 --imei $IMEI_OK

SEFTOOL_JDFLASHER_CONFIRM=1 \
gate "--jdflasher sin --imei: se niega (jdflasher no carga P3M)" \
     "exige --imei" \
     $ARGS_RF --jdflasher --cid 52

: # SEFTOOL_JDFLASHER_CONFIRM se limpia EXPLÍCITO (no basta con "no ponerlo"): un
  # `VAR=1 \` delante de una llamada a función de shell no se restaura igual en
  # todos los `sh` (medido: macOS/bash 3.2 lo deja filtrado al resto del guion
  # tras la llamada de arriba con --imei; Fedora/bash 5 si lo restaura). Sin este
  # vaciado explícito la prueba pasa en la Asahi y falla en la Mac.
SEFTOOL_JDFLASHER_CONFIRM= \
gate "--jdflasher sin SEFTOOL_JDFLASHER_CONFIRM=1: se niega" \
     "SEFTOOL_JDFLASHER_CONFIRM=1 obligatorio" \
     $ARGS_RF --jdflasher --cid 52 --imei $IMEI_OK

# El entorno «1» exacto: cualquier otra cosa falla igual (molde SEFTOOL_BREAK_CONFIRM).
for mal_env in "" "0" "yes" "true" "1 "; do
	SEFTOOL_JDFLASHER_CONFIRM="$mal_env" \
	gate "SEFTOOL_JDFLASHER_CONFIRM='$mal_env': no es «1» exacto, se niega" \
	     "SEFTOOL_JDFLASHER_CONFIRM=1 obligatorio" \
	     $ARGS_RF --jdflasher --cid 52 --imei $IMEI_OK
done

# Si no pasa --jdflasher, el entorno no tiene efecto: por eso una ausencia en el entorno no
# rompe las órdenes que no la piden. Aquí ya se abre el puerto (el guion está vacío, así que
# el seftool-mock termina con rc 77 «fin del guion»), lo importante es que NINGUNA compuerta
# de --jdflasher se queje.
rm -f tx_s.log out_s.log err_s.log
SEFTOOL_TX=tx_s.log SEFTOOL_JDFLASHER_CONFIRM=1 SEFTOOL_GUION=vacio.hex "$MOCK" \
	-p mock -b 921600 $ARGS_RF > out_s.log 2> err_s.log; rc=$?
if grep -qiE "jdflasher|JDFLASHER_CONFIRM" err_s.log out_s.log; then
	mal "sin --jdflasher, el env no debe disparar la rama (rc $rc; $(head -n 1 err_s.log))"
else
	ok "sin --jdflasher, SEFTOOL_JDFLASHER_CONFIRM=1 no cambia nada"
fi

# El prologue tiene que estar en loader/ con el sha256 esperado.
echo "(F) el fichero 2020_prologue está en loader/ con el sha256 esperado"
sha=$(sha256sum "$R/loader/2020_prologue" 2>/dev/null | cut -d' ' -f1)
esperado=e78a17a1c6ac22672e23a3388905b7076b5fbd5f41db84876fbb89cb418f085e
if [ "$sha" = "$esperado" ]; then
	ok "loader/2020_prologue sha256 == $esperado"
else
	mal "loader/2020_prologue sha256 '$sha' != '$esperado'"
fi

cd /; rm -rf "$T"
[ $fallos -eq 0 ] && echo "TODO BIEN" || { echo "$fallos FALLO(S)"; exit 1; }
