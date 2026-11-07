# Auditoría de seftool-senew — 2026-10-16

Hecha **sin tocar el teléfono** salvo un `identify` de solo lectura al W580i
bueno (CID 52 RED; su IMEI no se copia a ningún documento ni prueba de este repositorio) desde la Mac. Qué se encontró, qué se
corrigió, qué quedó decidido a propósito y cómo repetir las pruebas.

## Por qué

Al compilar seftool en la Mac (clang, que activa `_FORTIFY_SOURCE`) salieron dos
desbordamientos que en Linux pasaban en silencio. Eso destapó una clase de
defectos del seftool original —tamaños fijos de búfer frente a lo que manda el
teléfono, `long` de 8 bytes donde se esperaba 4, `wcstombs` con `wchar_t` de
4 bytes— que en Windows no se notaban.

## Lo más grave

| Defecto | Efecto |
|---|---|
| `flash_babe` contaba bloques con `long` (8 bytes en 64 bits) | **0 de 460 bloques**; terminaba con «0 blocks flashed ok». `flash-anycid` no habría escrito nada. Ahora 460/460 (main) y 209/209 (fs), medido con los firmwares reales. |
| `csloader_read_gdfs_var`: `cmd_buf[8]` para un paquete de 9 bytes | Desbordamiento de pila en **cada** lectura de variable GDFS del CS loader: `read-gdfs` y `upload-anycid` abortaban en macOS. Lo vieron los revisores independientes y el banco (ASan lo reproduce con la versión anterior). |
| `gdfs_read_var`: `strncpy` de 33 bytes a `char[32]` | Aborto en macOS al leer el operador (`identify`). |
| `loader_send_encoded_cmd_and_data` y `flash_babe`: búfer de 0x800 para paquetes de 0x805 | Desbordamiento al subir loaders y al flashear. |
| `babe_check` rápido no avanzaba de bloque | Un `.mbn` truncado pasaba como bueno. |
| `csloader_write_gdfs` ignoraba el estado de error del teléfono (`cmd != 4 && estado != 0`) | Una restauración «completa» con variables rechazadas. |
| `flash_convert_raw_to_babe` con `malloc` | La cabecera BABE de la imagen de senew (la que SENUS manda a `flash-anycid`) llevaba basura del montón. |

Lista completa de commits: `git log 97fbae2..HEAD` (rama `mac-usb`).

## Cambios de comportamiento deliberados

Todos son **más estrictos o más explícitos**; ninguno cambia lo que se manda
al teléfono con entradas válidas, salvo donde se dice.

* **`flash-anycid` exige `--cid N`** (el «ACTIVE CID» que imprimió `identify`).
  En un DB2020 recién conectado el CID activo no se conoce (el EROM de DB2020
  no lo informa; se lee tras cargar un loader, y `identify` apaga el teléfono),
  así que lo da quien llama. Se contrasta con la tabla de senew: si la celda
  (W580, CID) no es «image available» (CID 53, sin verificar) no escribe.
* **`flash-anycid` comprueba main y fs ENTEROS antes de mandar nada**: SHA-1 de
  cada bloque si la imagen es de Sony (si no hay hashes/firma que comprobar,
  como en una imagen de `raw2babe`, solo la tabla de bloques, con aviso); tabla
  completa; ventana de NOR **0x44140000..0x48000000** (por debajo están el
  arranque, el CID y las unidades de seguridad); direcciones alineadas a 64 KiB;
  plataforma DB2020; CID de la cabecera = `--cid`. Rechaza `cda`.
  `SEFTOOL_SENEW_FORCE=1` salta esas guardas a propósito (no la integridad).
* **`--restore-boot`**: `read-flash`, `write-script` y `fsx-download` con
  exploit terminan escribiendo `./rest/*.rest` en la NOR (0x44140000). Con el
  bug de `long` esa escritura era un no-op; al corregirlo habría pasado a
  ejecutarse en acciones de lectura. Ahora solo con `--restore-boot`.
* **`write-gdfs` valida el volcado entero antes de subir ningún loader** (cuenta,
  límites) y trata cualquier estado de error del teléfono como fallo.
  **Unidades grandes**: los GDFS reales traen unidades de hasta 5950 bytes (el
  emulador del W580 tiene 6 por encima de 0x600) y upstream las **recortaba en
  silencio** a 0x600 diciendo que todo fue bien. Aquí, por omisión, se niega y
  explica las dos salidas: `--gdfs-max N` (órdenes de un solo paquete hasta N
  bytes; **nunca probado en un teléfono**) o `--skip-big` (restaura el resto y
  termina con error: restauración PARCIAL declarada).
