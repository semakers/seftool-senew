#!/usr/bin/env python3
"""Guiones para el teléfono simulado (tests/mock/sp_mock.c).

REAL: lo que contestó el W580i bueno (CID 52 RED) el 2026-10-16 en un
identify, tal como lo vio la traza USB. El nombre del modelo llegó cortado
a 64 bytes en la traza: el resto se rellena con ceros.

  guion.py real                  -> solo el prefijo real
  guion.py fuzz SEMILLA K        -> K trozos reales y después basura bien
                                    y mal formada (paquetes 0x89, acks,
                                    longitudes en los bordes, cadenas que
                                    el código busca)

Sesión del CS loader (SOLO LECTURA; construida leyendo el código, NO es una
captura real: sirve para recorrer con los detectores de memoria el camino de
read-gdfs / upload-anycid, no para saber cómo habla el teléfono):
  guion.py readgdfs [grande] DUMP -> prefijo real + carga del CS loader +
                                    activación + nombre + CXC + volcado READALL.
                                    Escribe en DUMP el archivo que read-gdfs
                                    debe dejar (cuenta + variables).
  guion.py cs                    -> lo mismo sin el volcado (upload-anycid)
  guion.py fuzzcs SEMILLA        -> readgdfs con bytes alterados
"""
import os
import random
import struct
import sys


def h(s):
    return bytes.fromhex(s)


def paquete(cmd, data, roto=False):
    cuerpo = bytes([0x89, cmd, len(data) & 0xFF, (len(data) >> 8) & 0xFF]) + data
    x = 0
    for b in cuerpo:
        x ^= b
    ck = (x + 7) & 0xFF
    if roto:
        ck ^= 0x5A
    return cuerpo + bytes([ck])


NOMBRE = h(
    "00 57 00 35 00 38 00 30 00 69 00 00 00 ba be 6f 00 6e 00 65 00 2d 00 6e 00 61"
    " 00 6d 00 65 00 00 00 ba be 00 00 05 00 00 00 c0 ff ff ff 00 00 00 00 38 99 b0"
    " 45 84 01 00 00 28 00 04"
)
NOMBRE = NOMBRE + bytes(0x49 - len(NOMBRE))

REAL = [
    h("5a"),
    h("99 00 03 01 ff ff ff ff"),
    h("ff ff ff ff ff ff ff"),
    h("45 73 42"),
    h("45 68 4d"),
    h("45 61 54 45 62 53"),
    h("45 64 51"
      " 89 08 37 00 30 37 31 31 33 30 20 31 31 35 30 20 4e 50 41 43 58 43 31 32 35 30"
      " 33 33 30 5f 44 42 32 30 32 30 5f 50 52 4f 44 55 43 54 49 4f 4e 49 44 4c 4f 41"
      " 44 45 52 5f 50 33 4d f5"),
    h("06 89 0a 02 00 89 7e 7d"),
    h("06 89 1e 13 00 00 01 33 00 01 33 35 30 30 30 30 30 30 30 30 30 30 30 30 b8"),
    h("06 89 58 0d 00 00 04 00 00 00 ff 00 00 00 34 00 00 00 1a"),
    h("06 89 1d 01 00 00 9c"),
    b"\x06" + paquete(0x1D, NOMBRE),
    h("06 89 1d 21 00 00 54 45 4c 43 45 4c 20 47 53 4d" + " 00" * 22 + " e2"),
]

CADENAS = [b"EsB", b"EhM", b"EaT", b"EbS", b"EdQ", b"Security disabled =)\n",
           b"EERE,", b"CS_LOADER", b"CSLOADER", b"FLASHLOADER", b"PRODUCTIONID",
           b"FILE_SYSTEM_LOADER", b"R8BE001     prgCXC1123362_GENER", b"\x06", b"Z"]
BORDES = [0, 1, 2, 3, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 0x7FE, 0x7FF,
          0x800, 0x801, 0x1000, 0x1001, 0x1002, 0x2000, 0xFFFE, 0xFFFF]
CMDS = [0x08, 0x0A, 0x0D, 0x1D, 0x1E, 0x21, 0x22, 0x24, 0x57, 0x58, 0x00, 0xFF]


