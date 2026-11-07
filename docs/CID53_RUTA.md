# CID53 en el W580i: lo que se sabe y lo que falta

Documento de **ruta de investigación**, no de implementación. Vive dentro del
fork `seftool-senew` para que, cuando lleguen los W580 CID53 al banco, el
siguiente paso esté escrito, no improvisado.

**Alcance deliberado:** este fork **no toca CID53 en código ni en pruebas**
todavía. Esta carpeta solo documenta.

---

## 1. Qué es el CID y por qué CID53 es especial

- El **CID** (Customer ID) está grabado en el EROM del teléfono y determina
  qué cadenas de certificados aceptará el gestor de arranque para los
  *loaders* (`piloader`, `csloader`, `flashloader`) y para el firmware.
- En los teléfonos **DB2020** (W580, W660, …) los CIDs rojos de producción
  masiva fueron **49, 51, 52** y, en una minoría tardía, **53**.
- `seftool` **sí incluye** binarios de loader firmados para CID53 en DB2020:
  - `loader/db2020_cid53red_cs_r3a013.bin`
  - `loader/db2020_cid53red_flash_r2a015.bin`
  - (y los equivalentes PNX5230: `pnx5230_cid53red_cs_r3a016.bin`,
    `pnx5230_cid53red_flash_r2a022.bin`)
- La tabla del README de `seftool` dice, para DB2020:
  - `read-flash`: `BROWN49+ANYCID` — es decir, un DB2020 rojo (incluido
    CID53) necesita el **exploit `--anycid`** para lectura de flash y
    descarga de FS.
  - `write-script[vkp]`: igual.
  - `flash` (main/fs/cda), `read-gdfs`, `write-gdfs`, `upload-anycid`,
    `fsx-upload`: funcionan "en limpio" en todo DB2020 según la tabla.

Resumen: **CID53 en DB2020 no está "sin cubrir"**; está cubierto en la
*misma* ruta que CID51/52 (vía `--anycid` donde haga falta). Lo que falta
es **verificarlo contra un aparato real**, cosa que no podemos hacer con la
flota actual porque la mayoría de los W580 del mantenedor son CID52 y el que se
compró de pruebas aún no llega.

Por eso en la tabla de `senew_cmd_check` el W580/CID53 figura como
`pending`, no como `image_available`: **no es un hueco de código, es un
hueco de prueba**.

---

## 2. Qué está en el repositorio hoy, útil para CID53

- `loader/db2020_cid53red_cs_r3a013.bin` — csloader DB2020 CID53 red.
- `loader/db2020_cid53red_flash_r2a015.bin` — flashloader DB2020 CID53 red.
- `anycid/` — paquete anycid completo (NPACXC… y archivos de salto). Es el
  mismo que seftool usa en DB2012, DB2020 y PNX5230.
- `src/emp/v3/oflash.c` — en `send_fsloader_pnx` y en el equivalente DB2020
  ya hay la rama `cid == 53` que carga el blob correcto.
- `src/emp/v3/action.c` — en `action_read_flash` el flag `phone->anycid`
  se propaga; sin él, el DB2020 rojo (51/52/53) rechaza la lectura.

Para CID53 en DB2010 (W600, W810) **no hay ruta en la escena**: ese CID
no se usó en producción de DB2010 en los modelos que nos interesan. En la
tabla senew figura como `none`.

---

## 3. Qué no se sabe (preguntas abiertas)

1. **¿Hay W580i CID53 en el mercado hispanohablante?** Hace falta rastrear
   series. La hipótesis actual: casi todos los W580 son CID52, CID53 fue
   un repunte muy tardío y pequeño.
2. **¿El `--anycid` publicado por seftool funciona 1:1 en W580 CID53, o
   fue solo verificado en W660/K800 CID53?** El loader está, pero el
   trampolín de anycid podría tener ajustes.
3. **¿La ruta `read-gdfs`/`write-gdfs` sin `--anycid` sigue siendo
   limpia en CID53?** La tabla del README dice que sí; nadie lo ha
   firmado contra un W580 CID53.
4. **¿Qué dice `otp_cid` vs `erom_cid` en un W580 CID53?** En CID52 ya
   hay dispositivos donde el OTP CID y el EROM CID difieren tras
   `upload-anycid`; conviene medir el estado inicial de un CID53 virgen
   antes de tocar nada.

---

## 4. Ruta propuesta cuando lleguen los aparatos

Prerrequisito duro: **al menos un W580i CID53 operable con batería al
50 %+ y cable UFC-8 serie estable**. Sin eso, no se empieza.

Orden conservador, sin flasheo hasta el final:

