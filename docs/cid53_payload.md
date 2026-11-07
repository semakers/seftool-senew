# CID53 en el W580i: el payload anycid y qué puede ser senew ahí dentro

Este documento es el estudio técnico del flujo **anycid** del DB2020 red —el
único camino público para meter código propio en un W580i CID53— y el **diseño**
de cómo senew puede vivir ahí. No se implementa nada hasta la fase 4; aquí solo
se decide.

Marcas de confianza:
- `[publicado]` — en los tutoriales de la escena o visible al ejecutar el código
  de seftool upstream contra el flujo.
- `[seftool]` — lo deduce el propio código de `seftool-senew` (ficheros citados).
- `[firmware]` — se ve en la imagen `w580_r8be001_main.bin` (ver `docs/arranque.md`
  del árbol `w580/sistema`).
- `[inferido]` — lectura cruzada de dos o más fuentes.
- `[sin aparato]` — pregunta que solo contesta un W580 CID53 real conectado.

---

## 1. Qué es, exactamente, un «paquete anycid»

### 1.1 Contenido

El fork trae en `anycid/` **un paquete por plataforma y un paquete por firmware**:

- `anycid/DB2020.zip` — paquete de plataforma, común a todo DB2020 (W580, W660,
  K800, K810, K850…). `[seftool]`
- `anycid/R8BE001_<CDA>_<MERCADO>.zip` — paquete por firmware. Para W580 el
  firmware canónico es **R8BE001** y están todos los CDAs que la escena publicó
  (CXC1123326, CXC1123362, CXC1250374, CXC1250446, CXC1250640,
  `1201-7587_GENERIC_VI`). `[seftool]`

Descomprimiendo `DB2020.zip`:

```
tpa/preset/custom/customize_upgrade.xml
tpa/preset/custom/executor.jar          (43 534 bytes)
```

Descomprimiendo `R8BE001_CXC1123362_GENERIC_LA.zip`:

```
tpa/preset/custom/executor.b            (3 692 bytes)
```

### 1.2 Qué dice el XML (literal)

```xml
<customization>
  <java>
    <midlet deletable="true" upgradeable="true" install="true" folder="games">
      <jar-file>executor.jar</jar-file>
    </midlet>
  </java>
  <file-operations>
    <file-op parse-mask="fa" op="delete" dest="/tpa/preset/system/ams/j26dc2c0c0597f84987531f687b2c9db2c6ee1dab.b"/>
    <file-op parse-mask="fa" op="move"   source="/tpa/preset/custom/executor.b"
                                         dest="/tpa/preset/system/ams/j26dc2c0c0597f84987531f687b2c9db2c6ee1dab.b"/>
  </file-operations>
</customization>
```

Lo que ordena el XML `[publicado]` `[inferido]`:
1. **Instalar `executor.jar` como MIDlet en la carpeta Juegos**, marcándolo
   como visible y actualizable: lo normal de un «customize_upgrade».
2. **Borrar el fichero AMS del sistema** `j26dc…1dab.b` (nombre tipo
   «jad-sha1-of-origin.b»: un objeto nativo que el OS A200 asocia a cierto
   MIDlet por su hash) y **sobreescribirlo con `executor.b`**, moviéndolo
   desde `/tpa/preset/custom/`.

El efecto neto: cuando el usuario abre «Juegos → executor», el runtime
Java del A200 carga el MIDlet, el MIDlet pide la rutina nativa y el AMS
le devuelve **la rutina nativa de `executor.b`**, no la original. Esa
rutina es el verdadero payload anycid.

### 1.3 Qué es `executor.b` como fichero

Cabecera del fichero `[inferido del volcado]`:

```
offset 0x00: CA FE B0 0B                 magia (OSE 2 «BeOS», formato de módulo del A200)
offset 0x04: 00 9F 21 31                 versión + flags
offset 0x08: 44 40 79 24                 ¿CRC o estampilla?
offset 0x0C: 00 00 00 00
offset 0x10: 64 00 00 04                 n.º de secciones + punteros a tabla
...
offset 0x40..: código Thumb (11 4B F8 B5 04 00 06 AF ...)
```