def basura(r):
    t = r.random()
    if t < 0.15:
        return b"\x06"
    if t < 0.65:
        n = r.choice(BORDES) if r.random() < 0.6 else r.randrange(0, 300)
        data = bytes(r.randrange(256) for _ in range(min(n, 300))) + bytes(max(0, n - 300))
        p = paquete(r.choice(CMDS) if r.random() < 0.8 else r.randrange(256), data,
                    roto=r.random() < 0.1)
        if r.random() < 0.1:  # longitud declarada que miente
            p = p[:2] + bytes([r.randrange(256), r.randrange(256)]) + p[4:]
        return (b"\x06" + p) if r.random() < 0.5 else p
    if t < 0.85:
        return r.choice(CADENAS)
    return bytes(r.randrange(256) for _ in range(r.choice([1, 2, 3, 4, 5, 10, 100, 1000])))


# ---------------- sesión del CS loader (solo lectura) ----------------
CS_HELLO = b"071130 1150 NPACXC1250331_DB2020_CSLOADER_R3A009"
CXC = b"R8BE001     prgCXC1123362_GENERIC_LA"


def ack_pkt(cmd, payload):
    return b"\x06" + paquete(cmd, payload)


def cs_replies(gdfs_server=True, cxc=None):
    """Lo que contesta el teléfono desde que se manda el CS loader hasta
    leer el nombre y el artículo CXC. Orden = el del código."""
    # Cabecera, prólogo y cuerpo se mandan en paquetes de hasta 0x7FF bytes;
    # cada uno que no es el último recibe un 06 suelto, y el último la
    # respuesta 3D. Los tamaños salen del loader real del W580 CID52.
    ruta = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "loader",
                        "db2020_cid52red_cs_r3a009.bin")
    b = open(ruta, "rb").read()
    partes = [896, struct.unpack_from("<I", b, 552)[0], struct.unpack_from("<I", b, 744)[0]]
    out = []
    for tam in partes:
        n = max(1, -(-tam // 0x7FF))
        out += [b"\x06"] * (n - 1)
        out.append(ack_pkt(0x3D, b"\x00"))
    out.append(paquete(0x08, CS_HELLO))  # saludo del loader
    out.append(ack_pkt(0x01, b"\x09\x00"))  # CS loader activado (data[1] = estado 0)
    if gdfs_server:
        out.append(ack_pkt(0x04, b"\x05\x00"))  # servidor GDFS activado
        out.append(paquete(0x04, b"\x01\x00" + "W580i".encode("utf-16le") + b"\x00\x00"))
        out.append(paquete(0x04, b"\x01\x00" + (cxc or CXC) + b"\x00"))
    return out


def dump_records(grande=False):
    r = random.Random(1234)
    recs = []
    for i in range(60):
        n = r.choice([0, 1, 2, 7, 16, 33, 64, 100, 255, 256, 600, 0x600, 0x5FF])
        recs.append((r.randrange(1, 6), r.randrange(256), r.randrange(256), bytes(r.randrange(256) for _ in range(n))))
    if grande:
        recs.insert(30, (2, 0x34, 0x12, bytes(0x700)))  # variable > 0x600
    return recs


def readall_chunks(recs, maxidx=0x7F0):
    """Paquetes 89 04 LL LL 02 00 [cuenta] vars... ck, troceados como el
    código los espera: LL = (índice del fin de los datos) - 1, y el 1.er
    paquete lleva la cuenta de variables tras la cabecera."""
    rb = [bytes([b, lo, hi]) + struct.pack("<I", len(d)) + d for (b, lo, hi, d) in recs]
    paquetes, cur, idx = [], [], 10
    for x in rb:
        if cur and idx + len(x) > maxidx:
            paquetes.append((cur, idx))
            cur, idx = [], 6
        cur.append(x)
        idx += len(x)
    paquetes.append((cur, idx))
    chunks = []
    for k, (xs, fin) in enumerate(paquetes):
        ll = fin - 1
        p = bytes([0x89, 0x04, ll & 0xFF, ll >> 8, 0x02, 0x00])
        if k == 0:
            p += struct.pack("<I", len(recs))
        p += b"".join(xs) + b"\x00"  # ck: el código no lo verifica
        chunks.append(p)
    chunks[0] = b"\x06" + chunks[0]  # serial_wait_ack consume un 06 antes
    dump = struct.pack("<I", len(recs)) + b"".join(rb)
    return chunks, dump


def fsx_ok(subcmd):
    """Respuesta FSX genérica: cmd 0x03, data[0]=subcmd (echo), data[1]=0 (OK)."""
    return ack_pkt(0x03, bytes([subcmd, 0x00]))


def fsx_mal(subcmd, status):
    """Respuesta FSX de ERROR: cmd 0x03, data[0]=subcmd, data[1]=status != 0.
    Antes del arreglo A-1, csloader_put_file daba por bueno este chunk y seguía."""
    return ack_pkt(0x03, bytes([subcmd, status]))


def upload_anycid_ok_prefix():
    """Prefijo común: EROM + QHLDR + CS loader + servidor GDFS + FSX start + chdir /.
    El CXC crudo lleva «prg» entre version y nombre; csloader_gdfs_get_cxc_article
    lo trocea y rellena phone->cxc_article = "R8BE001_CXC1123362_GENERIC_LA", y
    después loader.c:303 copia ese valor a phone->anycid_target. Así el fork busca
    ./anycid/R8BE001_CXC1123362_GENERIC_LA.zip, que existe en el repo."""
    out = list(REAL[:10]) + cs_replies()  # CXC = CXC por omisión (con «prg»)
    out.append(fsx_ok(0x12))  # start_fsx_server
    out.append(fsx_ok(0x08))  # change_directory("/")
    return out


def main():
    if sys.argv[1] == "real":
        trozos = REAL
    elif sys.argv[1] == "cs":
        # opcional: otro artículo CXC (p. ej. uno sin paquete anycid)
        trozos = REAL[:10] + cs_replies(cxc=sys.argv[2].encode() if len(sys.argv) > 2 else None)
    elif sys.argv[1] == "putfile":
        # Flujo entero de upload-anycid con los dos zips reales del fork:
        # 1 (start_fsx) + 1 (chdir) + 54 FSX del DB2020.zip
        # [customize_upgrade.xml: 3 MKDIR + 1 DELETE + 2 PUTFILE = 6;
        #  executor.jar: 3 MKDIR + 1 DELETE + 44 PUTFILE = 48]
        # + 9 FSX del lA.zip
        # [executor.b: 3 MKDIR + 1 DELETE + 5 PUTFILE = 9]
        # = 65 respuestas CMD_CS_FSX, más 1 CMD_CSLOADER shutdown.
        #
        # argv[2]: "ok" o un N (1..65) para inyectar un ENOSPC (status 0x12)
        # en la N-ésima respuesta FSX. Por omisión (sin argv[2]): todas OK.
        corte = None
        if len(sys.argv) > 2 and sys.argv[2] != "ok":
            corte = int(sys.argv[2])
        trozos = upload_anycid_ok_prefix()  # ya trae FSX #1 (start) y #2 (chdir)
        TOTAL_FSX = 65
        for n in range(3, TOTAL_FSX + 1):
            if corte is not None and n == corte:
                trozos.append(fsx_mal(0x0C, 0x12))
            else:
                # el subcmd exacto no importa al cliente mientras cmd==0x03 y data[1]==0
                trozos.append(fsx_ok(0x0C))
        trozos.append(ack_pkt(0x01, b"\x08\x00"))  # shutdown_fsx_server (CMD_CSLOADER)
    elif sys.argv[1] in ("readgdfs", "fuzzcs"):
        grande = sys.argv[1] == "readgdfs" and sys.argv[2] == "grande"
        cero = sys.argv[1] == "readgdfs" and sys.argv[2] == "cero"
        recs = [] if cero else dump_records(grande)
        chunks, dump = readall_chunks(recs)
        if sys.argv[1] == "readgdfs":
            open(sys.argv[-1], "wb").write(dump)
            trozos = REAL[:10] + cs_replies() + chunks
        else:
            r = random.Random(int(sys.argv[2]))
            trozos = REAL[:10] + cs_replies() + chunks
            for _ in range(r.randrange(1, 6)):
                i = r.randrange(len(trozos) - len(chunks) + 1 if r.random() < 0.3 else 10, len(trozos))
                b = bytearray(trozos[i])
                if b:
                    j = r.randrange(min(len(b), 12)) if r.random() < 0.7 else r.randrange(len(b))
                    b[j] = r.choice([0, 1, 0x7F, 0x80, 0xFF, r.randrange(256)])
                    trozos[i] = bytes(b)
            if r.random() < 0.3:
                trozos = trozos[: r.randrange(10, len(trozos))]
    else:
        r = random.Random(int(sys.argv[2]))
        k = int(sys.argv[3])
        trozos = REAL[:k] + [basura(r) for _ in range(r.randrange(1, 60))]
    for t in trozos:
        print(t.hex())


if __name__ == "__main__":
    main()