* **`read-gdfs` es atómico**: escribe `<nombre>.INCOMPLETO`, lo relee con el
  mismo analizador que usará `write-gdfs` (`fsync`) y solo entonces lo renombra al
  nombre definitivo. Una lectura que falla o se interrumpe **no destruye** un
  respaldo bueno anterior; si ya había uno, pasa a `<nombre>.anterior`. 0
  variables no es un respaldo. `identify` hace lo mismo con las 5 unidades de
  seguridad (`.part`, estado de cada respuesta comprobado, se reintenta si falta).
* **`--break-rsa`** (DB2010 CID49: W600/W810): con el nombre del modelo ya bien
  decodificado elige boot/ose por nombre y **escribe de verdad** un bootloader
  (antes abortaba sin escribir porque el nombre salía vacío). Muestra lo elegido
  y exige `SEFTOOL_BREAK_CONFIRM=1`. La primera vez, en el W810 sacrificable;
  nunca en el W600.
* **`upload-anycid`** verifica los dos zips (CRC) y que existan **antes** de
  abrir el servidor de archivos del teléfono; antes, sin paquete, «terminaba con
  éxito» sin subir nada.
* **Loaders**: la cabecera BABE que promete más bytes de los que tiene el archivo
  se rellena con ceros si faltan ≤ 4096 (hay loaders reales a los que faltan 1 y
  49) o se rechaza; antes se mandaba memoria del montón.

## Capa USB de la Mac (`mac/sp_usb.c`)

macOS no crea puerto para el «SEMC USB Flash» (0fce:adde, clase 255):
`-p usb` habla por libusb. Elige la configuración USB (macOS la deja sin
configurar), lee de a un paquete (con un búfer grande, un mensaje de 512·k bytes
no terminaba hasta agotar el plazo), reintenta al reabrir tras un reinicio, y una
escritura parcial o con plazo vencido es **error** (antes, un envío parcial
pasaba por bueno). Con **dos** aparatos 0fce:adde enchufados se niega y los lista
(antes abría uno al azar). No usa `libusb_set_auto_detach_kernel_driver` (con root
hace un reset USB del teléfono). `sp_flush` solo vacía el búfer propio: vaciar también el FIFO
del teléfono se probó y se descartó, porque tras mandar la `R` que arranca el
loader el código hace `sp_flush` antes de esperar el saludo, y un vaciado con
plazo se lo podría comer.

## Revisión independiente (Sonnet 5.5, esfuerzo máximo)

Siete lentes sobre el diff (bytes enviados, escritura a la NOR, memoria,
portabilidad, capa USB, red de seguridad/restauración, SENUS) más una prueba
diferencial: **4 004 parejas de ejecuciones** (traza real + 1 000 fuzz × 4
acciones) entre la línea base y el código, **0 diferencias sin explicar**; en la
Mac la versión original abortaba en 64 casos de `identify`, incluida la traza
real. Hallazgos que no estaban en la auditoría propia y se corrigieron:

| Hallazgo | Qué se hizo |
|---|---|
| `csloader_read_gdfs_var` `cmd_buf[8]` para 9 bytes: `read-gdfs` y `upload-anycid` abortaban en macOS (lo vieron 4 lentes) | `cmd_buf[16]` |
| `write-gdfs` recortaba unidades > 0x600 y daba la restauración por buena | política explícita (`--gdfs-max`, `--skip-big`) |
| `read-gdfs` pisaba con su nombre definitivo el respaldo bueno si la segunda lectura fallaba | `.INCOMPLETO` → verificación → renombrado atómico |
| `read-flash` & co. con exploit escribían el `.rest` en la NOR tras corregir `long` | `--restore-boot` |
| `--break-rsa` pasa a escribir de verdad con el nombre bien decodificado | confirmación explícita |
| dos aparatos en modo C: abría uno al azar | se niega |
| `identify`: unidades de seguridad a medias dadas por buenas, estado de error guardado como unidad vacía | `.part` + estado |
| `./tmp` se borraba recursivamente; rutas relativas al directorio actual | carpeta propia `.seftool_tmp`; exige `./loader` |

**Quedan fuera de seftool y se documentan, no se arreglan aquí (SENUS):** el
Respaldo de SENUS lanza `identify` + 2×`read-gdfs` + `read-flash` seguidos sin
pedir reconectar en modo C (cada acción apaga el teléfono), y exige `read-flash`,
que en un DB2020 RED no funciona sin `--anycid` (que a su vez exige
`upload-anycid`, que **escribe**). Para el primer contacto se usa el CLI, como
dice `w580/fisico/CUANDO_LLEGUES_respaldo.md`.

*Actualización 2026-10-17:* `read-flash --anycid` ya existe como acción de primera clase (compuertas, volcado
atómico, lectura antes de reparar) y SENUS lo encadena en el respaldo (4 pausas guiadas). Ver
[`read_flash_anycid.md`](read_flash_anycid.md).