El formato es el **objeto cargable AMS** del Application Management System
del A200 (el «OSE Delta Binary»). Lo carga el AMS por su propio enlazador
dinámico, resolviendo símbolos contra el OS A200 ya en marcha. **No es un
binario ARM crudo**: no vale `objcopy -O binary` y saltar a offset 0. `[publicado]`

El código Thumb de `+0x40` empieza con el patrón clásico de un stub
A200 que llama por `BX r9` a servicios del sistema (el `04 4B 9F 46 …`
que se ve en la mitad del fichero). `[inferido]`

### 1.4 Dónde se escribe en la flash NOR del aparato

**No se escribe directamente a la NOR**. seftool entra en modo servicio,
sube los flashloaders CID53 firmados, levanta el servidor **FSX** del
csloader (el servicio FS del A200), y copia los dos zips **al sistema de
ficheros FFX** del teléfono — la misma mitad que el A200 llama «filesystem»
y que vive en `0x45E00000..0x47DFFFFF` según el mapa del main. `[firmware]`

Los ficheros caen en:
- `/tpa/preset/custom/customize_upgrade.xml`
- `/tpa/preset/custom/executor.jar`
- `/tpa/preset/custom/executor.b`

Al rearrancar el teléfono con SIM, el subsistema de personalización del A200
lee `customize_upgrade.xml`, instala el MIDlet y mueve el `.b`. **El main
en NOR no se toca.** `[publicado]`

### 1.5 Qué parte del firmware llama al payload

El payload corre **como código nativo cargado por el AMS del A200**:

- Momento: cuando el usuario abre el MIDlet. Durante el arranque frío
  (`docs/arranque.md` §2–§8) el payload **no corre**: lo lanza una
  interacción de usuario desde el menú Juegos. `[publicado]`
- Modo: **Thumb, modo USR**, con la MMU encendida y las cachés encendidas,
  usando la **tabla de páginas del A200** (`TTBR0 = 0x20E64000`,
  `DACR = 1`). `[firmware]`
- Pila: la pila del proceso Java/AMS que el OSE le asigna. Pequeña (del
  orden de kilobytes). `[inferido]`
- Entrypoint: la tabla de secciones del `.b`; el AMS resuelve la entrada
  por el enlazador dinámico. No hay «dirección de salto» absoluta: el
  AMS lo relaja al cargarlo. `[inferido]`

### 1.6 Qué servicios del A200 siguen vivos

**Todos**: el payload corre *encima* del OSE del A200. Están vivos
`[inferido]`:

- Scheduler OSE y mensajería (`send_sig`/`receive`).
- `debug_printf`, el RAMLOG (`0x20FFF000`) y el servidor de trazas.
- Drivers DDA: LCD, teclado, GPIO, PMU, batería, perro guardián.
- Pila BT (si está encendida), audio, UART.
- El propio AMS: puede abrir ficheros del FFX, pedir memoria al
  `ose_memory_mgr`, crear procesos nuevos (si tiene el permiso).

### 1.7 Memoria disponible para el payload

**Esto solo lo contesta un aparato real** `[sin aparato]`. Lo que se sabe
por el mapa del firmware (`docs/arranque.md` §4) `[firmware]`:

- RAM del main (OSE + procesos): `0x20000000..0x20E80000` (≈ 14,5 MiB).
  El AMS pide al `ose_memory_mgr` un pool para procesos de usuario; el
  tamaño típico de un `heap_create` para un MIDlet en A200 ronda 1–2 MiB,
  pero **el AMS permite reservar bloques nativos adicionales** desde
  el `.b`. Lo medido en K800 ronda 3–4 MiB disponibles para el stub de
  anycid.
- Memoria gráfica (`EXT_GRAM` `0x20E80000`+`0x180000`) está ocupada por
  el framebuffer: **no se puede usar como RAM libre**.
- La NOR (`0x44000000..0x47FFFFFF`) está mapeada entera, pero la zona
  del main es `su_ro_usr_na` para procesos USR: **no se puede ejecutar
  código propio desde ahí sin pasar por el AMS**.
- El tamaño del `.b` sí tiene techo: el AMS valida que el objeto quepa
  en el bloque de carga que le reserva. El `executor.b` original mide
  **3 692 bytes**, lo que marca una cota baja observada. La cota alta
  real de un `.b` cargable en el A200 del W580 es `[sin aparato]`; en
  la escena K800/K810 se han cargado objetos de hasta ~128 KiB.

