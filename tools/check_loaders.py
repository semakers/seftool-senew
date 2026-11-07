#!/usr/bin/env python3
"""Comprueba que cada loader que nombra src/core/payload.h existe en ./loader
con el MISMO nombre (mayúsculas incluidas: en Linux importan, en macOS/APFS
no, y un nombre que solo funciona en la Mac revienta en la otra máquina).

Distingue lo que el código USA de lo que solo está definido:
  - usado y con otra capitalización  -> error (esto ya mordió con PNX5230)
  - usado y ausente                   -> error, salvo los de CONOCIDOS
  - definido pero nunca usado         -> aviso, no error

Uso: tools/check_loaders.py        (sale con 1 si hay un error)
"""
import glob, os, re, sys

raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
h = open(os.path.join(raiz, "src/core/payload.h"), encoding="utf-8", errors="replace").read()
ldr = os.path.join(raiz, "loader")

# Defectos de upstream en chips que este fork no usa (DB2000): se anotan, no fallan.
CONOCIDOS = {"DB2000_CSLOADER_R4B_SETOOL"}

# Los loaders viejos (Z80, AVR, RTZ) viven en subcarpetas (loader/z80/…): el
# código antepone la carpeta, así que basta con que el nombre exista en algún sitio.
reales = set()
for _d, _dirs, _files in os.walk(ldr):
    reales.update(_files)
    reales.update(_dirs)
reales_min = {n.lower(): n for n in reales}

codigo = ""
for f in glob.glob(os.path.join(raiz, "src", "**", "*.c"), recursive=True):
    codigo += open(f, encoding="utf-8", errors="replace").read()

errores = avisos = 0
for m in re.finditer(r'^\s*#define\s+(\w+)\s+"([^"]+)"', h, re.M):
    macro, nombre = m.groups()
    if "/" in nombre or not nombre.strip() or nombre in reales:
        continue
    usado = re.search(r"\b" + re.escape(macro) + r"\b", codigo) is not None
    if nombre.lower() in reales_min:
        print(f"ERROR  mayúsculas  {macro}: el código pide «{nombre}», el archivo es «{reales_min[nombre.lower()]}»")
        errores += 1
    elif usado and macro not in CONOCIDOS:
        print(f"ERROR  falta       {macro}: «{nombre}» se usa y no está en loader/")
        errores += 1
    elif usado:
        print(f"conocido           {macro}: «{nombre}» falta (defecto de upstream, chip no usado aquí)")
    else:
        print(f"aviso  sin uso     {macro}: «{nombre}» no existe, pero ningún código lo usa")
        avisos += 1
print(f"{errores} error(es), {avisos} aviso(s)")
sys.exit(1 if errores else 0)