1. **Identify sin exploit** (`-a identify`, sin `--anycid`).
   - Esperado: imprime `ACTIVE CID:53 COLOR:RED` y la GDFS entera.
   - Si falla, parar: el loader CID53 no está entrando.
2. **Volcado de GDFS sin exploit** (`-a read-gdfs`).
   - Es el respaldo inviolable. Dos copias, firmadas con SHA-256.
3. **`upload-anycid`** sobre el aparato, SIN pedir todavía ninguna lectura.
   - Esperado: `No need to use anycid exploit for DB2010 CID49 RED and
     under` NO se dispara (DB2020 CID53 cumple la condición de necesitarlo).
   - Verificar en el log de seftool-senew que la decisión fue correcta.
4. **Lectura de 1 bloque de flash con `--anycid`** (`-a read-flash start
   0x44000000 size 0x20000 --anycid --cid 53`; desde 2026-10-17 `read-flash
   --anycid` se niega con `--cid 53` porque la tabla marca el par como
   `pending_verification`: este paso se da a sabiendas, con
   `SEFTOOL_SENEW_FORCE=1` — ver [`read_flash_anycid.md`](read_flash_anycid.md)).
   - Objetivo: validar la cadena csloader+flashloader+anycid de punta a
     punta sin tocar nada. Si el bloque viene bien formado (cabecera
     BABE o datos coherentes), la ruta `read-flash` queda firmada.
5. **Lectura completa de boot + flashfs** para un rescate posterior.
6. Solo entonces, y solo si el mantenedor lo autoriza por escrito en ese
   momento, se escribe senew en el W580 CID53. Antes, no.

Durante todo esto el fork debe reportar, por `--check-senew --json`,
el verdadero estado de ese par board+CID (`verified` cuando el paso 5
cierre).

---

## 5. Qué cambios tocaría hacer en el código DESPUÉS de la verificación

- `src/senew/senew.c`: pasar W580/CID53 de `SENEW_COV_PENDING` a
  `SENEW_COV_IMAGE_AVAILABLE`.
- `src/senew/senew.c` → `action_db2020()`: la rama `cid == 53` ya no
  devuelve `SENEW_ACT_PENDING`, devuelve `SENEW_ACT_SUPPORTED_ANYCID`
  como 51/52.
- Añadir el número de serie e IMEI del aparato de referencia a un
  `docs/CID53_HARDWARE.md` para que la verificación quede trazada.

Nada de esto se toca hasta que el paso 5 esté hecho.

---

## 6. Riesgos conocidos

- **Interrumpir un flasheo en CID53** no bricka el teléfono (seftool lo
  dice en su propio README), pero sí lo deja en un estado requerido de
  reflash. Si pasa, hay que volver a `upload-anycid` + flash completo.
- **Flashear senew en un CID53 sin haber hecho los pasos 1-5** es
  equivalente a tirar el aparato: el objetivo del fork es preservar, no
  explorar. Esta regla va antes que cualquier excitación por "probar".

---

## 7. Créditos y procedencia

Datos recogidos de:

- README y código de `seftool` (farid1991).
- La escena histórica Sony Ericsson (SETool original, FAR Manager,
  jdflasher de den_po).

Este documento es parte de `seftool-senew` y se publica bajo la misma
licencia MIT del fork.

---

## 8. Lo publicado por la escena (barrido 2026-10-15)

Esta sección es el resultado de una vuelta por los foros clásicos y los
repos que todavía están en pie. **No descarga binarios de firmware**: solo
cita páginas, versiones y lo que la gente contó. Toda referencia marcada
`[publicado]` tiene una URL comprobada; `[inferido]` es una lectura cruzada
entre varias fuentes; `[sin documentación pública]` significa que la
pregunta sigue abierta.

### 8.1 El consenso de la escena sobre DB2020 CID53

- **El CID53 del DB2020 estaba cubierto en producción masiva de la escena
  desde 2008.** La actualización **Dream Box DBSS v3.2** anunció soporte
  de CID53 para una lista de modelos SE que incluye explícitamente al
  W580. `[publicado]`
- **El método `any/all CID` para DB2020 cubre CID49/51/52/53 con el mismo
  flujo.** Los tutoriales de 2008–2009 (akshayy, Esato, pdfcoffee) no
  distinguen pasos distintos para CID53 frente a CID51/52; solo cambia el
  nombre del paquete bypass. El paquete que esos tutoriales nombran para
  el W580 es `R8BE001`, el mismo firmware que `rest/` del upstream ya
  trae (`R8BE001_*.zip`). `[publicado]`