### 1.8 Lo que hace el `executor.b` del anycid público

**Lo que se sabe por el patrón del código y por la receta de la escena**
`[publicado]` `[inferido]`:

1. El MIDlet Java llama a su rutina nativa (resuelta por el AMS a nuestro
   `.b`).
2. El `.b` toma el servicio nativo y: **parchea en RAM** las comprobaciones
   de CID que hace la próxima vez que se arranque el flashloader o el
   csloader, o deja un **gancho** en el A200 para que los siguientes
   mensajes del protocolo serie se contesten como si el CID fuera 49.
3. En algunas recetas el `.b` escribe también a GDFS un «marcador»
   para que el teléfono quede «desbloqueado» entre arranques.
4. **No reemplaza el firmware**: cuando el usuario reinicia, el A200
   vuelve a ser el que estaba en NOR; el truco consiste en que, mientras
   el stub siga residiendo en el `.b` cargado al abrir el MIDlet, las
   siguientes operaciones de seftool pasan sin firmar.

En DB2020, lo que publica seftool upstream (`src/emp/v3/action.c` líneas
297–354) `[seftool]` es exactamente este flujo: subir los dos zips vía
FSX. **No sube un firmware alternativo.**

---

## 2. Lo que se midió en la imagen R8BE001

### 2.1 Fichero y procedencia

`w580_r8be001_main.bin`, 30 146 560 bytes (28,75 MiB),
mapeado a `0x44140000..0x45E7FFFF`. Es la imagen cruda del main; el
resto hasta `0x48000000` lo rellenan FS (`0x45E00000..0x47DFFFFF`) y
GDFS (`0x47E00000..0x47FFFFFF`). `[firmware]`

### 2.2 De dónde parte y qué deja listo el firmware

Del `docs/arranque.md` del árbol senew (ya volcado contra la imagen)
`[firmware]`:

- Vectores a `0x44140000`; `reset` a `0x44140AC4`; crt0 a `0x44140C64`.
- A la hora del payload la MMU está **encendida**, con `TTBR0 =
  0x20E64000`, `SCTLR = 0x0009127F`, `DACR = 1`.
- Cachés de instrucciones y datos encendidas.
- Zona de **RAMLOG** disponible en `0x20FFF000` para dejar trazas al
  propio `.b` (si el AMS deja escribir ahí al proceso usuario, cosa que
  depende de la DACR del dominio: `[sin aparato]`).
- Primitivas útiles cercanas que ya están en RAM: `debug_printf`
  (`0x44236C50`), `vsnprintf` (`0x44557C88`). El AMS las exporta como
  símbolos resolubles por el enlazador dinámico `[inferido]`.

### 2.3 Punto exacto desde el que el payload empieza a correr

**No es una dirección fija del binario**: es un callback resuelto en
caliente por el AMS cuando el MIDlet llama a su método nativo. El AMS
mantiene una tabla de módulos cargados (`ams/*.b`) indizados por el
**hash del `.jad`/`.jar`** (`j26dc2c0c0597f84987531f687b2c9db2c6ee1dab`
es ese hash, Base16 de SHA-1 de la cadena de identidad del MIDlet).
`[inferido]`

Lo que **sí** se puede fijar en nuestro payload senew-pequeño: el punto
de entrada estático del `.b`, que el enlazador deja al principio del
`.text` del objeto.

### 2.4 Primitivas útiles ya mapeadas

- `debug_printf` → RAMLOG + UART de trazas. **Es la vía de impresión**.
- `ose_memory_mgr` → `heap_alloc`/`heap_free` para pedir memoria.
- `semcfs/ffx` → acceso a ficheros del FFX (para leer o escribir el
  bloque de ajustes del propio senew-pequeño si hiciera falta).
- Primitivas de LCD (`PFLINK_PDICON`, framebuffer `0x20E80000`). Si el
  AMS deja al proceso USR tocar el PDICON **directamente**, se puede
  pintar: `[sin aparato]`. Si no, hay que pasar por los servicios
  gráficos del A200.

### 2.5 La MMU

Como ya está encendida y la DACR del dominio del AMS está puesta por el
A200, el payload **no toca CP15**. Un payload senew-pequeño que necesite
cachés separadas o cambiar `DACR` **tiene que** apagar el A200 antes
(opción B).

