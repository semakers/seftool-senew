# Integración seftool-senew ↔ SENUS

**SENUS** (Sony Ericsson New Update Service) es la herramienta Flutter de
escritorio que instalará y mantendrá **senew** en los teléfonos soportados
(W580, W600, W810). Su fase 2 llama a `seftool-senew` como back-end para
hablar con el teléfono por el cable serie.

Este documento especifica **qué contrato ofrece `seftool-senew` para que
SENUS pueda hacer esa llamada y parsear sus resultados** sin heurísticas
sobre texto humano.

---

## 1. Modos de salida

| Modo       | Cómo se pide            | Para quién             |
|------------|-------------------------|------------------------|
| Humano     | (por defecto)           | el mantenedor en terminal     |
| JSON       | `--json`                | SENUS (fase 2)         |

En este encargo solo está implementado el modo JSON para **`--check-senew`**
(no hay teléfono conectado). Las acciones con teléfono (identify, read-gdfs,
read-flash, …) siguen imprimiendo texto humano. La ruta JSON para ellas se
abrirá cuando llegue el primer aparato al banco y se puedan medir campos
estables (no se diseña a ciegas; ver *la trampa escrita se repite*).

---

## 2. `--check-senew --json`: contrato actual

```json
{
  "tool": "seftool-senew",
  "schema": 1,
  "boards": [
    {
      "name": "W580",
      "chipset": "DB2020",
      "cids": {
        "49": "image_available",
        "51": "image_available",
        "52": "image_available",
        "53": "pending"
      }
    },
    { "name": "W600", "chipset": "DB2010", "cids": { "49": "image_available", "51": "image_available", "52": "image_available", "53": "none" } },
    { "name": "W810", "chipset": "DB2010", "cids": { "49": "image_available", "51": "image_available", "52": "image_available", "53": "none" } }
  ]
}
```

### Campos

- `tool` — nombre del binario. Siempre `"seftool-senew"`. SENUS debe
  rechazar cualquier otro valor (no es su fork).
- `schema` — entero. SENUS debe comparar con el que entiende. **Cambio de
  esquema ⇒ `schema` sube 1**. No se añaden campos silenciosamente.
- `boards[].name` — identificador corto del board senew (`W580|W600|W810`).
- `boards[].chipset` — `DB2020` o `DB2010`. Lo que la ingeniería inversa
  usa para elegir loaders.
- `boards[].cids` — objeto. Clave = CID como string decimal. Valor = uno
  de:
  - `image_available` — hay OS senew para ese par; SENUS puede ofrecer
    "instalar".
  - `pending` — par documentado, aún sin verificar en hardware real.
    SENUS debe mostrarlo pero NO ofrecer instalar.
  - `none` — par no aplicable (p. ej. CID53 en DB2010). SENUS lo oculta.

### Estabilidad

- Los nombres de campo y el conjunto de valores de `cids` **son contrato
  estable** mientras `schema == 1`.
- Añadir boards (p. ej. K750 cuando entre al proyecto) **no** sube
  `schema`.
- Añadir CIDs tampoco: SENUS debe iterar las claves de `cids`, no
  hardcodearlas.
- Añadir un nuevo valor en `cids` (p. ej. `verified`) **sí** sube
  `schema`, porque es un cambio semántico que SENUS debe entender.

---

## 3. Cómo llamarlo desde Flutter

```dart
final r = await Process.run('seftool-senew', ['--check-senew', '--json']);
if (r.exitCode != 0) throw SenusError('seftool-senew failed');
final doc = jsonDecode(r.stdout as String) as Map<String, dynamic>;
if (doc['tool'] != 'seftool-senew') throw SenusError('wrong tool');
if (doc['schema'] != 1) throw SenusError('unsupported schema');
// ... iterar boards[]
```

`stderr` se puede ignorar en este comando (no escribe nada en el camino
feliz). Para los comandos de teléfono SENUS tendrá que leer stderr: es
donde seftool pone los `Error:`.

---

## 4. Pendiente para fase 2 (teléfono conectado)

Cuando llegue el primer aparato soportado al banco, abrimos un segundo
contrato JSON para `-a identify`, con los campos que ya imprime en humano:

- `chip_id` / `platform` (DB2010, DB2020, PNX5230)
- `erom_cid`, `erom_color`
- `otp_locked`, `otp_cid`, `otp_paf`, `imei`
- `gdfs.model`, `gdfs.brand`, `gdfs.cxc_article`, `gdfs.cda_article`, ...
- `senew.board` (el que resuelve `senew_board_from_model`)
- `senew.coverage` (uno de los valores de `cids`)

