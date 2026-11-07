# --jdflasher: destrabar DB2020 CID 52 RED sin executer

Rama paralela a `--anycid` para destrabar un W580 DB2020 CID 52 RED usando el
camino oficial del plugin `jdflasher.dll` (den_po). Dos acciones usan la misma
rama de carga del flashloader: `read-flash --jdflasher` (lectura, commit 1) y
`flash-jdflasher` (escritura, commit 2). La escritura es DRY-RUN por defecto.

## Fuente autoritativa

El protocolo no se infiere: viene literal del XML del plugin, bajado de la Wayback
Machine y espejado en el repositorio hermano `w580`:

```
jdflasher_ext/jdflasher/scripts/db2020.xml
```

Bloque relevante para CID 52:

```xml
<bflash break="2020_52">
  <qhtry>2020_prologue</qhtry>
  <run/>
  <ldr>2020_f_R2A005</ldr>
  <activateloader/>
</bflash>
```

Es decir: `qhtry(2020_prologue)` → `run` → `ldr(2020_f_R2A005)` → `activateloader`.
No hay un `qhldr(2020_pi_P3M)` previo, no hay `csloader` adicional en esta rama,
no hay `mem_patcher` (los `db2020_cid5?_mem_patcher_r2a006.bin` que trae `loader/`
son de la herramienta SETool2 Lite, otra cadena distinta).

## El `2020_prologue`

- Vive en `loader/2020_prologue`.
- SHA-256: `e78a17a1c6ac22672e23a3388905b7076b5fbd5f41db84876fbb89cb418f085e`.
- Tamaño: 39 679 bytes.
- Cabecera BABE **versión 3** (`BA BE 00 03`), el mismo formato que ya usa el
  `2020_pi_P3M` del camino clásico. `struct babehdr_t` (ver `src/emp/v3/babe.h`,
  comentario literal `// 03/04`) acepta v3; `loader_fit()` no mira el campo
  `ver` — solo rechaza headers absurdos (prólogo o cuerpo > 16 MiB).

## Reconexiones

Una SOLA al final del flujo completo (al terminar la escritura), con el texto
inglés que ya trae `lang#1.lng`:

> «unplug the cable, reinsert battery and press ok»

Las memorias externas que decían «3 reconexiones durante el flujo» no cuadran con
el XML ni con los strings del plugin. Esto corrige esa anotación.

## Compuertas (línea de órdenes)

Se cablean en `src/main.c` después de `parse_args()`, antes de abrir el puerto.
Toda la lista:

- `--jdflasher` presente.
- `--cid 52` (si viene distinto, se niega; `--cid 49/51` van por `--anycid`;
  `--cid 53` queda explícitamente pendiente, igual que en `read_flash_anycid.md`).
- `--imei <S>` presente y no vacío. Jdflasher **no** carga el loader de P3M antes
  del prólogue, así que no hay OTP leído previamente: la identidad entre sesiones
  tiene que darla quien llama.
- `--anycid` ausente (mutuamente excluyente: jdflasher no usa executer).
- `--break-rsa` ausente (ésa es la ruta de DB2010 CID 49 RED).
- `SEFTOOL_JDFLASHER_CONFIRM=1` en el entorno, exacto. Cualquier otra cosa — vacío,
  `0`, `yes`, `true` — se niega.

## No herencia desde SENUS

**SENUS no debe exportar `SEFTOOL_JDFLASHER_CONFIRM` al proceso hijo `seftool`.**
Mismo molde que `SEFTOOL_BREAK_CONFIRM`: una confirmación olvidada en el shell
nunca puede destrabar el preloader. El filtro de entorno vive en el fork
`mac-usb` de SENUS, no en este repositorio.

## Orden de operaciones mandatorio

Antes de usar `--jdflasher` contra un aparato real:

1. **Respaldo doble de GDFS**: `mac/doble_gdfs.sh` para dejar dos lecturas de la
   GDFS cotejadas en disco.
2. **Respaldo de la NOR por anycid**: `read-flash --anycid --cid 52 --imei <S>
   --restore-boot` (ruta ya probada en aparato, con compuertas y reparación de
   arranque posterior). El volcado se compara contra el SHA-1 que imprime seftool.
3. Solo entonces `--jdflasher`. La LECTURA (`read-flash --jdflasher --cid 52
   --imei <S>`) es inocua: carga el flashloader y vuelca; la ESCRITURA
   (`flash-jdflasher main <f> [fs <f>] --cid 52 --imei <S>`) va con DRY-RUN
   forzoso salvo que `SEFTOOL_JDFLASHER_WRITE=1` también esté puesto.

## Escritura: compuertas duras y DRY-RUN por defecto

`flash-jdflasher` reutiliza la rama del flashloader del commit 1, pero añade
las compuertas de ESCRITURA. Son DOS variables de entorno, no una, y las dos
tienen que valer `1` exacto:

- `SEFTOOL_JDFLASHER_CONFIRM=1` — ya exigida para cualquier `--jdflasher`
  (lectura y escritura). Dice «sé lo que destraba el prologue, lo he leído».
