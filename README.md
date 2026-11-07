# seftool-senew

*Español más abajo — [ir a la versión en español](#español).*

> [!WARNING]
> **A new, experimental fork. Use it at your own risk.** `seftool` writes to the flash memory of phones: a mistake can leave a phone unusable. Read what an action does before running it, keep backups, and never run it on a phone you cannot afford to lose.

**seftool-senew** is a fork of **[seftool](https://github.com/farid1991/seftool) by farid1991** (MIT), the command-line tool for Sony Ericsson phones (DB2000/DB2010/DB2012/DB2020/PNX5230, Z80/AVR), adapted for the **[senew](https://github.com/semakers/senew-w580)** project: an open operating system for the **Sony Ericsson W580i**. It is the engine that **[SENUS](https://github.com/semakers/senus)** runs underneath to talk to a phone that still has the manufacturer's firmware.

Everything of upstream still works. The original author, the original license (MIT, [`LICENSE`](LICENSE)) and the upstream README ([`README.upstream.md`](README.upstream.md), kept verbatim) are preserved; the credits at the end of that README stand: *the_laser* (SETool), *den_po* (jdflasher), the *phoneXS* team, *fixeria*, *B-Con*.

## What this fork adds

(detail in [`CHANGES_SENEW.md`](CHANGES_SENEW.md) and [`docs/`](docs/); the fork started from upstream commit `a60d869`)

- **W580i / DB2020 over the any-CID route**: `upload-anycid`, `flash-anycid`, `read-flash --anycid` (atomic dumps, windows and gates before any byte is sent), a first-class path for **CID 52/53** phones, `--restore-boot`.
- **`senew` module** (`src/senew/`): which `(board, CID)` pairs have a senew image, `--check-senew [--json]` (offline, machine-readable for SENUS), an `identify` hint.
- **`--imei`**: the phone's identity is checked when the first loader is activated; `SEFTOOL_SENEW_FORCE` and `SEFTOOL_BREAK_CONFIRM` only count with the exact value `1`.
- **Atomic, checked dumps** (`.INCOMPLETO` → verify → rename), safer GDFS reads (retries, double read, hash comparison), stricter parsers (fuzzed), many fixes found in two audit rounds (see [`docs/auditoria_2026-10-16.md`](docs/auditoria_2026-10-16.md)).
- **macOS and Windows without a serial port**: `mac/sp_usb.c` talks to the phone over USB directly with libusb (also used by the Windows build), so no libserialport is needed there.
- **`jdflasher` route for CID 52** (needs a loader that belongs to another project, see below), `tools/hacer_b.py` (the `.b` package format), the mock test bench in `tests/mock/`.
- A **contract with SENUS** ([`docs/senus_integracion.md`](docs/senus_integracion.md)).

## What is and is not in this repository

- `loader/` and `rest/` are the loaders and restore data **distributed by the original seftool project**; they are kept as upstream ships them, **except `loader/2020_prologue`** (it comes from the `jdflasher` plugin of another project and is not ours to distribute: the `jdflasher` route needs you to bring it yourself).
- `anycid/` has only what senew's installer uses: `DB2020.zip` (the any-CID package for the DB2020 platform) and six `R8BE001_*_GENERIC_*.zip` packages (a few kilobytes each, they contain the manufacturer's original `executor.b`, included only so the first installation can work). The other any-CID packages of upstream, and upstream's `break49/` folder, are **not** included: take them from [farid1991/seftool](https://github.com/farid1991/seftool) if you need them (SENUS never uses them).
- **Not here on purpose**: firmware images, flash or GDFS dumps (`backup/` is git-ignored), `*.vkp` patches (ElfPack patches on top of the manufacturer's firmware), compiled binaries.
- The tests that need real firmware ask you to pass the files; none is provided.

## Build

### macOS (what SENUS ships)

`mac/build.sh` builds with `clang` only (no Homebrew, no cmake): libusb 1.0.27 is compiled from source and linked **statically** (LGPL-2.1: its source is the official tarball below, and this script is the build recipe), zlib/minizip come from zlib 1.3.1.

```sh
mkdir -p deps && cd deps
curl -L -o libusb.tar.bz2 https://github.com/libusb/libusb/releases/download/v1.0.27/libusb-1.0.27.tar.bz2   # sha256 ffaa41d741a8a3bee244ac8e54a72ea05bf2879663c098c82fc5757853441575
curl -L -o zlib.tar.gz    https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz        # sha256 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
cd ..
mac/build.sh "$PWD/deps"        # -> ./seftool
./seftool --check-senew
```

SENUS's universal build (arm64 + x86_64) is [`instaladores/macos_universal.sh`](https://github.com/semakers/senus/blob/main/instaladores/macos_universal.sh) in the SENUS repository.

### Windows (MSYS2 MINGW64)

`win/build.sh [dir-libusb]` builds `seftool.exe` statically against the official libusb 1.0.27 binaries (unpack the `.7z` release). The phone needs the **WinUSB** driver on the *SEMC USB Flash* device (0fce:adde); SENUS's installer does that for you.

### Linux (CMake)

```sh
sudo apt install build-essential cmake pkg-config libserialport-dev zlib1g-dev libbz2-dev libminizip-dev   # Debian/Ubuntu (package names not tested; the fork was built on Fedora)
mkdir build && cd build && cmake .. && cmake --build . -j
./seftool --check-senew
```

## Use

```sh
./seftool --check-senew          # coverage table: which (board, CID) have a senew image
./seftool --check-senew --json   # the same, stable JSON for SENUS
./seftool -h                     # all actions
```

Actions that talk to a phone (`identify`, `read-gdfs`, `read-flash`, `upload-anycid`, `flash-anycid`…) are described in [`docs/`](docs/) and in the upstream README. seftool resolves its data folders (`./loader`, `./anycid`, `./rest`, `./backup`) **relative to the working directory**, not to the binary: run it from the folder that contains them.

## License

MIT, like upstream — see [`LICENSE`](LICENSE) (Copyright 2005 Farid Irwan; the fork's additions are marked in `CHANGES_SENEW.md`). The libraries you link in keep their own licenses (libusb LGPL-2.1, zlib).
*Sony*, *Sony Ericsson*, *W580i* and other names are trademarks of their respective owners and are used only to say which hardware this is about; this project is not affiliated with them. It exists to preserve and keep using hardware its owners already have.

---

<a id="español"></a>

# seftool-senew

> [!WARNING]
> **Un fork nuevo y experimental. Úsalo bajo tu propio riesgo.** `seftool` escribe en la memoria flash de los teléfonos: un error puede dejar un teléfono inservible. Lee lo que hace una acción antes de ejecutarla, haz copias de seguridad y no lo uses con un teléfono que no puedas perder.

**seftool-senew** es un fork de **[seftool](https://github.com/farid1991/seftool) de farid1991** (MIT), la herramienta de línea de órdenes para teléfonos Sony Ericsson (DB2000/DB2010/DB2012/DB2020/PNX5230, Z80/AVR), adaptado al proyecto **[senew](https://github.com/semakers/senew-w580)**: un sistema operativo abierto para el **Sony Ericsson W580i**. Es el motor que **[SENUS](https://github.com/semakers/senus)** maneja por debajo para hablar con un teléfono que todavía lleva el firmware del fabricante.

Todo lo de upstream sigue funcionando. Se conservan el autor original, la licencia original (MIT, [`LICENSE`](LICENSE)) y el README de upstream ([`README.upstream.md`](README.upstream.md), tal cual); valen los créditos del final de ese README: *the_laser* (SETool), *den_po* (jdflasher), el equipo de *phoneXS*, *fixeria*, *B-Con*.

## Qué añade el fork

(detalle en [`CHANGES_SENEW.md`](CHANGES_SENEW.md) y [`docs/`](docs/); el fork salió del commit `a60d869` de upstream)

- **W580i / DB2020 por la ruta any-CID**: `upload-anycid`, `flash-anycid`, `read-flash --anycid` (volcados atómicos, ventanas y compuertas antes de mandar un solo byte), ruta de primera clase para teléfonos **CID 52/53**, `--restore-boot`.
- **Módulo `senew`** (`src/senew/`): qué pares `(placa, CID)` tienen imagen de senew, `--check-senew [--json]` (sin teléfono, legible por máquina para SENUS), una pista en `identify`.
- **`--imei`**: la identidad del teléfono se comprueba al activar el primer loader; `SEFTOOL_SENEW_FORCE` y `SEFTOOL_BREAK_CONFIRM` solo valen con el valor exacto `1`.
- **Volcados atómicos y comprobados** (`.INCOMPLETO` → verificar → renombrar), lecturas del GDFS más seguras (reintentos, doble lectura, comparación de hashes), parsers más estrictos (con fuzzing) y muchos arreglos de dos rondas de auditoría (ver [`docs/auditoria_2026-10-16.md`](docs/auditoria_2026-10-16.md)).
- **macOS y Windows sin puerto serie**: `mac/sp_usb.c` habla con el teléfono por USB directo con libusb (lo usa también la versión de Windows), así que no hace falta libserialport.
- **Ruta `jdflasher` para CID 52** (necesita un loader que pertenece a otro proyecto, ver abajo), `tools/hacer_b.py` (el formato de paquete `.b`) y el banco de pruebas con simulador en `tests/mock/`.
- Un **contrato con SENUS** ([`docs/senus_integracion.md`](docs/senus_integracion.md)).

## Qué hay y qué no hay en este repositorio

- `loader/` y `rest/` son los loaders y datos de restauración que **distribuye el proyecto seftool original**; se dejan como los trae upstream, **salvo `loader/2020_prologue`** (viene del plugin `jdflasher` de otro proyecto y no nos toca distribuirlo: la ruta `jdflasher` necesita que lo aportes tú).
- `anycid/` solo tiene lo que usa el instalador de senew: `DB2020.zip` (el paquete any-CID de la plataforma DB2020) y seis paquetes `R8BE001_*_GENERIC_*.zip` (unos kilobytes cada uno; contienen el `executor.b` original del fabricante y se incluyen solo para que la primera instalación funcione). Los demás paquetes any-CID de upstream y su carpeta `break49/` **no** están: tómalos de [farid1991/seftool](https://github.com/farid1991/seftool) si los necesitas (SENUS nunca los usa).
- **No está a propósito**: imágenes de firmware, volcados de flash o de GDFS (`backup/` está en `.gitignore`), parches `*.vkp` (parches de ElfPack sobre el firmware del fabricante), binarios compilados.
- Las pruebas que necesitan firmware real te piden que pases los ficheros; no se aporta ninguno.

## Compilar

### macOS (lo que lleva SENUS)

`mac/build.sh` compila solo con `clang` (sin Homebrew ni cmake): libusb 1.0.27 se compila de las fuentes y se enlaza **estática** (LGPL-2.1: su fuente es el tarball oficial de abajo, y este guion es la receta de compilación); zlib/minizip salen de zlib 1.3.1.

```sh
mkdir -p deps && cd deps
curl -L -o libusb.tar.bz2 https://github.com/libusb/libusb/releases/download/v1.0.27/libusb-1.0.27.tar.bz2   # sha256 ffaa41d741a8a3bee244ac8e54a72ea05bf2879663c098c82fc5757853441575
curl -L -o zlib.tar.gz    https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz        # sha256 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
cd ..
mac/build.sh "$PWD/deps"        # -> ./seftool
./seftool --check-senew
```

La compilación universal de SENUS (arm64 + x86_64) es [`instaladores/macos_universal.sh`](https://github.com/semakers/senus/blob/main/instaladores/macos_universal.sh) en el repositorio de SENUS.

### Windows (MSYS2 MINGW64)

`win/build.sh [dir-libusb]` compila `seftool.exe` estático contra los binarios oficiales de libusb 1.0.27 (descomprime la versión `.7z`). El teléfono necesita el driver **WinUSB** en el dispositivo *SEMC USB Flash* (0fce:adde); el instalador de SENUS lo hace por ti.

### Linux (CMake)

```sh
sudo apt install build-essential cmake pkg-config libserialport-dev zlib1g-dev libbz2-dev libminizip-dev   # Debian/Ubuntu (nombres de paquete sin probar; el fork se compiló en Fedora)
mkdir build && cd build && cmake .. && cmake --build . -j
./seftool --check-senew
```

## Uso

```sh
./seftool --check-senew          # tabla de cobertura: qué (placa, CID) tienen imagen de senew
./seftool --check-senew --json   # lo mismo, JSON estable para SENUS
./seftool -h                     # todas las acciones
```

Las acciones que hablan con un teléfono (`identify`, `read-gdfs`, `read-flash`, `upload-anycid`, `flash-anycid`…) están descritas en [`docs/`](docs/) y en el README de upstream. seftool busca sus carpetas de datos (`./loader`, `./anycid`, `./rest`, `./backup`) **relativas al directorio de trabajo**, no al binario: ejecútalo desde la carpeta que las contiene.

## Licencia

MIT, como upstream — ver [`LICENSE`](LICENSE) (Copyright 2005 Farid Irwan; lo añadido por el fork está marcado en `CHANGES_SENEW.md`). Las bibliotecas que enlazas conservan sus licencias (libusb LGPL-2.1, zlib).
*Sony*, *Sony Ericsson*, *W580i* y otros nombres son marcas de sus respectivos dueños y se usan solo para decir de qué aparato se trata; este proyecto no tiene relación con ellos. Existe para conservar y seguir usando aparatos que sus dueños ya tienen.