---

## 3. Cómo encaja senew: dos opciones

### 3.1 El marco

senew hoy (ver `senew-w580/`): un main propio que **arranca en
frío desde `0x44140000`** y toma el control del DB2020. Su `arranque.S`
inicializa pilas, cachés, TCMs y la SDRAM. Vive con `TTBR` propio, perro
guardián propio, drivers propios.

**El flujo anycid público no permite reemplazar el firmware**: no hay
RSA break conocido para DB2020 (sólo `break49/` para DB2000/DB2010).
Después de `--anycid`, los flashloaders CID53 **siguen pidiendo
firmware firmado por SE**: el único main.mbn que entra es R8BE001 (u
otro firmware oficial del catálogo W580). `[publicado]` `[seftool]`

Por tanto senew en W580 CID53 **no puede ser lo que es hoy en W600
CID49** (reemplazo frío con `break49`). Hay dos salidas realistas, y
una tercera que es la honesta:

### 3.2 Opción A — senew-hospedado (dentro del A200)

senew se **reduce a una app** cargada por el AMS, como el propio
executor público. Su `.b` + `.jar` entran por el mismo flujo anycid.

- **Tamaño del payload:** limitado a lo que el AMS acepte en un `.b`.
  El `executor` canónico mide 3,6 KiB; en K800 se han visto `.b` de
  decenas de KiB. **Cota práctica estimada**: < 128 KiB. `[inferido]`
  senew entero (`build/sistema.bin` para W580 pesa ~1 MiB con shell,
  ui, apps y r3d) **no cabe**.
- **Cambios en senew**: enormes. Hay que sacrificar `arranque.S`
  entero, `enlace.ld` (no arranca en `0x44140000`, lo reubica el AMS),
  `hal/` (gpio, i2c, nor, kbd, lcd, reloj, intcon → pasa todo por
  llamadas al A200), `plat/` (ya no es dueño del hardware), el perro
  guardián, el reinicio, la MMU. Lo que queda es `shell/` + `ui/` +
  `apps/`, y aun así `ui` tiene que pintar a través del PDICON del
  A200.
- **Garantías que se pierden:** control del arranque, el perro propio,
  la MMU propia, el schedule propio, la UART propia, la persistencia
  propia (W6FS: tiene que vivir en el FFX del A200, no en NOR cruda).
  En la práctica senew deja de ser «senew» y pasa a ser **una app
  SEMC con cara de senew**.
- **Riesgo del aparato:** mínimo. El A200 original sigue en NOR; si
  el payload falla, se desinstala el MIDlet y el teléfono vuelve a
  ser un W580 SEMC. **No bricka**.

### 3.3 Opción B — senew-second-stage (take-over desde el AMS)

El `.b` que entra por anycid es un **mini-bootloader** cuyo trabajo es:

1. Pedir memoria al A200.
2. Leer de FFX (o del propio NAND/NOR vía servicios A200) un **blob
   senew** que **se instaló previamente** por FSX (como un fichero más
   del FFX).
3. **Apagar el A200 con cuidado**: cerrar las IRQ (`CPSR.I/F = 1`),
   invalidar DMAs vivos, bajar el PMU a un estado conocido, apagar la
   SDRAM si procede, bajar el reloj.
4. **Reubicar senew** desde el blob a la zona de RAM que elija (p. ej.
   `0x20A00000`) o copiarlo a la ITCM/SDRAM y saltar.
5. Reinicializar MMU y cachés como senew sabe hacer en frío
   (`hal/arranque.S` de senew).
6. Entrar en senew.

- **Tamaño del `.b`:** solo el stub. Objetivo < 32 KiB, holgado dentro
  de la cota práctica del AMS.
- **Tamaño del blob senew en FFX:** 1–3 MiB; el FFX del W580 son 36
  MiB (`0x45E00000..0x47DFFFFF`), de los que el A200 usa una parte;
  entra sobrado.
