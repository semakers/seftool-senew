#!/bin/sh
# Lecturas del GDFS (SOLO LECTURA) con reintentos automaticos y salida en vivo.
#   IMEI=<imei> DIR=<carpeta> BUENAS=<ya hechas> sh gdfs_reintentos.sh [max_intentos] [lecturas_a_reunir]
# Cada conexion NUEVA en modo C (aparato nuevo en el bus) se lee AL INSTANTE.
# El exito es que exista backup/GDFS_*.bin (seftool solo lo crea tras verificar);
# el rc puede ser !=0 por el apagado del servidor GDFS DESPUES de guardarlo.
# NUNCA se borra un volcado: lo previo se aparta con marca de hora.
set -u
cd "$HOME/dev/seftool-senew" || exit 1
MAX=${1:-12}
QUERIDAS=${2:-2}
DIR=${DIR:-backup/gdfs_reint}
IMEI=${IMEI:-}
buenas=${BUENAS:-0}
mkdir -p "$DIR"
SEFTOOL=./seftool
intento=0
visto_ultimo=""

id_actual() {
	ioreg -p IOUSB -w0 2>/dev/null | grep "SEMC USB Flash" | sed 's/.*id \(0x[0-9a-fA-F]*\).*/\1/' | head -1
}
hora() { date +%T; }

while [ "$buenas" -lt "$QUERIDAS" ] && [ "$intento" -lt "$MAX" ]; do
	echo "[$(hora)] === esperando conexion NUEVA en modo C (buenas=$buenas/$QUERIDAS, intento=$intento/$MAX) ==="
	while [ -n "$(id_actual)" ] && [ "$(id_actual)" = "$visto_ultimo" ]; do sleep 1; done
	t=0
	while :; do
		id=$(id_actual)
		[ -n "$id" ] && [ "$id" != "$visto_ultimo" ] && break
		sleep 1; t=$((t+1))
		[ $t -ge 1800 ] && { echo "[$(hora)] 30 min sin aparato: me paro"; exit 1; }
	done
	visto_ultimo=$id
	intento=$((intento+1))
	echo "[$(hora)] aparato nuevo $id -> intento $intento: leo YA${IMEI:+ (solo si el IMEI es $IMEI)}"
	for x in backup/GDFS_*.bin backup/GDFS_*.bin.INCOMPLETO; do
		[ -e "$x" ] && mv "$x" "$DIR/apartado_$(date +%H%M%S)_$(basename "$x")"
	done
	LOG="$DIR/intento_$(date +%H%M%S).log"
	# salida en vivo: lo que dice seftool, sin el volcado hexadecimal del cable USB
	SENEW_USB_TRAZA=1 script -q "$LOG" "$SEFTOOL" -p usb -b 921600 -a read-gdfs ${IMEI:+--imei $IMEI} 2>&1 \
		| tr -d '\r' | grep -a --line-buffered -v '^\[usb' | grep -a --line-buffered -v '^$' | sed -u 's/^/    | /'
	f=$(ls backup/GDFS_*.bin 2>/dev/null | head -1)
	if [ -n "$f" ]; then
		buenas=$((buenas+1))
		dst="$DIR/lectura_$(date +%H%M%S).bin"
		mv "$f" "$dst"
		( cd "$DIR" && shasum -a 256 "$(basename "$dst")" | tee "$(basename "$dst").sha256" )
		echo "[$(hora)] >>> LECTURA BUENA $buenas/$QUERIDAS: $(wc -c <"$dst" | tr -d ' ') bytes ($dst)"
	else
		echo "[$(hora)] --- intento $intento sin volcado (ver lineas de arriba)"
	fi
done
echo
echo "FIN: $buenas lectura(s) buenas, $intento intento(s) nuevos"
ls -la "$DIR"/lectura_*.bin 2>/dev/null