Hasta entonces SENUS debe parsear la salida humana con cuidado y marcar
"identify" como experimental. **No se diseña el contrato antes de medirlo
contra un aparato real.**

---

## 5. Qué NO ofrece seftool-senew

- No tiene un "modo biblioteca" cargable (`.so`/`.dll`): es un CLI con
  salida JSON. Esta decisión es deliberada: una biblioteca comparte
  espacio de memoria con SENUS y cualquier crash en el protocolo serie
  se llevaría la UI. CLI + JSON mantiene la aislación.
- No descarga firmwares: eso lo hace SENUS (o un servicio aparte).
  `seftool-senew` solo *habla con el teléfono*.
- No decide políticas de "sí/no instalar": solo reporta la cobertura.
  La política vive en SENUS.

---

## 6. Variables de entorno conocidas por SENUS

- `SENUS_SEFTOOL_RAIZ=/ruta/al/fork`: cambia la raíz del fork (por omisión
  `seftool-senew`).
- `SENUS_SEFTOOL_ENSAYO=1`: ejecuta en modo ensayo (no lanza el binario,
  solo imprime la orden). Para desarrollo y tests.
- `SENUS_PUERTO=/dev/ttyUSB0`: fuerza el puerto que ve `Seftool`.

`SEFTOOL_SENEW_FORCE=1` existe en el fork y salta las compuertas de senew en
`flash-anycid` y `read-flash --anycid` (la tabla de CID y la ventana de la
NOR). Solo vale el valor exacto `1` (antes valía cualquier valor, incluso
vacío). **SENUS no lo activa por su cuenta ni lo hereda**: lanza seftool con
el entorno del padre SIN esa variable ni `SEFTOOL_BREAK_CONFIRM` (cualquier
grafía de mayúsculas; `Seftool.confirmacionesDelHumano`). Si el mantenedor quiere
forzar, lo lanza desde terminal.

`SEFTOOL_BREAK_CONFIRM` también solo vale con el valor exacto `1`, y **SENUS tampoco la hereda**
(decidido el 2026-10-17): autoriza `--break-rsa`, que ESCRIBE un bootloader parchado en la NOR, y después
seftool espera «quitar la batería y pulsar Y» por stdin, que SENUS cierra. Con la variable exportada el
resultado era «input error», bootloader escrito y main sin flashear. Por eso SENUS **no pasa `--break-rsa`
en ninguna orden**: el plan de instalación bloquea los W600/W810 con CID ≤ 49 (los que lo necesitarían) y
explica que hay que hacerlo a mano desde el CLI. (Una versión anterior de esta nota decía lo contrario,
«SENUS nunca lo pasa»: era falso; SENUS lo añadía solo en ese camino.)

SENUS pasa `-b 921600` en cada orden con puerto (`Seftool.baudios`): es la
velocidad de todas las recetas validadas; con la de por omisión de seftool
(115200) los 64 MiB de la NOR por un cable serie tardarían ~1,5 h. Con
`-p usb` no cuenta.

### `--imei <IMEI>`: de quién es el teléfono de cada sesión

Cada orden de seftool es una sesión nueva y entre una y otra se vuelve a enchufar el cable.
SENUS pasa `--imei <IMEI>` (el que dijo `identify`, o el del respaldo al restaurar/instalar) en
**toda orden con puerto menos `identify`**, justo antes de `-a`. seftool lo compara en cuanto el
primer loader (el piloader P3M) está activo y el OTP leído —antes del CS loader, del flashloader o de
cualquier loader de exploit, y por tanto antes de escribir nada— y, si no coincide o el aparato no
dice su IMEI, sale con `imei-check: REFUSED: …` (última línea de stderr) sin mandar nada más.

El valor es el que imprime `identify` (`IMEI:`), hasta 15 letras, cifras o `_` y **con alguna letra o cifra**: un OTP
en blanco (todo 0x00/0xFF) sale como `______________` y no identifica a ningún teléfono, así que el parser lo
rechaza (rc 1, sin abrir el puerto), igual que SENUS (`Seftool.imeiValido`). Un teléfono con el OTP en blanco no se
puede respaldar ni restaurar desde SENUS: no hay forma de comprobar de quién es en cada sesión. SENUS valida el mismo criterio antes de lanzar la orden y **nunca** la lanza
sin `--imei` cuando hay un IMEI esperado: un IMEI inválido hace fallar la orden en SENUS, no la deja
pasar sin comprobación.

