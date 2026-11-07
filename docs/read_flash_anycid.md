# `read-flash --anycid` — volcar la NOR entera de un W580 RED

> **Estado (2026-10-17): NUNCA ha corrido contra un aparato.** Lo que hay es código leído, un banco sin
> teléfono y esta guía. Todo lo marcado **[sin saber]** solo lo contestará la primera sesión real.

## Para qué

El respaldo de rollback de SENUS necesita la NOR entera (64 MiB, `0x44000000..0x48000000`) de un W580 RED
(CID 49/51/52) para poder volver atrás. En un DB2020 RED, `read-flash` a secas falla («This cid & cert is not
supported»): la lectura solo la acepta el loader de setool2, y ese solo se carga por la rama **anycid**
(`upload-anycid` + el MIDlet «executer» abierto en el teléfono).

## Qué NO es

* **No es de solo lectura de punta a punta.** `upload-anycid` **escribe 3 ficheros** en el sistema de archivos
  del teléfono (`/tpa/preset/custom/…`: `customize_upgrade.xml`, `executor.jar`, `executor.b`). No toca la NOR
  directamente ni el GDFS, pero el volcado de la NOR **los incluirá**. El *primer contacto* con un aparato sigue
  siendo `identify` + 2× `read-gdfs` (de verdad solo lectura; `w580/fisico/CUANDO_LLEGUES_respaldo.md`).
* `read-flash --anycid` en sí **no manda ninguna orden de escritura a la NOR**: solo `0x32` (lectura) y su ACK.
  El `.rest` solo se escribe con `--restore-boot`, y entonces **después** del volcado.
* Tampoco cubre `write-script *.vkp --anycid` ni `fsx-download --anycid`, que siguen sin compuerta.

## Secuencia (CLI)

```sh
cd seftool-senew
./seftool -p usb -b 921600 -a identify                    # anota «ACTIVE CID:52 COLOR:RED»
# (cada orden apaga el teléfono: volver a entrar en modo C = tecla C + cable)
./seftool -p usb -b 921600 -a upload-anycid --anycid      # ESCRIBE 3 ficheros en el FS del teléfono
# HUMANO: desconecta, pon la SIM, enciende, Menú → Juegos → «executer»;
#         con el MIDlet activo, apaga; tecla C + cable.
./seftool -p usb -b 921600 -a read-flash start 0x44000000 size 0x4000000 --anycid --cid 52
```

`--cid N` es el *ACTIVE CID* que imprimió `identify` y es **obligatorio**: en un DB2020 recién conectado el CID
activo no se conoce hasta cargar un loader (`erom_get_info` devuelve 0 para DB2020), así que lo da quien llama.

## Compuertas (todas **antes** de mandar un solo byte de loader)

Con el teléfono en modo C solo han salido los tres paquetes del saludo (`3f`, `4b`, `5337`). La última línea de
stderr es siempre `read-flash --anycid: REFUSED: <motivo>`; las pistas van en líneas anteriores.

| Se rechaza si… | Por qué |
|---|---|
| falta `--cid` | sin el CID activo no se sabe si es seguro seguir |
| la tabla de senew no da cobertura `image_available` para (W580, CID): **CID 53** (sin verificar en un aparato real), 50, 99… | igual que `flash-anycid`; `docs/CID53_RUTA.md` |
| la ventana no cabe en `0x44000000..0x48000000` (tamaño 0, por debajo, por arriba, o que da la vuelta de 32 bits) | no se manda a leer memoria que no es la NOR |

`SEFTOOL_SENEW_FORCE=1` salta la tabla y la ventana **a sabiendas**. Solo vale el valor exacto `1` (antes valía
cualquier valor, incluso vacío) y **SENUS no lo hereda**: el hijo se lanza sin esa variable.

El parser de `read-flash` rechaza además `start`/`size`/`block` que no quepan en 32 bits o traigan basura
(`0x144000000`, `0x1000x`, `block 65536`).

## Qué hace, y qué imprime

1. Compuerta (arriba) → «`read-flash --anycid: SOLO LECTURA, no se manda ninguna orden de escritura a la NOR.`»
2. Carga **solo** el loader de setool2: `STARTING SETOOL BOOTLOADER…`, saludos `LDR: …`, y «`Security disabled =)`».
   Si el executer no está activo: `[QHTRY] Run executer first` y se sale **sin tocar nada**.
