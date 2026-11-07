# Formato `.b` de anycid (OSE Delta Binary para AMS del A200 — DB2020)

Documento de **ingeniería inversa limitada** sobre el envoltorio `.b` del
flujo anycid del DB2020 red, derivado del único corpus público al que
tenemos acceso: los 6 ficheros `executor.b` que trae el fork
`seftool-senew` en `anycid/R8BE001_*.zip`, todos para el firmware R8BE001
del W580i.

Marcas de confianza:
- `[observado]` — se ve en los bytes de los 6 ficheros al cruzar unos
  contra otros.
- `[inferido]` — lectura cruzada con el firmware R8BE001 y con el
  comportamiento esperado del AMS (OSE/A200).
- `[sin saber]` — pregunta abierta.

---

## 1. Corpus

| Fichero                                   | Tamaño | SHA256 (12 primeros) |
|-------------------------------------------|--------|----------------------|
| R8BE001_1201-7587_GENERIC_VI.b            | 3692   | `abfb0a1f6072` |
| R8BE001_CXC1123326_GENERIC_LY.b           | 3692   | `1556c4227397` |
| R8BE001_CXC1123362_GENERIC_LA.b           | 3692   | `e1750845cc2d` |
| R8BE001_CXC1250374_GENERIC_TH.b           | 3692   | `ee7e20006058` |
| R8BE001_CXC1250446_GENERIC_FY.b           | 3692   | `f7d9e838c350` |
| R8BE001_CXC1250640_GENERIC_AI.b           | 3692   | `fa0918042c05` |

Mismo tamaño exacto (3692 B) en los 6. `[observado]` La identidad del
tamaño cruza todos los CXC del mismo firmware base R8BE001, lo cual
encaja con que el **tamaño lo marca el firmware que carga el `.b`**, no
la rama Java que lo pide: todos los W580i R8BE001 reservan el mismo
bloque AMS. `[inferido]`

Diferencias byte a byte entre los 6: **399 bytes diferentes, agrupados
en 143 tiradas**; de ellas **126 son tiradas de 3 bytes en los offsets
0..2 de una palabra alineada a 4** (patrón LE con byte alto constante
`0x44`), y **2 son del mismo patrón pero en los offsets 1..3** (patrón
BE o palabra «marcada»). Las 15 tiradas restantes están concentradas en
la cabecera (ver §3). `[observado]`

## 2. Estructura general

```
+--------+---------------------+
| 0x00   | Cabecera (64 B)     |
+--------+---------------------+
| 0x40   | Código Thumb        |   ← empieza con 11 4B F8 B5 ...
|        | + tabla de literales|
| …      | + .rodata / símbolos|
+--------+---------------------+
| EOF    | 3692 B total        |
+--------+---------------------+
```

El código empieza en el offset **0x40** `[observado]`: el patrón
`11 4B F8 B5` es `ldr r3, [pc, #68]` seguido de `push {r3-r7, lr}`,
preludio canónico de rutina Thumb-32 con pool de literales a +0x44 del
PC. `[inferido]`

## 3. Cabecera (0x00..0x3F)