- `SEFTOOL_JDFLASHER_WRITE=1` — específica de la ESCRITURA. Sin ella la
  función carga el flashloader R2A005 y vuelve `0` sin mandar ningún
  `BFLASH`: el DRY-RUN demuestra que el prologue engancha pero no toca la
  NOR. Imprime en stdout cómo relanzar con `WRITE=1` para flashear de
  verdad.

**Ni `SEFTOOL_JDFLASHER_CONFIRM` ni `SEFTOOL_JDFLASHER_WRITE` las hereda
SENUS al llamar a `seftool`.** El filtro ya ignora la primera; añade la
segunda al mismo filtro en el fork `mac-usb` de SENUS. Una confirmación
olvidada en el shell nunca puede flashear.

`flash-jdflasher` NO acepta `cda` (molde `flash-anycid`); aceptarlo y callar
daría la impresión de que se había escrito. El parser lo rechaza en línea de
órdenes.

**Jdflasher es irreversible en el preloader: destraba la verificación RSA.
Sin respaldo previo (`mac/doble_gdfs.sh` para GDFS, y `read-flash --anycid
... --restore-boot` para la NOR entera cotejada por SHA-1) `flash-jdflasher`
NO se usa.** El dispositivo pierde la cadena de confianza del arranque y, si
el main escrito está roto, no hay vuelta atrás sin otra línea de carga.

## CID 53

El XML no lo cubre: no hay `<bflash break="2020_53">`. Las compuertas rechazan
`--cid 53 --jdflasher` explícitamente. CID 53 sigue por `docs/CID53_RUTA.md`.

## Causa del `-2 of 902 bytes` tras el prologue (2026-10-18, noche)

Las primeras pruebas contra el W580 roto fallaron no en la reconexión USB
sino en el formato del siguiente binario. Análisis estático de
`jdflasher.dll` con capstone (sin tocar el aparato) muestra que el
comando `<ldr>` del script XML tiene bifurcación interna según un flag
de sesión en offset `0x1016c`:

```
if (session->flag_already_loaded == 0)
    qhldr(name, 0);   // crudo: QH00/QA00/QD00 ASCII + handshake
else
    cmd3c(name);      // empaquetado: cmd 0x3C header 0x89 + checksum
```

Solo `cmd3c` marca ese flag al terminar; `qhldr`/`qhtry` lo leen pero no
lo marcan. En el flujo CID52 (`<qhtry>2020_prologue</qhtry> <run/>
<ldr>2020_f_R2A005</ldr> <activateloader/>`) el flag queda a 0 tras el
prologue, de modo que el `<ldr>` del R2A005 se despacha por la rama
**cruda** (qhldr). El `<activateloader/>` posterior sí marca el flag.

Corroboración por `scripts/qa2020.xml` del mismo plugin: para CID52 pone
`<ldr>` (deja decidir al flag → crudo); para CID53 pone `<cmd3c>`
explícito forzando empaquetado. El autor del plugin ya sabía que, tras
SU prologue, el CID52 deja el bootrom en modo crudo persistente y el
CID53 en modo empaquetado. Es la hipótesis del cuaderno del día
confirmada por el propio DLL — sin tocar el teléfono.

Nuestra implementación de ayer mandaba el R2A005 por
`loader_send_binary` → `loader_send_binary_noact` → cmd 0x3C: la rama
equivocada para CID52. 902 B = 5 B header 0x3C + 896 B primer bloque del
R2A005 BABE + 1 B checksum. El bootrom espera los 4 B ASCII `"QH00"`
como marcador y rechaza el header 0x89 → `serial_write` corta por
timeout → `-2 of 902`.

El fix cablado en `jdflasher-cid52-r2a005-crudo`:

- Función nueva `loader_send_qhldr_jdflasher(port, phone, name)` en
  `src/emp/v3/loader.c`: verifica SHA-1 (`DB2020_FLASHLOADER_R2A005_SHA1_HEX`),
  manda QH00/QA00/QD00 crudo, byteleft + `'R'`, espera los 3 B de packet
  (DB2020) + hello 0x89, decodifica, llama a `loader_activate_payload`.
- `src/emp/v3/bflash.c` rama jdflasher: la segunda llamada
  (`loader_send_binary(..., DB2020_FLLOADER_R2A005_DEN_PO)`) pasa a
  `loader_send_qhldr_jdflasher(..., DB2020_FLLOADER_R2A005_DEN_PO)`.
- `src/emp/v3/action.c` `action_read_flash` jdflasher: idéntico cambio.
- Rama bfs (`loader_send_binary(..., DB2020_CSLOADER_R3A006_DEN_PO)`
  encadenado sobre R2A005): queda igual con un `TODO` nuevo —
  `qa2020.xml` indica que la vía bfs CID52 jdflasher debe
  **re-ejecutar prologue + run + qhldr(R3A006) desde cero**, no encadenar
  sobre R2A005. No se cambia aquí porque esa vía no se ha ensayado aún.

## Reconexión tras el prologue (2026-10-18)