## Pruebas (`tests/mock/`)

Un teléfono de guion (`sp_mock.c`) sustituye a libserialport; seftool se compila
con ASan + UBSan + FORTIFY (en la Mac, clang). `guion.py` reproduce **byte a
byte** el `identify` real del W580 del 2026-10-16 y, **construida leyendo el
código (no es una captura)**, una sesión de CS loader de **solo lectura** para
recorrer `read-gdfs` y `upload-anycid`. No hay ningún simulador del loader de
escritura a flash ni de escritura de GDFS/archivos.

```
tests/mock/build.sh                      # Linux (gcc)       · build_mac.sh en la Mac
tests/mock/prueba_gdfs.sh                # read-gdfs, autoverificación, write-gdfs rechaza malos
tests/mock/prueba_script.sh              # parser de scripts GDFS con líneas hostiles
tests/mock/prueba_readflash.sh           # read-flash --anycid: compuertas, volcado atómico, plan de lectura (2026-10-17)
tests/mock/prueba_secunits.sh            # copia atómica de las 5 unidades de seguridad
tests/mock/prueba_precheck.sh [main fs]  # comprobación previa con firmware real
tools/check_loaders.py                   # nombres de loaders de payload.h vs ./loader
SALIDA=/tmp/f FUZZ_MODO=cs tests/mock/fuzz.sh read-gdfs 1 3000
tests/mock/fuzz_dump.sh 1 3000           # write-gdfs con volcados mutados
```

**No editar un script de fuzz mientras corre** (sh lo lee sobre la marcha); lanzar
una copia.

## Primer contacto con el W580 desde la Mac (solo lectura)

Sigue `w580/fisico/CUANDO_LLEGUES_respaldo.md`: **no** se usan `read-flash`,
`fsx-download`, `--anycid` ni `upload-anycid` (en un RED no son de solo lectura).
Teléfono APAGADO, batería ≥ 50 %, tecla **C** pulsada y, sin soltarla, enchufar el
cable (directo al Mac, no por un hub). Cada acción apaga el teléfono al terminar:
hay que volver a entrar en modo C antes de la siguiente. Si entra como «Memory
Stick» (0fce:e089) no es modo servicio; repetir.

```sh
cd seftool-senew
sh mac/build.sh                       # una vez; deja ./seftool
./seftool -p usb -b 921600 -a identify
mac/doble_gdfs.sh                     # dos read-gdfs con reconexión entre ambos + sha256
```

`SENEW_USB_TRAZA=1` vuelca el tráfico USB (la primera sesión real con la capa
nueva: mirar «paquete N» en la línea `[usb] 0fce:adde …`). Los volcados llevan el
IMEI en el nombre y la identidad del aparato: no salen de la máquina.

## Qué NO está verificado

* `upload-anycid` y `flash-anycid` **nunca han corrido contra un teléfono**.
* `flash_babe` no se ha probado de punta a punta: se decidió no simular el
  loader de escritura. Se verificó con los firmwares reales y leyendo el código.
* La sesión de CS loader del banco es una reconstrucción a partir del código.
  El formato real de `READALL` no se ha visto en el cable.
* La capa USB solo se ha probado con el `identify` real (lectura). Las lecturas
  por paquete y el reintento de apertura son posteriores a esa prueba.
* «Security disabled =)» se imprime sin condición tras cargar el loader, y las
  respuestas EsB/EhM/EaT/EbS/EdQ se aceptan con cualquier valor de 3 bytes: que
  salga no demuestra nada. SENUS lo usa solo como aviso.
* **Geometría física de la NOR y punto de no retorno de `flash_babe`**: el
  loader de setool2 borra por sector físico de 256 KiB, no por bloque BABE
  (64 KiB). Un corte entre el `0x10` del bloque 0 y el primer `0x01` deja los
  primeros 256 KiB del main en 0xFF, no 64 KiB. Detalles cruzados con los docs
  del aparato en [`sector_nor_256kib.md`](sector_nor_256kib.md). No es bug de
  seftool; es la física.

* **`read-flash --anycid`** (2026-10-17, [`read_flash_anycid.md`](read_flash_anycid.md)) **nunca ha corrido contra un
  teléfono**: ni la carga del loader de setool2 para leer, ni la trama `0x33` que contesta, ni el executer. El banco
  prueba las compuertas (antes de cualquier byte de loader), el plan de lectura y el escritor atómico, y repite la
  captura real del identify; **no** hay teléfono de guion para el loader de FLASH.

## Pendiente conocido

* PNX5230 / AVR / Z80 / DB2000: se acotaron las copias que se vieron, pero no se
  auditó todo ese código; no se usa con el W580/W600/W810.
* `fsx-upload`/`fsx-download` por rutas muy largas: ahora se rechazan; no se
  probaron contra un teléfono.
