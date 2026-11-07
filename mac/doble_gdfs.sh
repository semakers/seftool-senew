#!/bin/sh
# Dos lecturas del GDFS del W580 por USB desde la Mac, cada una tras una
# reconexión NUEVA en modo C, y comparación de sus sha256. SOLO LECTURA: es el
# paso 3 de w580/fisico/CUANDO_LLEGUES_respaldo.md ("hazlo DOS veces y compara").
#
#   mac/doble_gdfs.sh [segundos_de_espera_por_conexión]      (por omisión 900)
#
# Cada acción de seftool apaga el teléfono al terminar, así que entre una lectura
# y la otra hay que volver a entrar en modo C: teléfono APAGADO, tecla C pulsada
# y, sin soltarla, enchufar el cable. El guion espera a que el Mac vea un
# «SEMC USB Flash» NUEVO (otro id de USB) y entonces lee. Si una lectura falla se
# para: no sigue a ciegas.
#
# Deja en backup/gdfs_doble/:  lectura1.bin lectura2.bin  (+ .sha256 y el registro
# de cada sesión con el tráfico USB: lecturaN.log). El volcado definitivo solo
# existe si pasó su propia verificación (seftool lo escribe como .INCOMPLETO hasta
# entonces).
#
# Variables para pruebas (sin teléfono): SEFTOOL (binario) e IOREG (comando).
set -u
R=$(cd "$(dirname "$0")/.." && pwd)
cd "$R" || exit 1
ESPERA=${1:-900}
SEFTOOL=${SEFTOOL:-./seftool}
IOREG=${IOREG:-ioreg}
DIR=backup/gdfs_doble
mkdir -p "$DIR"
[ -x "$SEFTOOL" ] || { echo "no hay $SEFTOOL: compílalo con mac/build.sh"; exit 1; }

id_actual() {
	$IOREG -p IOUSB -w0 2>/dev/null | grep "SEMC USB Flash" | sed 's/.*id \(0x[0-9a-fA-F]*\).*/\1/' | head -1
}

ultimo=""
n=1
while [ $n -le 2 ]; do
	echo "[$(date +%T)] LECTURA $n de 2: apaga el W580, mantén la C y enchufa el cable (espero hasta ${ESPERA} s)"
	t=0
	while :; do
		id=$(id_actual)
		if [ -n "$id" ] && [ "$id" != "$ultimo" ]; then
			break
		fi
		[ -z "$id" ] && ultimo=""
		sleep 1
		t=$((t + 1))
		if [ $t -ge "$ESPERA" ]; then
			echo "[$(date +%T)] no apareció un aparato nuevo en ${ESPERA} s: me paro. No se escribió nada."
			exit 1
		fi
	done
	ultimo=$id
	echo "[$(date +%T)] visto $id: leyendo el GDFS"
	rm -f backup/GDFS_*.bin backup/GDFS_*.bin.INCOMPLETO
	SENEW_USB_TRAZA=1 script -q "$DIR/lectura$n.log" "$SEFTOOL" -p usb -b 921600 -a read-gdfs >/dev/null 2>&1
	rc=$?
	f=$(ls backup/GDFS_*.bin 2>/dev/null | head -1)
	if [ $rc -ne 0 ] || [ -z "$f" ]; then
		echo "[$(date +%T)] la lectura $n NO terminó bien (rc $rc). Último tramo del registro:"
		tr -d '\r' <"$DIR/lectura$n.log" | grep -v '^\[usb' | tail -12
		[ -e backup/GDFS_*.INCOMPLETO ] 2>/dev/null && echo "(hay un .INCOMPLETO en backup/: NO sirve para restaurar)"
		echo "Me paro. El teléfono sigue como estaba (solo se leyó)."
		exit 1
	fi
	mv "$f" "$DIR/lectura$n.bin"
	shasum -a 256 "$DIR/lectura$n.bin" | tee "$DIR/lectura$n.sha256"
	echo "[$(date +%T)] lectura $n correcta: $(wc -c <"$DIR/lectura$n.bin" | tr -d ' ') bytes"
	if [ -f "$HOME/verificar_gdfs.py" ]; then
		python3 -I "$HOME/verificar_gdfs.py" "$DIR/lectura$n.bin" 2>&1 | tail -15
	fi
	n=$((n + 1))
done

a=$(awk '{print $1}' "$DIR/lectura1.sha256")
b=$(awk '{print $1}' "$DIR/lectura2.sha256")
echo
if [ "$a" = "$b" ]; then
	echo "IGUALES: las dos lecturas del GDFS tienen el mismo sha256 ($a). El enlace es fiable."
	exit 0
fi
echo "DIFIEREN: $a  vs  $b"
echo "El enlace no es fiable (o el teléfono cambió entre lecturas). No se sigue; se guardan las dos."
exit 2