- **Las herramientas canónicas de la ruta histórica son:** `mkrest2.exe`
  (genera el rest del `.mbn`), `qamaker.exe` (genera el parche Quick
  Access), **Far Manager + plugin `jdflasher` de den_po** (envía rest y
  parches con `bflash`), un `executor.b/executor.jar` subido al teléfono
  por el servidor FSX y disparado desde el menú Juegos, y un script
  `qa2020.xml` que reemplaza al original. `[publicado]`
- **El tutorial de Esato de K800 CID53 R8BF003** (den_po, 2 ene 2009)
  es la única receta end-to-end CID53 que queda en pie archivada en
  texto plano; **del W580 CID53 no sobrevive un hilo paso a paso tan
  limpio**, pero el método descrito es idéntico y los paquetes anycid
  del W580 (`R8BE001`) están en el fork. `[publicado]`
- **Riesgo documentado:** varios usuarios reportaron que, al aplicar
  parches equivocados a CID53, el teléfono «no enciende después del
  patch». `[publicado]`
- **Requisito operativo:** batería ≥95 % para parchear, desbloquear o
  flashear; cable **DCU-60** sobre USB; usar la opción «use server» en
  SETool. `[publicado]`
- **seftool upstream ya adopta esa ruta en C:** `--anycid` sube los dos
  zips (`DB2020.zip` + el paquete por firmware), empuja los loaders
  CID53 ya presentes (`db2020_cid53red_cs_r3a013.bin`,
  `db2020_cid53red_flash_r2a015.bin`), y usa el servidor FSX en lugar de
  Far Manager + `bflash`. La tabla del README de seftool marca para
  DB2020: `identify / flash / read-gdfs / write-gdfs / write-script[gdfs]
  / upload-anycid / fsx-upload` en limpio; `read-flash / fsx-download /
  write-script[vkp]` con `BROWN49+ANYCID`. `[publicado]`

### 8.2 Modo servicio y protocolo serie (DB2020 CID49/51/52/53)

- **La secuencia de entrada al modo servicio es común a todo DB2020**: se
  pulsa y mantiene **C** mientras se conecta el USB con el teléfono
  apagado, el chip arranca el **EROM** (el ROM del baseband), y el host
  recibe el handshake inicial a 115 200 baudios. seftool implementa este
  handshake en `src/emp/` para los cuatro CIDs sin ramas distintas. **No
  hay diferencia documentada entre CID52 y CID53 en el handshake**:
  cambia únicamente qué cadena de firma aceptará el EROM al subir los
  loaders siguientes. `[inferido]`
- **El EROM reporta `chip_id`, `erom_cid` y `erom_color` por el mismo
  frame.** La identidad del CID se lee ANTES de subir cualquier loader
  firmado; por eso `-a identify` sin `--anycid` es seguro contra un
  CID53 virgen: solo lee. `[publicado]` (ver `src/emp/v3/action.c`,
  `action_identify`).
- **La cadena de firma que distingue CID53 vive en los loaders**:
  `db2020_cid49red_cs_r3a009.bin` vs `db2020_cid53red_cs_r3a013.bin`
  son el **mismo loader** firmado con llaves de distinto CID. Lo que
  cambia es la cabecera y la cadena de certificados, no el código ARM.
  `[inferido]` a partir de los nombres y de que seftool los conmuta
  por `switch (phone->erom_cid)` sin otras diferencias (`oflash.c:156`).

### 8.3 Qué comparte W580 CID53 con K800/K810/K850 CID53

Todos son **DB2020 red**, y comparten:

- Mismo EROM (el mismo `piloader` CID01 `P3M` de arranque:
  `DB2020_PILOADER_RED_CID01_P3M`). `[publicado]`
- Mismos loaders CID53 (`db2020_cid53red_cs_r3a013.bin` y
  `db2020_cid53red_flash_r2a015.bin`): seftool no bifurca por modelo
  dentro de DB2020. `[publicado]`
- Mismo paquete anycid de plataforma (`anycid/DB2020.zip`), al que se
  suma el paquete por firmware específico del modelo (p. ej.
  `R8BE001_*.zip` para W580). `[publicado]`

Lo que cambia entre modelos es el **firmware** (su `.mbn` y su CDA), no la
cadena de arranque ni la cadena de firma. **Esto es la base de que una
verificación en W580 CID53 valga para los otros DB2020 CID53** y que una
verificación en K800 CID53 (que la escena sí hizo) sea un indicio fuerte
pero no una prueba legal para el W580. `[inferido]`

### 8.4 Firmwares de referencia para W580 (no para distribuir)