La primera prueba real contra el W580 roto (sesión 1 `jdflasher-prime` limpia,
sesión 2 `--skip-prologue` con reconexión FÍSICA hecha por el humano) falló al
primer `serial_write` del header de `2020_f_R2A005` (`-2 of 902 bytes`, es decir
`SP_ERR_FAIL` de `mac/sp_usb.c`, no un código de libusb). Diagnóstico, sin tocar
el aparato de nuevo:

- **El `R` del prologue reinicia el teléfono DE VERDAD**, no es un hipo del bus
  USB: el aparato arranca su sistema operativo (confirmado por el mantenedor: "modo USB
  de datos del Walkman"). Esto es justo lo que ya asume `bflash_repair_boot` /
  `break_cid49` para DB2010 en `emp/v3/break.c`: tras su propio exploit, ESE
  camino YA reconecta con `connection_restart()` antes de mandar el siguiente
  binario — nuestro `loader_send_bflash_ldr_db2020` para CID52 no lo hacía.
- **`jdflasher.dll` (el plugin de den_po) no necesita un `<reconnect/>` en el
  XML para este paso porque lo resuelve por software.** Su tabla de importación
  (`objdump -p jdflasher.dll`) trae `SETUPAPI.dll`: `SetupDiGetClassDevsA`,
  `SetupDiEnumDeviceInterfaces`, `SetupDiGetDeviceInterfaceDetailA`,
  `SetupDiGetDeviceRegistryPropertyA`. No importa `RegisterDeviceNotification`
  ni nada de mensajes de ventana (`USER32.dll` solo trae `MessageBoxA` y
  compañía) — es decir, **sondea** el bus (`SetupDiGetClassDevs` + `CreateFileA`
  en un bucle con `Sleep`/`GetTickCount`, también importados), no espera un
  evento. El mensaje "unplug the cable, reinsert battery and press ok" de
  `lang#1.lng` es para OTRO punto del flujo (al terminar, según el propio XML:
  no hay ninguna etiqueta de reconexión entre `<run/>` y `<ldr>` para CID 52).
- **La diferencia real entre `2020_prologue` y `2020_pi_P3M`** (mismo formato
  BABE v3, mismo tamaño de cabecera) no son los 256 B de firma RSA (eso cambia
  entre cualquier par de artefactos firmados distintos) sino 3 bytes en el
  campo `z1` de `babehdr_t` (`src/emp/v3/babe.h`, comentario literal `// used
  by bootrom`): `cc cc cc` en el prologue contra `00 00 00` en P3M. Candidato a
  ser la marca que el bootrom usa para decidir el comportamiento de "prologue,
  no flashloader" (sin hello, sin activate) frente a "loader normal". No
  cablear nada sobre esta lectura sin más evidencia; queda como nota.
- **Arreglo aplicado** (sin tocar el aparato): entre `loader_send_prologue_jdflasher`
  y `loader_send_binary(..., R2A005)`, en los DOS sitios que encadenan la pareja
  (`loader_send_bflash_ldr_db2020` en `bflash.c` y `action_read_flash` en
  `action.c`), se llama ahora a `connection_restart(port, phone)` — la MISMA
  función que ya usa `break_cid49`. Reutiliza la espera ya cableada en
  `mac/sp_usb.c::sp_open` (hasta 15 s a 250 ms, variable `ya_se_abrio`) más el
  plazo de `wait_for_Z` (hasta 30 s): ~45 s de margen total, con progreso
  impreso, sin que el humano desconecte nada. `erom_get_info` es no-op para
  DB2020 (`src/core/connection.c`), así que `connection_restart` no dispara
  ninguna compuerta inesperada ligada a CID/color.
- El diseño de DOS SESIONES (`jdflasher-prime` + `--skip-prologue`) queda como
  **vía de respaldo manual** si la reconexión automática no alcanza (p. ej. el
  enlace USB de la Mac es inestable, ver `seftool-en-mac-por-usb.md`), no como
  el camino principal.
- Todavía **sin probar contra el aparato** este diseño nuevo. Antes de repetirlo
  en el W580 roto: confirmar que no se agrava nada si `connection_restart`
  tarda su máximo (~45 s) y el aparato ya arrancó su sistema operativo para
  entonces (hipótesis no descartada: si el SO ya tomó el control del USB, el
  reintento de `sp_open` nunca encontrará `0fce:adde` de vuelta hasta que el
  humano fuerce modo C de nuevo — en ese caso el error será claro y limpio,
  igual de recuperable que hoy, solo que automático en vez de manual).

## Mensajes de error del protocolo

Los `fprintf(stderr)` de `loader_send_qhtry_jdflasher` usan el vocabulario del
`lang#1.lng` del plugin para que lo que imprime la terminal case con lo que
documenta el proveedor:

- `header not accepted` → `EsB` tardó tras `QH00`.
- `prologue not accepted` → `EhM` tardó tras el header.
- `prologue execution failed` → `EaT` tardó tras el cuerpo, o no llegó el
  `byteleft` detrás.
- `bad payload` → no hubo paquete `hello` tras `R`.
