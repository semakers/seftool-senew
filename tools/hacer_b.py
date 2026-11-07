#!/usr/bin/env python3
# hacer_b.py — empaqueta un .bin Thumb-2 enlazado para R8BE001 como un
# .b del flujo anycid del DB2020 (ver docs/formato_b.md).
#
# ESTRATEGIA: no reconstruimos el envoltorio OSE Delta Binary desde cero.
# Partimos de un `executor.b` canónico (una CXC concreta del W580i R8BE001)
# como PLANTILLA de 3692 B y le sustituimos el bloque de código Thumb
# (desde el offset 0x40) por el `.bin` que generó nuestro arm-none-eabi-ld.
# La cabecera (0x00..0x3F) se conserva TAL CUAL de la plantilla, incluida
# la palabra 0x04 (candidato a checksum) y la 0x08 (puntero NOR al AMS).
#
# Esto implica dos cosas que están explicadas en formato_b.md §6:
#   1. Para cada CXC objetivo necesitamos su propia plantilla (`anycid/
#      R8BE001_<CXC>_<MERCADO>.zip`), porque las 126 palabras del pool
#      de literales apuntan a direcciones de ese firmware concreto.
#      En un `.bin` nuestro, el pool se tiene que resolver EN COMPILACIÓN
#      a los símbolos de la CXC pedida (p.ej. `debug_printf = 0x44236C50`
#      para R8BE001 CXC1123326). Esto es responsabilidad del enlazador
#      ARM y del fuente, no de este script.
#   2. La palabra 0x04 (checksum) de la plantilla queda sin recalcular.
#      Si el AMS la valida al cargar, el `.b` será rechazado y habrá que
#      descifrar el algoritmo con un aparato real; si no, pasa. Es parte
#      de la lista `[sin aparato]`.
#
# Uso:
#   python3 hacer_b.py \
#       --plantilla anycid/R8BE001_CXC1123362_GENERIC_LA.zip \
#       --codigo build/w580i-cid53/senew_payload.bin \
#       --salida  build/w580i-cid53/senew_payload.b
#
# Verificación de regresión (sin aparato):
#   python3 hacer_b.py --verificar anycid/R8BE001_CXC1123362_GENERIC_LA.zip
#   → reproduce byte a byte el mismo .b que viene en el zip.
#
# No ejecuta nada contra un aparato. No abre /dev/tty*.

from __future__ import annotations
import argparse
import io
import os
import sys
import zipfile
from pathlib import Path

TAMANO_B    = 3692         # tamaño fijo del envoltorio para R8BE001 (W580i)
OFFSET_CODIGO = 0x40       # inicio del código Thumb dentro del .b
TAMANO_CABECERA = OFFSET_CODIGO
TAMANO_CODIGO = TAMANO_B - TAMANO_CABECERA   # 3628 B máximo de código
MAGIA = b"\xca\xfe\xb0\x0b"
RUTA_B_DENTRO_ZIP = "tpa/preset/custom/executor.b"

def leer_plantilla(zip_path: Path) -> bytes:
    with zipfile.ZipFile(zip_path, "r") as z:
        with z.open(RUTA_B_DENTRO_ZIP) as f:
            data = f.read()
    if len(data) != TAMANO_B:
        raise SystemExit(
            f"[hacer_b] plantilla {zip_path.name}: tamaño {len(data)} != {TAMANO_B}; "
            f"solo validamos R8BE001 (W580i) hoy. Ver docs/formato_b.md §1."
        )
    if data[:4] != MAGIA:
        raise SystemExit(f"[hacer_b] plantilla {zip_path.name}: magia errónea; no es .b anycid.")
    return data

def empaquetar(plantilla: bytes, codigo: bytes) -> bytes:
    if len(codigo) > TAMANO_CODIGO:
        raise SystemExit(
            f"[hacer_b] el código mide {len(codigo)} B, no cabe en el bloque de "
            f"{TAMANO_CODIGO} B del .b. Reduzca o cambie plantilla."
        )
    cabecera = plantilla[:TAMANO_CABECERA]
    cuerpo   = codigo + b"\x00" * (TAMANO_CODIGO - len(codigo))
    salida   = cabecera + cuerpo
    assert len(salida) == TAMANO_B
    return salida

def verificar_identidad(zip_path: Path) -> int:
    """Prueba de regresión: extrae el .b, lo re-empaqueta con su propio
    código, y comprueba que sale idéntico. Esto demuestra que el script
    no corrompe la plantilla."""
    original = leer_plantilla(zip_path)
    recortado = original[TAMANO_CABECERA:]        # equivalente al .bin de salida
    reempaquetado = empaquetar(original, recortado)
    if reempaquetado != original:
        print(f"[hacer_b] ERROR: re-empaquetado difiere de original para {zip_path.name}",
              file=sys.stderr)
        return 1
    print(f"[hacer_b] OK: re-empaquetado idéntico a {zip_path.name} ({TAMANO_B} B).")
    return 0

def main() -> int:
    ap = argparse.ArgumentParser(description="Empaqueta un .bin Thumb-2 como .b anycid del DB2020.")
    ap.add_argument("--plantilla", type=Path,
                    help="ZIP de plantilla anycid R8BE001 (p.ej. anycid/R8BE001_CXC...zip)")
    ap.add_argument("--codigo",    type=Path, help="Binario Thumb-2 enlazado (.bin)")
    ap.add_argument("--salida",    type=Path, help="Ruta del .b final")
    ap.add_argument("--verificar", type=Path,
                    help="Prueba de regresión: re-empaqueta la plantilla indicada y la compara "
                         "con el original. No necesita --codigo ni --salida.")
    args = ap.parse_args()

    if args.verificar:
        return verificar_identidad(args.verificar)

    for req in ("plantilla", "codigo", "salida"):
        if getattr(args, req) is None:
            ap.error(f"falta --{req}")

    plantilla = leer_plantilla(args.plantilla)
    codigo    = args.codigo.read_bytes()
    salida    = empaquetar(plantilla, codigo)
    args.salida.parent.mkdir(parents=True, exist_ok=True)
    args.salida.write_bytes(salida)
    print(f"[hacer_b] escrito {args.salida} ({len(salida)} B); "
          f"código útil = {len(codigo)} B, relleno = {TAMANO_CODIGO - len(codigo)} B.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