## 7. Lo que SENUS llama exactamente a `flash-anycid`

`seftool -p <puerto> -b 921600 -a flash-anycid main <ssw> --cid <N>`:

- NO lleva `--anycid` suelto; la propia acción fuerza `phone->anycid = 1`
  en `action.c:281`. Pasarlo doble es redundante y no cambia nada, pero
  SENUS lo omite por limpieza.
- SENUS saca `<N>` del `identify` previo (ACTIVE CID), guardado en el
  respaldo de rollback.
- `<ssw>` sale del dropdown del selector de hito en la pantalla
  «Instalar senew», que descubre en runtime los `*_44140000.bin` dentro
  de `senew-w580/build/aparato/`.

## 8. Lo que SENUS llama a `read-flash --anycid` y a la restauración

Respaldo (W580 RED CID 49/51/52), una orden por sesión y una pausa «reconecta en
modo C» entre sesiones:

```
identify
read-gdfs                  (×2)
upload-anycid --anycid     (ESCRIBE 3 ficheros en el FS del teléfono)
… pausa: abrir «executer» (hasta 3 intentos) …
read-flash start 0x44000000 size 0x4000000 --anycid --cid <N>      (-p <puerto> -b 921600)
```

Líneas de stdout/stderr que SENUS lee (no cambiar sin avisar): `read-flash --anycid: REFUSED` (negativa de la
compuerta; no se reintenta), `imei-check: REFUSED` (otro teléfono; no se reintenta ni se pide el ritual del
executer; en `read-flash --anycid` y `flash-anycid` es la última línea de stderr y **no** va acompañada de
`Run executer first` ni de la lista de causas del executer, que antes salían también y se confundían con
«executer ausente»), `Run executer first` (se reintenta el paso del executer) y
`Flash dump saved <ruta> (<n> bytes, SHA-1 <hex>)` (el volcado está entero; si seftool sale con error *después*,
se acepta). El volcado se busca por su nombre exacto `flashdump_<imei>_44000000_04000000.bin`; un `.INCOMPLETO`
no cuenta.

SENUS **no se fía del nombre**: antes de cada intento aparta (`….previo-<marca>`, sin borrar) cualquier volcado y
`.INCOMPLETO` con ese nombre de una pasada anterior —una orden a mano en el CLI deja exactamente el mismo nombre—
y solo acepta el volcado si la línea `Flash dump saved` de ESTA pasada nombra ese fichero, con 64 MiB y con el
SHA-1 que SENUS calcula sobre el fichero en disco. Después comprueba que sea creíble como NOR: los 256 KiB de
`0x44000000` y de `0x44140000` no son un solo byte repetido, el primer MiB no se repite en el MiB 16, 32 o 48 (un
loader que ignorase la dirección) y el volcado contiene `prg<MAPP CXC article>` (el ID del firmware, p. ej.
`prgCXC1123362`). Un volcado que no pasa se deja junto al respaldo como `flash_full.bin` pero NO se registra. Esa
última comprobación es una **suposición sin verificar en un aparato** (ver `read_flash_anycid.md`).

Restauración (mismo aparato): `flash main` en un W580 RED va por OFLASH con el flashloader firmado y
`flash_babe_fw` rechaza en el host una imagen de `raw2babe` («Can not check BABE file»). Una imagen sin firma solo
entra por la rama anycid, así que SENUS restaura el main con `upload-anycid` → pausa «executer» →
`flash-anycid main <recorte> --cid <N>` → pausa → `write-gdfs`. CID 53, BROWN y DB2010 siguen con `flash main`.
**Sin probar en un aparato** (y, si senew ya reemplazó el main, el paso de «executer» —que necesita el Java de
fábrica— puede no poder completarse: SENUS ofrece entonces restaurar SOLO el GDFS).

## 9. Trampas conocidas

- `./.seftool_tmp` es relativo al cwd del proceso seftool (ver
  `src/emp/v3/csloader.c:1483`). Si el mantenedor lanza seftool a mano desde
  otra terminal mientras SENUS está trabajando, los dos procesos se
  pisan. SENUS no lanza dos a la vez; aun así, mejor no abrir dos
  terminales con `./seftool` activo en el mismo cwd.
- `csloader_put_file` ahora exige `repl.data[1] == 0` (ver
  `auditoria_2026-10-16.md`). Antes se tragaba un error del FSX; SENUS
  verá «Putfile refused by phone: cmd 0x03 status 0x12 at offset …»
  en el log cuando el teléfono rechace un chunk.
