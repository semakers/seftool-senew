# Fork changelog — seftool-senew

Everything in this file documents changes made **on top of**
[`seftool`](https://github.com/farid1991/seftool) by farid1991 (MIT).
Upstream code is unmodified unless noted.

## 2026-10-15 — fork opened

Based on upstream commit `a60d869`.

### Added
- `src/senew/senew.h`, `src/senew/senew.c` — offline coverage table and
  model → board resolver. Pure data + printing, no serial dependency.
- `--check-senew` option in `src/main.c`, short-circuits before port
  handling. `--json` emits a stable JSON payload (schema 1).
- `src/emp/v3/action.c` — one line added in each of the two `identify`
  paths (`action_identify` for DB2010/DB2020, `action_identify_pnx` for
  PNX5230): calls `senew_print_identify_hint(model, cid)` after Model
  is printed. Nothing above it changed.
- `docs/CID53_RUTA.md` — Spanish-language investigation plan for CID53
  on W580 (DB2020). Document only; no code or tests touch CID53 in this
  fork yet.
- `docs/senus_integracion.md` — contract between this CLI and SENUS
  (JSON schema, stability rules).
- `README.md` — new EN+ES README with the human context (preservation of
  the maintainer's own W580i / W600i / W810i).

### Changed
- `CMakeLists.txt` — added `src/senew` to `target_include_directories`
  (sources are picked up by the existing `file(GLOB_RECURSE src/*.c)`).
- `src/main.c` — new help lines for `--check-senew` and `--json`;
  silently-accept these flags in `parse_args` so the "unknown option"
  check doesn't fire.

### Preserved
- `README.MD` — upstream README is kept verbatim (note the uppercase
  extension); the new `README.md` references it.
- `LICENSE` — unchanged. The fork is also MIT.
- All of `src/action.c`, `src/emp/`, `src/eric/`, `src/core/`,
  `src/utils/`, `loader/`, `anycid/`, `break49/`, `rest/`: untouched.

### Not done (deliberate, see docs/CID53_RUTA.md)
- No code change for CID53 on DB2020. The data entry in the senew table
  is `pending`, meaning "documented, not implemented". The verification
  path waits for a CID53 unit to reach the bench.

## 2026-10-17 — `read-flash --anycid` de primera clase (rama `mac-usb`)

Para que el respaldo rollback de un W580 RED lleve la NOR entera (64 MiB). Detalle y procedimiento de la
primera sesión real: [`docs/read_flash_anycid.md`](docs/read_flash_anycid.md).

- `flash_read` escribe de forma atómica (`.INCOMPLETO` → comprobar → renombrar; el anterior a `.anterior`).
- `read-flash --anycid` en DB2020: compuerta (`--cid`, tabla de senew, ventana de la NOR) antes de mandar un
  solo byte de loader; carga solo el loader de setool2; lee; repara el arranque (`--restore-boot`) DESPUÉS.
- `SEFTOOL_SENEW_FORCE` solo vale con el valor exacto `1` (antes valía cualquier valor, incluso vacío).
- El parser de `read-flash` rechaza `start`/`size`/`block` que no caben en 32 bits o traen basura.