3. Lee de 64 KiB en 64 KiB (1024 trozos para la NOR entera): `reading raw: ./backup/flashdump_<imei>_44000000_04000000.bin`,
   `reading 1024 blocks`, `reading block: n/1024 (addr … size 0x10000)`.
4. Publica (atómico, como `read-gdfs`): `Flash dump saved <ruta> (67108864 bytes, SHA-1 <hex>)`.
5. Solo si se pidió `--restore-boot`: repara el arranque **después** del volcado.
6. `Shutdown phone` (apaga el teléfono).

«Security disabled =)» se imprime sin condición tras cargar el loader: **que salga no demuestra nada**.

## `--imei`: no leer el teléfono equivocado

`read-flash` (y todas las acciones que arrancan con el piloader P3M) acepta `--imei <IMEI>`: el IMEI tal como
lo imprime `identify` (hasta 15 letras, cifras o `_`). En cuanto el primer loader está activo y el OTP leído —antes
del CS loader, del flashloader de setool2 o de cualquier otro— se compara con el del aparato; si no coincide, o el
aparato no dice el suyo, sale con rc 255 y, como última línea de stderr,
`imei-check: REFUSED: no es el teléfono esperado; no se manda nada más.`
En DB2020 el IMEI no se conoce hasta ese punto (`erom_get_info` devuelve 0), así que no puede comprobarse antes.

En `read-flash --anycid` y `flash-anycid` la comprobación ocurre al activar el preloader de setool2, que ya corre en
la RAM del teléfono ajeno (inofensivo: es RAM) y **antes** de `DB2020_LOADER_FOR_SETOOL2` y de cualquier lectura o
escritura de la NOR. Ahí NO se imprime `[QHTRY] Run executer first` ni la lista de causas del executer: no es lo
que falla. **[sin saber]** si el hello real de ese preloader llega por el mismo camino que el del P3M (nunca se ha
visto en un cable; el guion real del P3M no sirve para esa carga): esa parte se probó por estructura del fuente
(`prueba_readflash_x.sh`) y con funciones puras, no de punta a punta.

El IMEI tiene que traer alguna letra o cifra: un OTP en blanco sale como `______________` y el parser lo rechaza.

Es lo que usa SENUS: entre una sesión y otra se vuelve a enchufar el cable, y sin esto otro teléfono recibiría las
órdenes del respaldo. Desde el CLI es opcional; sin `--imei` el comportamiento es el de siempre.

## Ficheros

| Fichero | Cuándo |
|---|---|
| `./backup/flashdump_<imei>_44000000_04000000.bin.INCOMPLETO` | mientras lee; un fallo (o Ctrl-C, o un corte) lo deja ahí con `read-flash INCOMPLETE (…): partial data left in …, do NOT use it` |
| `./backup/flashdump_<imei>_44000000_04000000.bin` | solo al terminar y comprobarse el tamaño en disco (`fsync`) |
| `….bin.anterior` | el volcado bueno anterior del mismo nombre, apartado (UNA generación: el siguiente volcado bueno sustituye a este `.anterior`; un fallo al publicar devuelve el anterior a su sitio) |

Los volcados llevan el IMEI en el nombre y la identidad del aparato: **no salen de la máquina**.

## `--restore-boot`

Sin él no se escribe nada (`Note: boot-area repair is OFF`). Con él, `bflash_repair_boot` escribe `./rest/*.rest`
en la NOR (`0x44140000`) **después** de leer. (Upstream lo hacía antes de leer, dentro de
`loader_send_bflash_ldr_db2020`: el volcado habría salido de la NOR ya «reparada».) SENUS **nunca** lo pasa.

## CID 53

`--cid 53` se rechaza (la tabla marca W580/CID53 como `pending_verification`). Es justo el **paso 4** de
`docs/CID53_RUTA.md`: ahí, a sabiendas, `SEFTOOL_SENEW_FORCE=1 ./seftool … read-flash start 0x44000000 size 0x20000 --anycid --cid 53`.
SENUS aborta el respaldo de un CID 53 justo tras `identify`.

## Primera sesión real (procedimiento)

Antes de los 64 MiB, una **sonda de 2 bloques** (128 KiB): valida la cadena entera con poco en juego.

```sh
SENEW_USB_TRAZA=1 ./seftool -p usb -b 921600 -a read-flash start 0x44000000 size 0x20000 --anycid --cid 52 \
  2>&1 | tee sonda1.log
sha1sum backup/flashdump_*_44000000_00020000.bin     # tiene que coincidir con el SHA-1 que imprimió seftool
head -c 64 backup/flashdump_*_44000000_00020000.bin | xxd   # que no sea todo FF ni todo 00
```

