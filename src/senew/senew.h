/*
 * senew.h -- Fork extension for seftool-senew.
 *
 * Public API of the senew lookup module. Everything here is OFFLINE: it
 * never touches a serial port and never talks to a phone. It just answers
 * "do we have a senew image for this phone + CID?" and "what do we know
 * about the coverage of each (platform, CID) pair in this fork?".
 *
 * Keeping the table in one place lets us refactor hardcoded CID branches
 * in oflash.c / bflash.c into data-driven lookups later, without touching
 * seftool's upstream protocol code.
 */
#ifndef SENEW_H
#define SENEW_H

#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Supported phones in the senew project (the maintainer's own hardware).             */
/* These are the only models where senew currently has an OS image.           */
/* -------------------------------------------------------------------------- */
typedef enum {
	SENEW_BOARD_UNKNOWN = 0,
	SENEW_BOARD_W580,  /* DB2020 */
	SENEW_BOARD_W600,  /* DB2010 */
	SENEW_BOARD_W810,  /* DB2010 */
} senew_board_t;

/* Coverage state for one (board, cid) cell. */
typedef enum {
	SENEW_COV_NONE = 0,          /* No work at all. */
	SENEW_COV_PENDING,           /* Documented, not implemented yet. */
	SENEW_COV_PENDING_VERIFICATION, /* Code path exists (loaders shipped,
	                                 * anycid plumbing in upstream), but
	                                 * the (board, CID) combo has never
	                                 * been exercised against a real
	                                 * device. See docs/CID53_RUTA.md.
	                                 * SENUS must refuse to flash on this
	                                 * state. */
	SENEW_COV_IMAGE_AVAILABLE,   /* senew OS build exists for this board+CID. */
} senew_coverage_t;

/* Coverage state for one action across all CIDs on one board. */
typedef enum {
	SENEW_ACT_UNKNOWN = 0,
	SENEW_ACT_SUPPORTED,         /* seftool-senew wires it on this board. */
	SENEW_ACT_SUPPORTED_ANYCID,  /* Needs the --anycid exploit path. */
	SENEW_ACT_PENDING,           /* Known gap, under investigation. */
} senew_action_state_t;

/* -------------------------------------------------------------------------- */
/* Model -> board resolution.                                                 */
/* Called from the identify path AFTER the model string is read from GDFS.    */
/* `model` is the raw string reported by the phone (e.g. "W580i", "W600i").   */
/* Returns SENEW_BOARD_UNKNOWN when the model is not one of ours.             */
/* -------------------------------------------------------------------------- */
senew_board_t senew_board_from_model(const char *model);
const char *senew_board_name(senew_board_t b);
const char *senew_board_chipset(senew_board_t b);

/* -------------------------------------------------------------------------- */
/* Coverage queries.                                                          */
/* -------------------------------------------------------------------------- */
senew_coverage_t senew_coverage_for(senew_board_t b, int cid);
senew_action_state_t senew_action_for(senew_board_t b, int cid, const char *action);

/* -------------------------------------------------------------------------- */
/* SEFTOOL_SENEW_FORCE=1: el humano, desde la terminal, salta a sabiendas las */
/* compuertas de senew (tabla de cobertura, ventana de NOR). Solo vale el     */
/* valor exacto "1": con getenv()!=NULL valían también "0" y la cadena vacía, */
/* y SENUS lanza seftool heredando el entorno.                                */
/* -------------------------------------------------------------------------- */
int senew_force_requested(void);
/* La misma regla para cualquier confirmación a sabiendas por entorno (SEFTOOL_SENEW_FORCE,
 * SEFTOOL_BREAK_CONFIRM): solo vale el valor exacto "1". */
int senew_env_one(const char *name);

/* -------------------------------------------------------------------------- */
/* Pretty-printers (human output).                                            */
/* -------------------------------------------------------------------------- */
void senew_print_table(void);
void senew_print_banner(void);

/* Print the senew verdict for a phone that has just been identified.
 * Safe to call with board=UNKNOWN; it will print a single line and return.
 * Called from action_identify() after Model is known.
 */
void senew_print_identify_hint(const char *model, int cid);

/* -------------------------------------------------------------------------- */
/* --check-senew: offline mode. Does not need a phone or a port. Prints the   */
/* full coverage table and a short help text, in human form or JSON.          */
/* Returns an exit code suitable for main().                                  */
/* -------------------------------------------------------------------------- */
int senew_cmd_check(int as_json);

#endif /* SENEW_H */