| Off  | Bytes (R8BE001_CXC1123326)    | Significado inferido |
|------|-------------------------------|----------------------|
| 0x00 | `ca fe b0 0b`                 | Magia `cafeb00b` (big-endian). `[observado]` Constante en todo el corpus. `[inferido]` Identifica el envoltorio OSE Delta Binary / módulo AMS del A200. |
| 0x04 | `00 9f 21 31`                 | **Varía** entre CXC. Byte alto `0x00` constante en los 6. `[observado]` Los otros 3 bytes son esencialmente aleatorios. **Candidato a CRC-24 / checksum / seed** del resto del módulo. `[inferido]` |
| 0x08 | `44 40 70 84`                 | **Varía** entre CXC. Byte alto `0x44` constante → dirección en la NOR del main. `[observado]` Al cruzar las 6 CXC los valores caen en `0x44400000..0x4453ffff` y `0x44524f6c..0x4453b900`, dentro del rango del main R8BE001 (`0x44140000..0x45E7FFFF`). `[inferido]` Puntero a sección del propio AMS del firmware (p.ej. callback de carga, símbolo de resolución, o entrada del índice de módulos). **No es un offset dentro del `.b`**: los valores están muy por encima de 3692. |
| 0x0C | `00 00 00 00`                 | Constante en los 6. `[observado]` Padding o puntero nulo. |
| 0x10 | `64 00 00 04`                 | Constante en los 6. `[observado]` `[sin saber]`: posible flag («versión 100 (0x64), 4 secciones»); también encaja con una longitud BE de 1.677.721.604, lo cual descarta esa lectura. La interpretación limpia es **dos campos u16**: `0x6400` y `0x0004`. |
| 0x14 | `00 00 20 00`                 | Constante. `[observado]` 0x2000 = 8192. Probable **alineación / tamaño reservado de bloque** (el AMS redondea el módulo a 8 KiB en RAM aunque pese 3692 B). `[inferido]` |
| 0x18 | `00 00 0d 0c`                 | Constante. `[observado]` 0x0d0c = 3340. **Posible tamaño del área de código útil** (3692 − 0x40 cabecera − algo de cola = 3340 ± cola). `[inferido]` |
| 0x1C | `00 00 00 1c`                 | Constante. `[observado]` 0x1c = 28. **Posible offset del inicio de la primera entrada de la tabla de secciones**, contando desde 0x1C. 0x1C+0x1C = 0x38 cae en la cabecera; también puede ser cantidad. `[sin saber]`. |
| 0x20 | `00 00 00 01`                 | Constante. Contador o versión-del-registro. `[inferido]` |
| 0x24 | `00 00 00 00`                 | Constante. Padding. `[inferido]` |
| 0x28 | `40 00 00 0e`                 | Constante. `[observado]` 0x4000000e. **Candidato a flag + tag**: `0x40` como flag «ejecutable», `0x000e` como tag/id de sección (14). `[inferido]` |
| 0x2C | `00 00 00 01`                 | Constante. `[inferido]` |
| 0x30 | `00 00 00 00`                 | Constante. `[inferido]` |
| 0x34 | `40 00 00 4c`                 | Constante. `[observado]` 0x4000004c. Mismo patrón que 0x28: flag + offset. **0x4c = 76**, lo cual podría ser el offset absoluto del siguiente ítem contando desde algún anclaje; también 0x4c = 76 = 0x40 (código) + 12 → **inicio del cuerpo del código Thumb tras los 3 primeros halfwords de preludio**. `[sin saber]` |
| 0x38 | `00 00 00 00`                 | Constante. `[inferido]` |
| 0x3C | `00 00 00 01`                 | Constante. `[inferido]` |

**Resumen operativo:**
- La cabecera es **constante** entre CXC del mismo firmware R8BE001
  **salvo dos palabras**: offset 0x04 (checksum/CRC) y offset 0x08
  (una dirección NOR por CXC). `[observado]`
- Toda la variabilidad por CXC de la cabecera cabe en 6 bytes.
- El tamaño del `.b` (3692 B) es idéntico, así que la geometría del
  envoltorio no cambia; **para nuestro empaquetador basta con generar
  un cuerpo del mismo tamaño y rellenar las dos palabras variables**.

## 4. Código y tabla de literales (0x40..EOF)

- Empieza en 0x40 con `11 4b f8 b5` (`ldr r3, [pc, #68]; push {r3-r7, lr}`),
  modo Thumb-2. `[observado]`
- A lo largo del resto del fichero hay **126 huecos de 32 bits con el
  byte alto fijo `0x44`** (los otros 3 varían por CXC), **alineados a
  palabra**. `[observado]` Interpretados como **enteros little-endian**
  son valores `0x44xxxxxx`, que caen en el rango de la NOR del R8BE001
  (`0x44140000..0x45E7FFFF`). `[inferido]`
- **Hipótesis fuerte**: son **literales con direcciones de funciones y
  variables del firmware R8BE001** — el pool clásico que un Thumb lee
  con `ldr rN, [pc, #imm]`. El AMS **no hace enlace dinámico de estos
  símbolos**: cada `.b` ya trae las direcciones **resueltas** para su
  CXC concreto. `[inferido]`
- Esto explica tres cosas que no se podían explicar antes:
  1. Por qué un `.b` publicado para una CXC no debería arrancar en otra
     (los literales apuntan al main equivocado).
  2. Por qué el fork publica un `.b` por CXC y no uno solo.
  3. Por qué `docs/cid53_payload.md` §2.4 dice que `debug_printf` está
     en `0x44236C50` y `vsnprintf` en `0x44557C88`: son dos de esos 126
     literales.
