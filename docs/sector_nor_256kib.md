# Sector físico de la NOR del W580 — 256 KiB (no 64 KiB)

Punto que no está en el código de seftool y conviene conocer antes de la
primera prueba por hitos del W580 CID52.

## Lo que vemos en el aparato

La NOR del W580i (Intel 0x887E, 512 Mbit M18 **[inferido]**) tiene un
tamaño de sector físico de **256 KiB**. Lo imprime el propio main del
teléfono:

```
NOR (Sibley) Flash device @ [0x44000000-0x47FFFFFF] 65536 Kb,
Vendor (0x89): Intel, Device: ?????? (0x887E),
Write Buffer Size: 1024 bytes,
Erase region … 256 Kb
```

Referencias cruzadas en el proyecto hermano `senew-w580`:

- `docs/fs.md:141` — explica que el `eraseSize=0x40000` del FS es
  LÓGICO; el físico lo saca por CFI y es 256 KiB.
- `docs/emulador.md:283` — mismo detalle en el modelo de la NOR del
  emulador.
- `docs/telefono_fisico.md:141` — «ponerle algo exige borrar bloques de
  256 KiB del main».

## Dónde está el precipicio

`BLOCK_SIZE = 0x10000` en `src/emp/v3/flash.h:8` es el bloque LÓGICO que
seftool mete en el flujo BABE (`addr` + 64 KiB de datos + ack `0x13`).
**No es el sector físico de borrado.**

El loader de setool2 (`loader/DB2020_LOADER_FOR_SETOOL2.bin`, 104 368 B)
es quien decide cuándo borrar. El fork no tiene su fuente, así que la
regla exacta se infiere **[inferido]** del comportamiento esperado de
CFI y de lo que escribe el main del W580: antes de escribir un bloque
lógico, borra el sector físico de 256 KiB que lo contiene.

Para `sistema_44140000.bin.ssw` (10 bloques × 64 KiB, 614 KiB en total,
rango `0x44140000..0x441D6344`) los sectores físicos tocados son:

| sector físico | rango | bloques BABE que cubre |
|---|---|---|
| 1 | `0x44140000..0x44180000` | 0, 1, 2, 3 |
| 2 | `0x44180000..0x441C0000` | 4, 5, 6, 7 |
| 3 | `0x441C0000..0x44200000` | 8, 9 |

Puntos de no retorno:

- **Corte entre el `0x10` del bloque 0 y el primer `0x01` de datos**: el
  sector físico 1 queda borrado por completo (los primeros 256 KiB del
  main, no 64 KiB). El teléfono NO arranca hasta rescribir desde el
  principio.
- **Corte en medio de un bloque posterior**: el sector físico actual
  tiene parte nuevo + parte 0xFF; los sectores anteriores están
  completos. El teléfono tampoco arranca (le falta la mitad del main),
  pero el cargador (`0x44000000..0x4413FFFF`) sigue intacto: una nueva
  sesión por tecla C acepta otro intento.
- **Corte entre el `0x11` (final) y el `0x12` (ack)**: el main está
  escrito entero. Un reset físico debería arrancar senew. Seftool aborta
  por timeout, pero no se pierde nada.

## Esto no es un bug de seftool

No hay nada que arreglar en el código del fork; es la geometría del
aparato. El informe de auditoría del 2026-10-16 lo pasó por alto porque
contaba paquetes a nivel BABE sin cruzar con el `eraseSize` físico; esta
nota lo documenta para la primera prueba por hitos.

## Qué sí se puede hacer desde el banco

- Confirmar visualmente, con `SENEW_USB_TRAZA=1`, el momento en que el
  teléfono acusa el `0x13` del bloque 0 — si tarda ~sector-físico-ms
  (~700 ms en Intel M18 **[inferido]**), es la señal de que el loader
  borró 256 KiB. Si es inmediato, puede estar borrando por bloques
  lógicos — pero es extraño por lo que dice CFI.
- Después de un corte, un `./seftool -p usb -b 921600 -a identify` dirá
  si el cargador sigue respondiendo. El `identify` lee el EROM, no la
  NOR escribible: aunque el main esté a 0xFF, debería contestar.

## Receta para la primera prueba por hitos

1. `hito_2` (8 KiB): escribe en el primer sector físico. Al terminar,
   `identify` debería seguir contestando y el teléfono debería dar la
   señal de vida mínima.
2. Si va, continuar con `hito_6`, `hito_3`, etc., siempre dentro del
   mismo sector físico (los hitos están todos por debajo de 32 KiB).
3. Antes del `sistema_sin_escritura` (10 bloques), tener claro que ya
   son 3 sectores físicos. El riesgo de corte en el bloque 0 es el
   mismo, pero el flujo entero es más largo.
4. `sistema_44140000.bin` es la primera imagen con la que una
   interrupción deja el teléfono necesitando la restauración de
   rollback. Desde 2026-10-17 el respaldo de SENUS lleva el `flash_full.bin`
   de un RED CID52 (`read-flash --anycid`, ver `read_flash_anycid.md`) y la
   restauración escribe el main por `flash-anycid`; **sin probar en un
   aparato**, y con senew ya puesto el paso de «executer» puede no poder
   completarse. La red parcial solo-GDFS no cura el main.

**Antes de pulsar «Reemplazo frío (anycid)»**, confirmar: `./seftool
-p usb -b 921600 -a identify` da `ACTIVE CID:52 COLOR:RED`, y el
dropdown del hito elegido NO dice «escribe NOR» a menos que ese sea el
paso del plan.