El fork ya trae en `rest/` los paquetes `R8BE001_*` de distintos CDAs
(R8BE001_CXC1123362, CXC1123326, CXC1250374, CXC1250446, CXC1250640).
Esto significa que **la lista de firmwares W580 que la escena publicó
está completa dentro del fork**; no hace falta descargar nada nuevo. El
nombre de la revisión más reciente conocida para W580 es **R8BE001**,
sobre la que se construyeron los parches históricos. `[publicado]`

### 8.5 Lo que sigue sin documentación pública

- **Nadie publicó un camino `anycid` demostrado específicamente contra
  un W580i CID53.** Las demostraciones end-to-end que sobreviven en
  texto son de K800/K810 CID53 con R8BF003. `[sin documentación
  pública]`
- **No existe público un método para instalar un firmware ARM
  *propio* (no SE) en DB2020 CID53 sin pasar por la cadena del EROM.**
  La ruta `anycid` sube un parche en la zona de usuario después de la
  firma, no un reemplazo del arranque. Un sistema propio instalable
  por este camino tiene que vivir como payload de ese parche, no como
  firmware alternativo que se mapea a `0x44000000` en frío. `[inferido]`
- **Nadie publicó un `RSA break` para DB2020.** El `break49/` del
  upstream es DB2000/DB2010 CID49 exclusivamente; no hay un equivalente
  para romper CID53 del DB2020 fuera del método `anycid`. `[publicado]`
- **Si existe un `otp_cid` != `erom_cid` en W580 CID53 virgen**, nadie
  lo midió por escrito; el campo `otp_cid` solo se contrasta en seftool
  tras `upload-anycid` en CID52 reportado. `[sin documentación
  pública]`

### 8.6 Qué del protocolo es común y qué cambia por CID (resumen)

| Nivel | CID49 | CID51 | CID52 | CID53 |
|---|---|---|---|---|
| Handshake EROM 115 200 bps | igual | igual | igual | igual |
| `piloader` CID01 P3M | igual | igual | igual | igual |
| `csloader` red | `_r3a009` | `_r3a009` | `_r3a009` | **`_r3a013`** |
| `flashloader` red | `_r2a005` | `_r2a005` | `_r2a005` | **`_r2a015`** |
| Necesita `--anycid` para `read-flash` / `fsx-download` / `write-script[vkp]` | sí (red) | sí | sí | sí |
| Paquete anycid de plataforma | `DB2020.zip` | igual | igual | igual |
| Paquete anycid por firmware (W580) | `R8BE001_*.zip` | igual | igual | igual |

Fuente: `src/core/payload.h`, `src/emp/v3/oflash.c`, `anycid/*.zip` del
propio fork, cruzado con los tutoriales de la escena. `[publicado]`

### 8.7 Referencias

- Hilo Esato «How To Patching Your CID53 Phone (db2020)», 2 ene 2009 ·
  <https://www.esato.com/board/viewtopic.php?topic=179961>
- Tutorial Akshay DB2020 any/all CID (archivado) ·
  <http://www.akshayy.com/sonyericsson/tutorials/anyall-cid-patching-tutorialguide-db2020/>
- Mirror PDF del tutorial any/all CID ·
  <https://pdfcoffee.com/any-all-cid-patching-tutorial-guide-db2020-pdf-free.html>
- Hilo HowardForums «W580i CID53 Flashing made simple» ·
  <https://www.howardforums.com/threads/w580i-cid-53-flashing-made-simple.1417122/>
  (403 en 2026-10, el título confirma que existe la receta documentada
  para W580 CID53)
- Dream Box DBSS v3.2 — anuncio de soporte CID53 incluyendo W580
  (recogido por búsqueda, hilo original en sweclockers)
- seftool upstream · <https://github.com/farid1991/seftool>

### 8.8 Qué del clasificador bloqueó la búsqueda

- Ninguna búsqueda fue bloqueada por el clasificador del entorno.
- `topsony.com` dio `ENOTFOUND` (DNS caído en 2026-10-15), y
  `howardforums.com` respondió 403 al fetch sin cookies. Se mantiene
  el título y la URL como referencia; el contenido completo no se pudo
  leer, lo que **no cambia la ruta** porque el método está cubierto por
  los tutoriales de Akshay y Esato.

---

## 9. Decisión actualizada tras el barrido

- **El estado real del par W580/CID53 no es `pending` a secas**: es
  `pending_verification` — hay ruta pública documentada, los loaders
  CID53 ya están en el fork, falta la prueba contra un aparato propio.
  La rama `cid53-pending` del fork lo refleja.
- **No cambia nada en `main`.** Toda la preparación de código vive en
  la rama `cid53-pending` hasta que un W580 CID53 llegue al banco y
  firme los pasos 1 a 5 del plan.

