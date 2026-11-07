#!/bin/sh
# Lo que prueba_readflash.sh no ve y una prueba de MUTACIÓN delató (2026-10-17). Sin teléfono, sin simular ningún loader.
#   (X) escritor atómico: disco que miente, apartado que falla, reintento, rollback, orden de fsync (--wrap de GNU ld)
#   (S) ESTRUCTURAL: el orden del camino --anycid y la promesa de «solo lectura». No se pueden probar sin simular un
#       loader (regla dura), así que se VIGILAN en el fuente: es frágil ante refactors, pero avisa.
#   (F) las dos veces que flash-anycid mira SEFTOOL_SENEW_FORCE, y el parser por su lado de ACEPTAR
# Uso: tests/mock/prueba_readflash_x.sh   (tras tests/mock/build.sh; solo Linux: --wrap es de GNU ld)
case "$(uname -s)" in Darwin) echo "OMITIDO en macOS: -Wl,--wrap es de GNU ld"; exit 0;; esac
R=$(cd "$(dirname "$0")/../.." && pwd)
T=${TMPDIR:-/tmp}/prueba_extra.$$
mkdir -p "$T/backup"; cd "$T" || exit 1
SRC=$(find "$R/src" -name '*.c' ! -name main.c)
INC="-I$R/mac -I$R/src -I$R/src/core -I$R/src/utils -I$R/src/emp/common -I$R/src/eric/common -I$R/src/senew"
SAN="-std=gnu99 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_FORTIFY_SOURCE=3 -Wall -Wno-missing-braces -Wno-unused-parameter"
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
fallos=0
ok()  { echo "  ok    $1"; }
mal() { echo "  MAL   $1"; fallos=$((fallos+1)); }

# shellcheck disable=SC2086
gcc $SAN $INC -Wl,--wrap=rename -Wl,--wrap=fsync "${EXTRA_C:-$R/tests/mock/prueba_readflash_x.c}" $SRC "$R/tests/mock/sp_mock.c" \
  -lminizip -lz -lbz2 -o rfx 2> cc.log || { echo "no compila el harness:"; cat cc.log; exit 1; }
grep -q "warning" cc.log && { echo "avisos del compilador:"; cat cc.log; fallos=$((fallos+1)); }
echo "(X) escritor atómico, con el disco y rename/fsync bajo control"
./rfx > x.log 2>&1; rc=$?
cat x.log
[ $rc -eq 0 ] && ok "el harness termina sin fallos" || mal "el harness falló (rc $rc)"