- **Cambios en senew**: `hal/arranque.S` se parte en dos: el camino
  frío (hoy, 0x44140000) y un **camino caliente** desde la dirección
  reubicada en SDRAM. El `enlace.ld` necesita un flag nuevo
  `W580_CID53_PAYLOAD` que (a) mueve ORIGIN(FLASH) al punto de carga
  en SDRAM, (b) convierte las secciones `.vectores` y `.text.arranque`
  en código **ya en RAM**, (c) elimina el bloque EMIF (no sirve en
  SDRAM), (d) ajusta `_data_flash` a `_data` (todo ya está en RAM al
  cargarse el blob).
- **Garantías que se pierden:** mientras senew no vuelva a programar el
  perro guardián en su llave `[firmware]` (`FUN_441ad0cc`, `0xF9010010
  ← 0xAA` …), si el A200 lo tenía puesto, puede morderlo. Hay que
  **matar el perro** antes de bajar el A200 (parchear el servicio o
  echarle de comer indefinidamente desde el payload antes del salto),
  cosa que depende del aparato: `[sin aparato]`.
- **Riesgo:** el payload corre encima de un A200 vivo con DMAs,
  temporizadores y audio potencialmente en marcha. **Apagar mal el
  A200 puede dejar el teléfono reiniciando**; pero como la NOR no se
  tocó (el A200 original sigue a `0x44140000`), un **reset físico lo
  recupera**: no bricka.

### 3.4 Opción C — no usar anycid para senew en CID53

Decirlo claro: la ruta pública no fue pensada para reemplazar el OS, y
reutilizarla para eso introduce una fragilidad que **no está en
senew-W600 CID49**. La vía honesta cuando llegue un W580 CID53 es:

1. Verificar el flujo anycid tal cual (los pasos 1–5 de `CID53_RUTA.md`
   §4).
2. **No instalar senew ahí**. Dejar el W580 CID53 como teléfono SEMC
   con el bypass para pruebas de laboratorio.
3. Priorizar W580 CID49/51/52 para senew.

Esta opción es la que corresponde a la regla «el fork preserva, no
explora» (`CID53_RUTA.md` §6). Si el objetivo es que **todo W580 pueda
correr senew**, hace falta A o B. Si el objetivo es «no romper ningún
aparato del mantenedor», C.

### 3.5 Recomendación

**Opción B, encuadrada como piloto, no como producto.**

Razones:

- **A** mata senew como proyecto: sin arranque propio, sin HAL, sin
  persistencia propia, no es un sistema sino una app SEMC. El trabajo
  de los últimos meses en `w580/sistema/` (NOR propio, W6FS, el shell,
  r3d, mecano) **se pierde entero**.
- **B** permite mantener **todo senew tal como está**, con un camino
  de arranque adicional. El `.b` del stub es un trabajo acotado (unas
  pocas semanas) y comparte código con `hal/arranque.S` del main
  propio (apagar/encender cachés, programar MMU, saltar).
- **B** se puede probar **sin flashear**: subir el `.b` por anycid y
  ver si llega al salto a senew, con senew firmando por RAMLOG antes
  del `panic` que vendrá de los drivers tocando pines ya configurados
  por el A200. Es iterativo.
- **C** sigue siendo la respuesta correcta si el primer intento de B
  cuelga el teléfono más de un reset: el aparato de pruebas CID53 es
  caro.

**Qué queda estrictamente fuera de la recomendación:**

- Cualquier cosa que exija firma RSA de senew para la NOR del DB2020.
  Si algún día alguien publica un break DB2020, se replantea; hoy no.
- Modificaciones a la NOR del W580 CID53. **El main original se queda
  en `0x44140000` para siempre** (en opción B). senew vive en SDRAM.

---

## 4. Qué queda por medir en el aparato

Lista única para `docs/pendiente_del_aparato.md` cuando llegue el W580
CID53:

1. **Instalación limpia** del anycid (pasos 1–5 de `CID53_RUTA.md`).
2. **Tamaño máximo del `.b`** que el AMS acepta cargar como payload en
   `/tpa/preset/system/ams/`. Técnica: aumentar `executor.b` con
   padding incremental y ver cuándo deja de arrancar.
3. **Memoria libre** que el A200 entrega al proceso del MIDlet.
   Técnica: desde el stub, pedir `heap_alloc` incremental hasta
   fallo; imprimir por RAMLOG.
4. **Si el proceso USR tiene permiso de escribir el PDICON**. Si no,
   el UI de senew en opción B tiene que pintar pasando por el A200
   hasta que apaguemos el A200.