- Las 2 tiradas con patrón BE (bytes 1..3 variables, byte 0 constante)
  — en offsets 0x04 y 0x08 — son las dos palabras BE de la cabecera
  descritas en §3.

## 5. Qué cambia por CXC y qué no (recapitulación)

**Varía por CXC:**
- Offset 0x04..0x07: 24 bits + byte alto `0x00` (candidato a checksum).
- Offset 0x08..0x0B: 24 bits + byte alto `0x44` (puntero NOR a sección
  del AMS).
- **126 palabras del pool de literales** repartidas en el cuerpo
  Thumb, cada una con byte alto `0x44` constante y los 24 bits bajos
  variables.

**Constante entre CXC del mismo firmware R8BE001:**
- Resto de la cabecera (26 B constantes de los 64).
- Todo el código Thumb (los opcodes). Los pools varían, el código no.
- Tamaño del fichero: 3692 B.

## 6. Qué es necesario para generar un `.b` propio

Para la **opción B** del `docs/cid53_payload.md` necesitamos un `.b`
que lleve nuestro stub de salto a senew. En vez de descifrar el formato
exacto, aprovechamos que **la geometría del `executor.b` canónico ya
funciona**:

1. Partimos de uno de los `.b` R8BE001 publicados como **plantilla
   fija de 3692 B**.
2. Sobre-escribimos el bloque de código Thumb (de 0x40 hasta el final
   del área de código, antes del pool de literales de cola) con **nuestro
   stub**, montado por el toolchain ARM y empaquetado como blob
   Thumb-2 **sin pool propio** (toda referencia a direcciones del
   firmware la pondremos directamente como `.word 0x44xxxxxx` dentro
   del fuente ensamblador o del `.c`, para que acabe en los sitios
   donde el `.b` original también los tenía).
3. Recalculamos la palabra 0x04 **como una suma de comprobación simple
   del resto del `.b`** si así resulta serlo; si no (hipótesis
   alternativa: firma HMAC, CRC-24 propio) **dejamos la palabra tal
   cual** y aceptamos que **validar el `.b` en el aparato** nos dirá
   si el AMS lo rechaza o lo deja pasar. Punto `[sin aparato]`.
4. Dejamos la palabra 0x08 **con el valor del `executor.b` canónico de
   la misma CXC**, porque pensamos que apunta a estructuras del
   firmware que no sabemos reproducir. No hace falta cambiarla: el
   AMS la usa para resolver el callback, no para distinguir
   payloads. `[inferido]`

Este plan **no reconstruye el formato OSE desde cero**, cosa que
tomaría meses y requiere documentos internos de SE/OSE que no están en
abierto. Lo que hace es **reutilizar el envoltorio público** y
sustituir el *cargamento* por el nuestro.

## 7. Búsqueda abierta

«cafeb00b» + «OSE Delta Binary»: no hay publicación formal del formato.
El tratamiento más completo que localizamos en abierto son:

- Los foros de la escena SE 2008-2011 (XDA, se-nse, hackint0sh,
  esato) donde se publicó el flujo anycid; **ninguno describe el
  envoltorio byte a byte**. El código del `executor.b` se trata como
  blob opaco.
- El propio `seftool` upstream no toca el `.b`: lo sube tal cual. No
  hay parser.
- La huella `cafeb00b` también la usan clases de Java compiladas y
  algún artefacto Qualcomm sin relación con OSE/A200. Ningún
  parecido explotable.

Conclusión: **para este trabajo tratamos el envoltorio como opaco y
reemplazamos el cargamento**. Lo contrario (parser propio) queda como
proyecto independiente de preservación histórica.

## 8. Lo que falta medir con aparato

- Palabra 0x04: ¿checksum, firma, o seed de aleatorización? Técnica:
  modificar un byte del cuerpo del `executor.b` canónico y ver si el
  AMS lo acepta o rechaza; si lo rechaza, probar a recalcular como
  CRC-32/CRC-24/XOR/suma mod y repetir.
- Palabra 0x08: ¿puntero a tabla de módulos AMS o parte de la firma?
  Técnica: cambiarla a `0x00000000` y ver si el AMS falla.
- Tope real del `.b`: subir con padding hasta que falle. (Punto 2 de
  `cid53_payload.md` §4.)
- Si el AMS valida el `.b` **al mover el fichero** (durante
  `customize_upgrade.xml`) o **al abrir el MIDlet** (en caliente). Un
  rechazo en el primer momento es peor: no deja ni intentar.