# ---------------------------------------------------------------- (S) estructura del fuente
cuerpo() {   # cuerpo <archivo> <función>: desde su firma (columna 0) hasta el primer «}» en columna 0
	awk -v f="$2(" 'index($0, f) && $0 ~ /^[A-Za-z]/ && !p {p=1} p {print} p && /^}/ {exit}' "$1"
}
linea() { # linea <cuerpo> <patrón> [desde_linea]: primera aparición >= desde_linea (default 1)
	d=${3:-1}
	printf '%s\n' "$1" | awk -v d="$d" -v pat="$2" 'NR>=d && index($0, pat) { print NR; exit }'
}
orden() {    # orden <cuerpo> <patrón>...: todos aparecen, en ESE orden estricto, en apariciones distintas
	cu=$1; shift; prev=0
	for p in "$@"; do
		n=$(linea "$cu" "$p" $((prev + 1))); { [ -n "$n" ] && [ "$n" -gt "$prev" ]; } || { echo "    «$p» no está o va antes de lo debido"; return 1; }
		prev=$n
	done
}
echo "(S) estructura: el orden del camino read-flash --anycid (no hay otra forma de vigilarlo sin un loader simulado)"
AC=$R/src/emp/v3/action.c; BF=$R/src/emp/v3/bflash.c
RF=$(cuerpo "$AC" action_read_flash)
[ -n "$RF" ] || mal "no se encontró action_read_flash"
# Orden del camino jdflasher (primero en el fuente) y DESPUES del camino anycid; ambos
# comparten molde: gate -> flush -> loader del break -> LEER -> (solo si need_rest) reparar.
# jdflasher usa loader_send_prologue_jdflasher(2020_prologue) + connection_restart()
# (reconexión automática tras el reset real que mide 2026-10-18) + loader_send_qhldr_jdflasher(R2A005):
# CRUDO (QH00/QA00/QD00), NO loader_send_binary — ese empaquetaría cmd 0x3C y daría el `-2 of 902`
# del 2026-10-18 (ver docs/CID52_JDFLASHER.md «Causa del -2 of 902»);
# anycid usa loader_send_setool2_db2020. Las dos ramas llaman bflash_repair_boot: por eso
# ahora salen DOS veces, una por rama.
orden "$RF" 'phone->chip_id == DB2020 && phone->jdflasher == 1' 'action_read_flash_gate(' 'sp_flush(port, SP_BUF_BOTH)' \
      'loader_send_prologue_jdflasher(' 'connection_restart(' 'loader_send_qhldr_jdflasher(' 'flash_read(port, phone, addr, size)' 'if (phone->need_rest)' 'bflash_repair_boot(' \
      'phone->chip_id == DB2020 && phone->anycid == 1' 'action_read_flash_gate(' 'sp_flush(port, SP_BUF_BOTH)' \
      'loader_send_setool2_db2020(' 'flash_read(port, phone, addr, size)' 'if (phone->need_rest)' 'bflash_repair_boot(' \
      'loader_send_bflash_ldr(' \
  && ok "ramas jdflasher y anycid en orden; cada una: gate -> flush -> loader -> (jdflasher: +reconexión) -> LEER -> (si need_rest) reparar" \
  || mal "el orden del camino --jdflasher/--anycid cambió"
[ "$(printf '%s\n' "$RF" | grep -c 'bflash_repair_boot(')" = 2 ] && ok "bflash_repair_boot sale DOS veces en action_read_flash (jdflasher + anycid)" || mal "bflash_repair_boot ya no sale dos veces (jdflasher + anycid)"
# Las DOS compuertas que rechazan hacen return -1 inmediatamente despues (una por rama).
nret=$(printf '%s\n' "$RF" | grep -A1 'action_read_flash_gate(' | grep -c 'return -1')
[ "$nret" = 2 ] && ok "las dos compuertas (jdflasher y anycid) rechazan con return -1" || mal "una de las compuertas ya no corta (encontradas $nret 'return -1' tras gate)"
LD=$(cuerpo "$BF" loader_send_bflash_ldr_db2020)
orden "$LD" 'loader_send_setool2_db2020(' 'bflash_repair_boot(' && ok "flash-anycid: tras el loader de setool2 SIGUE reparando el arranque" || mal "flash-anycid perdió la reparación del arranque"
S2=$(cuerpo "$BF" loader_send_setool2_db2020)
printf '%s\n' "$S2" | grep -q 'Security disabled =)' && ok "loader_send_setool2_db2020 sigue imprimiendo «Security disabled =)»" || mal "falta «Security disabled =)»"
# la promesa de «solo lectura»: read-flash solo repara si el humano pasó --restore-boot (src/action.c, case ACT_READ_FLASH)
awk '/case ACT_READ_FLASH:/ {p=1} p {print} p && /break;/ {exit}' "$R/src/action.c" | grep -qF 'phone->need_rest = restore_boot_pedido(args);' \
  && ok "ACT_READ_FLASH: need_rest sale SOLO de --restore-boot" || mal "ACT_READ_FLASH: need_rest ya no sale solo de --restore-boot"