5. **Cómo mata el payload al perro guardián** sin que el A200 nos mate
   antes: ¿da tiempo a parchear la rutina de alimentación en RAM antes
   del salto, o hay que hacerlo con las IRQ ya cortadas?
6. **Qué DMAs están vivos** y cómo pararlos sin colgar el PMU.
7. **Si el A200 permite al proceso USR cambiar `CPSR`** (seguramente
   no: hay que entrar a SVC por un fallo controlado, otra puerta).

---

## 5. Qué queda en código ahora y qué queda solo en documento

**En código, en una copia** (`cid53_payload/sistema/`,
no toca `sistema` vivo):

- `placas/w580i-cid53/placa_conf.h` — placa nueva o flag
  `W580_CID53_PAYLOAD` apagado por omisión.
- `hal/enlace_payload.ld` — mapa alternativo (sin FLASH `0x44140000`,
  todo en SDRAM).
- `hal/arranque_payload.S` — camino caliente: sin reset de SDRAM, sin
  perro, sin EMC, va directo a `main()` suponiendo pilas ya puestas
  por el stub.
- `plat/main_payload.c` — camino caliente real (fase 3): marcadores
  en SDRAM, pintado al framebuffer en 0x20E80000, `debug_printf` por
  dirección directa (0x44236C50 para R8BE001), alimentación del perro
  en 0xF9010010 y bucle de panel. No salta a senew main() todavía;
  la línea `senew_main()` queda comentada para la fase 4b.
- `Makefile`: regla `make w580-cid53-payload` que compila el .S + .c,
  enlaza contra `enlace_payload.ld` (SDRAM 0x20A00000), vuelca a .bin
  crudo y lo empaqueta con `tools/hacer_b.py` del fork seftool-senew
  usando una CXC de R8BE001 como plantilla.

**Empaquetado `.b` resuelto en este ciclo (fase 2):**

- `docs/formato_b.md`: estudio del envoltorio OSE Delta Binary
  (`cafeb00b`) a partir de los 6 `executor.b` del fork. 64 B de
  cabecera, cuerpo Thumb-2 desde 0x40, 126 literales por CXC.
- `tools/hacer_b.py`: empaquetador. Reutiliza la cabecera de la
  plantilla (incluida la palabra 0x04 candidata a checksum) y
  sustituye el cuerpo. Prueba de regresión `--verificar` sobre las 6
  plantillas pasa byte a byte.

## 6. Instalación del payload (sin ejecutar, para martes)

El flujo de **sustitución** del `executor.b` canónico por el nuestro,
pensado para correr contra un W580 CID53 real cuando esté en el banco:

### 6.1 Compilar el `.b` propio

```
cd cid53_payload
make clean
make w580-cid53-payload PLANTILLA_CXC=R8BE001_CXC1123362_GENERIC_LA
```

Variables útiles del Makefile:

- `PLANTILLA_CXC`: cuál de las 6 CXC publicadas usamos como envoltorio.
  Por omisión `R8BE001_CXC1123362_GENERIC_LA`. **Hay que elegir la
  CXC del aparato que tengamos delante** (lo imprime seftool al
  identificar el teléfono). El pool de literales del Thumb (hoy
  mínimo: solo `0x44236C50` para `debug_printf`) se tiene que
  recompilar con las direcciones que correspondan a esa CXC concreta
  — para R8BE001 todas las CXC tienen las mismas direcciones RAM del
  main según `docs/arranque.md`, así que la plantilla influye en el
  envoltorio pero no en el pool, por ahora.
- `PLACA`: `w580i-cid53` (único soporte en este árbol).

Salida: `build/w580i-cid53/senew_payload.b`, siempre de 3692 B.

### 6.2 Preparar el ZIP anycid con nuestro `.b`

```
cp seftool-senew/anycid/R8BE001_CXC1123362_GENERIC_LA.zip \
   /tmp/senew_anycid.zip
# Sustituir el executor.b dentro del zip SIN tocar el XML ni el .jar:
cd /tmp && mkdir -p anycid_senew && cd anycid_senew
unzip /tmp/senew_anycid.zip
cp cid53_payload/build/w580i-cid53/senew_payload.b \
   tpa/preset/custom/executor.b
# Recomprimir conservando la ruta:
zip -r /tmp/senew_anycid.zip tpa/
```