Repetir la sonda (otro executer + modo C si hace falta) y comparar el SHA-1: la misma NOR da el mismo SHA-1.
Solo entonces, la NOR entera (a 921600 son del orden de 12 min por un cable serie; por USB **[sin saber]**).

**Guardar como evidencia** (sin copiar el IMEI a documentos): el stdout completo de cada sesión, la traza USB, los
`sha1sum`/`sha256sum`, el tamaño, y los 64 primeros bytes del volcado.

Con el volcado completo (y antes de usarlo desde SENUS), compruebe a mano lo que SENUS va a exigir:

```sh
sha1sum backup/flashdump_*_44000000_04000000.bin                # el mismo SHA-1 que imprimió seftool
grep -a -o 'prgCXC[0-9]*_[A-Za-z_]*' backup/flashdump_*_44000000_04000000.bin | sort | uniq -c   # el ID del firmware
cmp -n 1048576 backup/flashdump_*.bin <(tail -c +16777217 backup/flashdump_*.bin | head -c 1048576) && echo "MiB 16 = MiB 0: MAL"
```

**Preguntas que solo contestará el aparato [sin saber]:**

1. ¿El loader de setool2 contesta `0x32` como los demás (la trama `0x33` con su suma)?
2. ¿Hace falta abrir «executer» otra vez entre dos lecturas? (SENUS pide la pausa cada vez.)
3. ¿Cuánto tarda de verdad los 64 MiB?
4. ¿Qué devuelve el loader en las zonas sin mapear? (SENUS rechaza un volcado de un solo byte repetido.)
5. ¿El estado del exploit sobrevive a un main sustituido por senew? (Decide si la restauración por `flash-anycid`
   puede funcionar con senew puesto, o si el paso de «executer» —que necesita el Java de fábrica— se queda sin sitio.)
6. ¿Aparece `prgCXC…` en la NOR volcada? SENUS lo exige (`w580/docs/fs.md` lo da como observado en la imagen
   oficial, no en un volcado del aparato), y un volcado sin él no se registra. Si la NOR resulta ser de menos de
   64 MiB con alias (el mismo contenido repetido), SENUS también lo rechazará por «la NOR se repite»: sería un
   hallazgo a investigar, no un fallo de SENUS.

## Cómo lo usa SENUS

`EjecutorRespaldo` (modo real), W580 RED CID 49/51/52: `identify` → *reconectar* → `read-gdfs` A → *reconectar* →
`read-gdfs` B → *reconectar (aviso: desde aquí ya no es solo lectura)* → `upload-anycid` → hasta 3 × { *abrir
«executer»* → `read-flash … --anycid --cid N --imei <IMEI>` }. Toda orden con puerto menos `identify` lleva `--imei`.
El volcado se acepta solo si la línea `Flash dump saved` de esa pasada lo nombra, mide `0x4000000` B y tiene el SHA-1
que seftool imprimió; además sus 256 KiB de `0x44000000` y de `0x44140000` no pueden ser un solo byte repetido, el
primer MiB no se repite en el MiB 16/32/48 y tiene que contener `prg<MAPP CXC article>`. Antes de cada intento
SENUS aparta (no borra) un `flashdump_<imei>_…` anterior con el mismo nombre. La restauración
(`flash-anycid main … --cid N`, porque `flash main` rechaza en el host cualquier imagen sin firma) está en
`senus_integracion.md` §7–8.

## Qué se probó, y cómo (sin aparato, sin simular ningún loader)

`tests/mock/prueba_readflash.sh` (ASan + UBSan + FORTIFY):

* **(C)** funciones puras con búferes sintéticos: la ventana (incluida la aritmética de 64 bits), el plan de la NOR
  entera (1024 trozos, `0x44000000`…`0x47FF0000`), el escritor atómico (`.INCOMPLETO`, `.anterior`, SHA-1 contra
  `sha1sum`), `senew_force_requested()` y la compuerta.
* **(B)** sin `--anycid` contra la **captura real** del identify: falla, explica la receta, ninguna orden `0x32`.
* **(A)** seis rechazos por línea de órdenes con el saludo real: exactamente 3 escrituras, ninguna de loader.
* **(P)** el parser.
* **(A2)** `bflash_repair_boot` con `need_rest=0` no escribe ni un byte.

**No hay teléfono de guion para el loader de FLASH**: regla dura. Lo que ese loader contesta a una lectura no se ha
visto en un cable.