# flash_read (lo que no se puede ejercitar sin un loader): pide lo planeado y el commit espera exactamente `size`
FR=$(cuerpo "$R/src/emp/v3/flash.c" flash_read)
printf '%s\n' "$FR" | grep -qF 'flash_read_raw(port, chunk_addr, chunk)' && ok "flash_read lee la dirección del plan (chunk_addr)" || mal "flash_read ya no lee chunk_addr tal cual"
printf '%s\n' "$FR" | grep -qF 'flash_dump_commit(dump, size, sha1_hex)' && ok "flash_read publica esperando exactamente «size» bytes" || mal "flash_read ya no pasa «size» al commit"
# la línea que imprime el SHA-1 dice el SHA-1 de verdad (la prueba (X) la deja en x.log)
linea_sha=$(grep 'Flash dump saved ./backup/fsync1.bin' x.log | tail -n 1 | sed 's/.*SHA-1 \([0-9a-f]*\)).*/\1/')
real_sha=$(sha1sum backup/fsync1.bin | cut -d' ' -f1)
[ -n "$linea_sha" ] && [ "$linea_sha" = "$real_sha" ] && ok "«Flash dump saved … SHA-1 X»: X == sha1sum del archivo" || mal "el SHA-1 impreso '$linea_sha' != sha1sum '$real_sha'"

# restore_boot_pedido: lo único que decide si read-flash/write-script/fsx-download escriben el .rest
RB=$(cuerpo "$R/src/action.c" restore_boot_pedido)
printf '%s\n' "$RB" | grep -qF 'return args->restore_boot;' && ok "restore_boot_pedido devuelve solo --restore-boot" || mal "restore_boot_pedido ya no devuelve solo args->restore_boot"
# las confirmaciones a sabiendas miran el valor EXACTO «1» (senew_env_one), no getenv()!=NULL
AF=$(cuerpo "$AC" action_flash_anycid)
[ "$(printf '%s\n' "$AF" | grep -c 'senew_force_requested()')" = 2 ] && ! printf '%s\n' "$AF" | grep -q 'getenv(' \
  && ok "action_flash_anycid mira SEFTOOL_SENEW_FORCE dos veces, las dos con senew_force_requested()" \
  || mal "action_flash_anycid ya no usa senew_force_requested() en sus dos sitios (o vuelve a haber getenv)"
grep -qF 'senew_env_one("SEFTOOL_BREAK_CONFIRM")' "$R/src/emp/v3/break.c" && ! grep -q 'getenv("SEFTOOL_BREAK_CONFIRM")' "$R/src/emp/v3/break.c" \
  && ok "break.c: SEFTOOL_BREAK_CONFIRM solo vale «1»" || mal "break.c vuelve a aceptar cualquier valor de SEFTOOL_BREAK_CONFIRM"
# --imei: la comprobación vive en la activación de CUALQUIER loader (después de leer el OTP, antes de seguir)
LA=$(cuerpo "$R/src/emp/v3/loader.c" loader_activate_payload)
orden "$LA" 'loader_get_otp_info(' 'return loader_check_expected_imei(phone);' \
  && ok "loader_activate_payload: tras leer el OTP devuelve la comprobación del IMEI" || mal "loader_activate_payload ya no comprueba --imei tras leer el OTP"

# --imei: la negativa deja su marca y las pistas del executer no la disfrazan (no se puede ejercitar el camino
# anycid de punta a punta sin la charla real del preloader de setool2, que nunca se ha visto en un cable)
CE=$(cuerpo "$R/src/emp/v3/loader.c" loader_check_expected_imei)
orden "$CE" 'phone->imei_refused = 1;' 'return -1;' \
  && ok "loader_check_expected_imei: marca imei_refused justo antes de negarse" || mal "loader_check_expected_imei ya no marca imei_refused al negarse"
orden "$RF" 'loader_send_setool2_db2020(' 'if (phone->imei_refused)' 'Causas probables' \
  && ok "read-flash --anycid: tras la negativa de --imei sale SIN la lista de causas del executer (la última línea sigue siendo imei-check)" \
  || mal "read-flash --anycid: la lista de causas del executer puede salir tras la negativa de --imei"
orden "$AF" 'loader_send_bflash_ldr(' 'if (phone->imei_refused)' 'Causas probables' \
  && ok "flash-anycid: igual (la negativa de --imei no sale con la lista de causas del executer)" \
  || mal "flash-anycid: la lista de causas del executer puede salir tras la negativa de --imei"