El `DB2020.zip` del fork (`anycid/DB2020.zip`) **se usa sin cambios**:
contiene el `executor.jar` y el `customize_upgrade.xml` que ordenan
instalar el MIDlet y mover el `.b` a `/tpa/preset/system/ams/
j26dc…1dab.b`. Nosotros solo cambiamos el cargamento del `.b`.

### 6.3 Pasarle los dos zips a seftool-senew

El salto de firma CID53 lo hace **el `executor.jar` que ya trae el
fork** (`anycid/DB2020.zip`), no nuestro código. Nosotros solo
intervenimos en el `.b` que corre después:

```
cd seftool-senew
./build/seftool-senew \
    --port /dev/ttyUSB0 \
    --anycid \
    --db2020-pkg anycid/DB2020.zip \
    --fw-pkg    /tmp/senew_anycid.zip
```

(La bandera exacta la fija `src/emp/v3/action.c` del fork; en este
documento es solo una ilustración; no se ejecuta.) seftool:

1. Entra al modo servicio del DB2020 por el cable.
2. Sube flashloaders firmados CID53.
3. Levanta FSX del csloader.
4. Copia los dos zips al FFX del aparato en `/tpa/preset/custom/`.
5. Reinicia el teléfono.

### 6.4 Qué espera el martes

Al reiniciar el teléfono con SIM:

- El subsistema de personalización del A200 lee
  `customize_upgrade.xml`, instala el MIDlet «executor» en Juegos y
  mueve nuestro `senew_payload.b` a `/tpa/preset/system/ams/
  j26dc…1dab.b`.
- Al abrir el MIDlet, el AMS carga NUESTRO `.b`. Qué pasa después
  está en la lista `[a medir en el aparato]`:
  1. ¿El AMS acepta el `.b` o lo rechaza por la palabra 0x04
     (checksum)?  →  si lo rechaza, hay que descifrar el algoritmo
     (fase posterior; nada en este ciclo lo decide).
  2. Si lo acepta, ¿llega la ejecución a `_inicio_payload` /
     `reset_payload` / `main_payload`?  →  los 4 marcadores
     (`0x20FFFFF0..0x20FFFFFC` con firma `0x5E57A710`) lo dicen al
     volcar RAM.
  3. ¿Vemos el cuadro verde arriba-izquierda?  →  si sí, el proceso
     USR escribe EXT_GRAM; si no, hay que pasar por PFLINK_PDICON.
  4. ¿Llega la línea «senew arrancando» por la UART de trazas?  →
     dice si `debug_printf` del main vive y es llamable desde el AMS.
  5. ¿El perro aguanta sin morder?  →  cadencia del bucle a medir.

### 6.5 Recuperación

El main original del W580 CID53 **sigue intacto en NOR** en todo este
flujo (ver §3.3 opción B): si el payload cuelga, un **reset físico**
devuelve el teléfono a R8BE001 original. **No bricka.** Para
desinstalar el MIDlet basta con un `customize_upgrade.xml` nuevo que
mueva un `executor.b` neutro (el canónico del fork, p. ej.) y
reinstale el MIDlet original. En último caso, el flasheo normal CID53
con firmware R8BE001 oficial restituye todo.

**No se ejecuta nada de §6.1–§6.4 en este ciclo.** Documento vivo
para el martes, cuando el aparato esté en la mesa.

---

## 6. Resumen honesto

- El flujo anycid **no sirve para flashear senew como firmware**
  en W580 CID53: no hay RSA break DB2020.
- La única manera de meter senew propio ahí es **cargarlo encima** del
  firmware SEMC, bien como app (**A**, pierde senew), bien como stub
  que apaga el SEMC en vivo y arranca senew en SDRAM (**B**, piloto).
- La prudencia (**C**) también es una opción válida y es la que marca
  `CID53_RUTA.md` §6 por omisión: no se escribe senew a un CID53 hasta
  que los pasos 1–5 estén firmados.
- La rama `cid53-pending` del fork no cambia su estado por este
  documento: sigue siendo `pending`. El par W580/CID53 pasa a
  `pending_verification`, y la elección entre A, B o C se congela
  hasta tener aparato.