! grep -q 'printf("\[QHTRY\] Run executer first' "$BF" && [ "$(grep -c 'loader_hint_run_executer(phone)' "$BF")" = 3 ] \
  && ok "bflash.c: los 3 avisos «Run executer first» pasan por loader_hint_run_executer" \
  || mal "bflash.c vuelve a imprimir «Run executer first» sin mirar imei_refused"

# ---------------------------------------------------------------- (F) contra el mock
MOCK=$R/tests/mock/seftool-mock
[ -x "$MOCK" ] || { echo "falta $MOCK: corre tests/mock/build.sh"; cd /; rm -rf "$T"; exit 1; }
ln -s "$R/loader" loader; ln -s "$R/anycid" anycid; ln -s "$R/rest" rest
python3 "$R/tests/mock/guion.py" real | head -10 > real10.hex
python3 "$R/tests/mock/guion.py" real | head -3 > hs3.hex
MAIN=${MAIN:?MAIN = un main .mbn de firmware (aportado por ti)}

echo "(F) flash-anycid: SEFTOOL_SENEW_FORCE=0 NO fuerza (compuerta y comprobación previa), rechaza antes de mandar un loader"
SEFTOOL_SENEW_FORCE=0 SEFTOOL_TX=tx_f1.log SEFTOOL_GUION=real10.hex "$MOCK" -p mock -b 921600 -a flash-anycid main "$MAIN" --cid 53 > o_f1.log 2> e_f1.log; rc=$?
if [ $rc -eq 255 ] && grep -q "no da cobertura" e_f1.log && ! grep -q '^51483030' tx_f1.log; then ok "FORCE=0 y --cid 53: la tabla lo niega (rc 255, sin loader)"; else mal "FORCE=0 y --cid 53 (rc $rc; $(head -n 2 e_f1.log | tr '\n' '|'))"; fi
SEFTOOL_SENEW_FORCE=0 SEFTOOL_TX=tx_f2.log SEFTOOL_GUION=real10.hex "$MOCK" -p mock -b 921600 -a flash-anycid main "$MAIN" --cid 51 > o_f2.log 2> e_f2.log; rc=$?
if [ $rc -eq 255 ] && grep -q "no pasa la comprobación" e_f2.log && ! grep -q '^51483030' tx_f2.log; then ok "FORCE=0 y la imagen RED52 con --cid 51: la comprobación previa la niega (rc 255, sin loader)"; else mal "FORCE=0 y --cid 51 (rc $rc; $(head -n 2 e_f2.log | tr '\n' '|'))"; fi

echo "(P) el parser, del lado de ACEPTAR: block se multiplica por 64 KiB; «+», espacio inicial y «-0» no son números de 32 bits"
SEFTOOL_TX=tx_p1.log SEFTOOL_GUION=hs3.hex "$MOCK" -p mock -b 921600 -a read-flash start 0x47FF0000 block 2 --anycid --cid 52 > o_p1.log 2> e_p1.log; rc=$?
if [ $rc -eq 255 ] && grep -q "0x47FF0000 + 0x20000 " e_p1.log; then ok "block 2 = 0x20000 bytes (la compuerta lo muestra y lo rechaza por pasarse del final)"; else mal "block 2 (rc $rc; $(head -n 2 e_p1.log | tr '\n' '|'))"; fi
for v in "+4096" " 4096" "-0"; do
	rm -f tx_p2.log
	SEFTOOL_TX=tx_p2.log SEFTOOL_GUION=hs3.hex "$MOCK" -p mock -b 921600 -a read-flash start 0x44000000 size "$v" > o_p2.log 2> e_p2.log; rc=$?
	if [ $rc -eq 1 ] && grep -q "invalid size" e_p2.log && [ ! -e tx_p2.log ]; then ok "size «$v»: rechazado por el parser"; else mal "size «$v» (rc $rc; $(head -n 1 e_p2.log))"; fi
done

cd /; rm -rf "$T"
[ $fallos -eq 0 ] && echo "TODO BIEN" || { echo "$fallos FALLO(S)"; exit 1; }
