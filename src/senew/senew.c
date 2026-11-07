/*
 * senew.c -- Fork extension for seftool-senew.
 *
 * Hardware preservation project by semakers, based on seftool by
 * farid1991 (https://github.com/farid1991/seftool). See fork README.
 *
 * This file is intentionally self-contained: no dependency on libserialport
 * or on seftool's internal structs. Everything here is pure data + printing,
 * so it stays trivially testable and so --check-senew can run with no device
 * attached.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "senew.h"

/* -------------------------------------------------------------------------- */
/* Model -> board table.                                                      */
/* The phone reports its model from GDFS as e.g. "W580i" or "W600i". Early    */
/* firmwares sometimes drop the trailing 'i'. We accept both.                 */
/* -------------------------------------------------------------------------- */
static const struct {
	const char *prefix;
	senew_board_t board;
} MODEL_PREFIXES[] = {
	{"W580", SENEW_BOARD_W580},
	{"W600", SENEW_BOARD_W600},
	{"W810", SENEW_BOARD_W810},
};
#define MODEL_PREFIXES_N (int)(sizeof(MODEL_PREFIXES) / sizeof(MODEL_PREFIXES[0]))

senew_board_t senew_board_from_model(const char *model)
{
	if (!model || !*model)
		return SENEW_BOARD_UNKNOWN;
	for (int i = 0; i < MODEL_PREFIXES_N; i++) {
		size_t n = strlen(MODEL_PREFIXES[i].prefix);
		if (strncasecmp(model, MODEL_PREFIXES[i].prefix, n) == 0)
			return MODEL_PREFIXES[i].board;
	}
	return SENEW_BOARD_UNKNOWN;
}

const char *senew_board_name(senew_board_t b)
{
	switch (b) {
	case SENEW_BOARD_W580: return "W580";
	case SENEW_BOARD_W600: return "W600";
	case SENEW_BOARD_W810: return "W810";
	default:               return "unknown";
	}
}

const char *senew_board_chipset(senew_board_t b)
{
	switch (b) {
	case SENEW_BOARD_W580: return "DB2020";
	case SENEW_BOARD_W600: return "DB2010";
	case SENEW_BOARD_W810: return "DB2010";
	default:               return "unknown";
	}
}

/* -------------------------------------------------------------------------- */
/* CID coverage table.                                                        */
/*                                                                            */
/* Rows are senew boards; columns are CID 49 / 51 / 52 / 53.                  */
/*                                                                            */
/* Status today (2026-10-15):                                                 */
/*  - CID49/51/52 on all three boards: senew image available (identify ok,    */
/*    read-gdfs/read-flash via seftool-senew's upstream code paths).          */
/*  - CID53: PENDING on W580 (DB2020). Loader binaries for DB2020 CID53 are   */
/*    shipped by seftool (db2020_cid53red_cs_r3a013.bin,                      */
/*    db2020_cid53red_flash_r2a015.bin), BUT:                                 */
/*      * Read/write ops require either `--anycid` (DB2020 non-brown49) or    */
/*        `--break-rsa` (DB2000/DB2010 red49 only). CID53 on DB2020 is red,   */
/*        not brown49, so it falls under --anycid.                            */
/*      * We have NOT verified this path against any CID53 W580 yet. All      */
/*        W580s on the maintainer's bench so far are CID52 red, which is the vast     */
/*        majority. CID53 was a late minority run.                            */
/*    See docs/CID53_RUTA.md for the full investigation plan.                 */
/*  - DB2010 (W600, W810) does not have the CID53 variant in the wild in the  */
/*    same way DB2020 does. We mark CID53 on those boards as NONE to keep the */
/*    table honest.                                                           */
/* -------------------------------------------------------------------------- */
static const int SUPPORTED_CIDS[] = {49, 51, 52, 53};
#define SUPPORTED_CIDS_N (int)(sizeof(SUPPORTED_CIDS) / sizeof(SUPPORTED_CIDS[0]))

static senew_coverage_t COVERAGE[3 /* W580,W600,W810 */][4 /* 49,51,52,53 */] = {
	/* W580 (DB2020) */ { SENEW_COV_IMAGE_AVAILABLE,
	                      SENEW_COV_IMAGE_AVAILABLE,
	                      SENEW_COV_IMAGE_AVAILABLE,
	                      /* CID53: loaders shipped, anycid route public since
	                       * 2008 (Dream Box DBSS v3.2) and documented in the
	                       * scene for K800/K810 CID53. W580 CID53 end-to-end
	                       * check against a real device is still pending.
	                       * See docs/CID53_RUTA.md section 8. */
	                      SENEW_COV_PENDING_VERIFICATION },
	/* W600 (DB2010) */ { SENEW_COV_IMAGE_AVAILABLE,
	                      SENEW_COV_IMAGE_AVAILABLE,
	                      SENEW_COV_IMAGE_AVAILABLE,
	                      SENEW_COV_NONE },
	/* W810 (DB2010) */ { SENEW_COV_IMAGE_AVAILABLE,
	                      SENEW_COV_IMAGE_AVAILABLE,
	                      SENEW_COV_IMAGE_AVAILABLE,
	                      SENEW_COV_NONE },
};

static int board_row(senew_board_t b)
{
	switch (b) {
	case SENEW_BOARD_W580: return 0;
	case SENEW_BOARD_W600: return 1;
	case SENEW_BOARD_W810: return 2;
	default:               return -1;
	}
}

static int cid_col(int cid)
{
	for (int i = 0; i < SUPPORTED_CIDS_N; i++)
		if (SUPPORTED_CIDS[i] == cid)
			return i;
	return -1;
}

senew_coverage_t senew_coverage_for(senew_board_t b, int cid)
{
	int r = board_row(b), c = cid_col(cid);
	if (r < 0 || c < 0)
		return SENEW_COV_NONE;
	return COVERAGE[r][c];
}

/* -------------------------------------------------------------------------- */
/* Per-action coverage on A1 phones (DB2010/DB2020).                          */
/* This is a snapshot of what seftool's own README table claims. It is not    */
/* reverse engineered from the C code; the C code is still the authoritative  */
/* source. We expose it so SENUS can show a user "yes, write-gdfs will work   */
/* on this phone" without having to connect it.                               */
/* -------------------------------------------------------------------------- */
static const char *ACTIONS[] = {
	"identify",
	"flash",
	"read-flash",
	"read-gdfs",
	"write-gdfs",
	"write-script",
	"unlock-usercode",
	"upload-anycid",
	"fsx-upload",
	"fsx-download",
};
#define ACTIONS_N (int)(sizeof(ACTIONS) / sizeof(ACTIONS[0]))

static senew_action_state_t action_db2010(int cid, const char *action)
{
	/* Mirror of seftool's README table, DB201x column.
	 * CID29/36/49 are the "native" red CIDs; higher reds need --anycid.
	 */
	if (strcmp(action, "identify") == 0 ||
	    strcmp(action, "flash") == 0 ||
	    strcmp(action, "read-gdfs") == 0 ||
	    strcmp(action, "write-gdfs") == 0 ||
	    strcmp(action, "write-script") == 0 ||
	    strcmp(action, "unlock-usercode") == 0 ||
	    strcmp(action, "upload-anycid") == 0 ||
	    strcmp(action, "fsx-upload") == 0) {
		return SENEW_ACT_SUPPORTED;
	}
	if (strcmp(action, "read-flash") == 0 ||
	    strcmp(action, "fsx-download") == 0) {
		if (cid == 49)
			return SENEW_ACT_SUPPORTED;
		return SENEW_ACT_SUPPORTED_ANYCID; /* 51, 52, 53 */
	}
	return SENEW_ACT_UNKNOWN;
}

static senew_action_state_t action_db2020(int cid, const char *action)
{
	if (strcmp(action, "identify") == 0 ||
	    strcmp(action, "flash") == 0 ||
	    strcmp(action, "read-gdfs") == 0 ||
	    strcmp(action, "write-gdfs") == 0 ||
	    strcmp(action, "write-script") == 0 ||
	    strcmp(action, "unlock-usercode") == 0 ||
	    strcmp(action, "upload-anycid") == 0 ||
	    strcmp(action, "fsx-upload") == 0) {
		return SENEW_ACT_SUPPORTED;
	}
	if (strcmp(action, "read-flash") == 0 ||
	    strcmp(action, "fsx-download") == 0) {
		/* BROWN49 is the only "native"; all red CIDs go through --anycid.
		 * We don't distinguish brown vs red at the senew table level yet;
		 * mark as needing --anycid to match the common case (red).
		 *
		 * CID53: code path exists end-to-end in seftool upstream
		 * (loaders db2020_cid53red_{cs_r3a013,flash_r2a015}.bin shipped,
		 * anycid/DB2020.zip present, oflash.c:156 and :230 route cid==53
		 * to the right binary). What is pending is the first run against
		 * a real W580 CID53 at the bench. SENUS must gate write
		 * operations on this state until docs/CID53_RUTA.md step 5
		 * reports verified.
		 */
		if (cid == 53)
			return SENEW_ACT_PENDING; /* unverified end-to-end on W580 */
		return SENEW_ACT_SUPPORTED_ANYCID;
	}
	return SENEW_ACT_UNKNOWN;
}

senew_action_state_t senew_action_for(senew_board_t b, int cid, const char *action)
{
	if (!action)
		return SENEW_ACT_UNKNOWN;
	switch (b) {
	case SENEW_BOARD_W580:
		return action_db2020(cid, action);
	case SENEW_BOARD_W600:
	case SENEW_BOARD_W810:
		return action_db2010(cid, action);
	default:
		return SENEW_ACT_UNKNOWN;
	}
}

/* -------------------------------------------------------------------------- */
/* Override a sabiendas (ver senew.h).                                        */
/* -------------------------------------------------------------------------- */
int senew_env_one(const char *name)
{
	const char *v = getenv(name);
	return v != NULL && strcmp(v, "1") == 0;
}

int senew_force_requested(void) { return senew_env_one("SEFTOOL_SENEW_FORCE"); }

/* -------------------------------------------------------------------------- */
/* Printing.                                                                  */
/* -------------------------------------------------------------------------- */
static const char *cov_glyph(senew_coverage_t c)
{
	switch (c) {
	case SENEW_COV_IMAGE_AVAILABLE:     return "image";
	case SENEW_COV_PENDING_VERIFICATION: return "pending-v";
	case SENEW_COV_PENDING:             return "pending";
	default:                            return "-";
	}
}

void senew_print_banner(void)
{
	printf("seftool-senew: fork of seftool (farid1991) for hardware preservation.\n");
	printf("               senew OS targets: W580 (DB2020), W600/W810 (DB2010).\n");
}

void senew_print_table(void)
{
	printf("\nsenew coverage (board x CID):\n");
	printf("  %-6s | %-9s | %-9s | %-9s | %-9s\n",
	       "board", "CID49", "CID51", "CID52", "CID53");
	printf("  -------+-----------+-----------+-----------+-----------\n");
	senew_board_t boards[] = { SENEW_BOARD_W580, SENEW_BOARD_W600, SENEW_BOARD_W810 };
	for (int i = 0; i < 3; i++) {
		printf("  %-6s |", senew_board_name(boards[i]));
		for (int j = 0; j < SUPPORTED_CIDS_N; j++) {
			senew_coverage_t c = senew_coverage_for(boards[i], SUPPORTED_CIDS[j]);
			printf(" %-9s |", cov_glyph(c));
		}
		printf("\n");
	}
	printf("\nLegend: image     = senew OS build available for this board + CID;\n");
	printf("        pending-v = route public & loaders shipped, device-level\n");
	printf("                    verification pending (see docs/CID53_RUTA.md),\n");
	printf("                    identify/read-gdfs OK, writes refused by SENUS;\n");
	printf("        pending   = under investigation, do not use;\n");
	printf("        '-'       = out of scope for this board.\n");
}

void senew_print_identify_hint(const char *model, int cid)
{
	senew_board_t b = senew_board_from_model(model);
	if (b == SENEW_BOARD_UNKNOWN) {
		printf("senew: model '%s' is not a senew target.\n", model ? model : "?");
		return;
	}
	senew_coverage_t cov = senew_coverage_for(b, cid);
	const char *verdict;
	switch (cov) {
	case SENEW_COV_IMAGE_AVAILABLE:
		verdict = "senew image available for this board + CID";
		break;
	case SENEW_COV_PENDING_VERIFICATION:
		verdict = "senew PENDING_VERIFICATION for this CID "
		          "(route public, loaders shipped, unverified on bench; "
		          "see docs/CID53_RUTA.md); identify + read-gdfs OK, "
		          "DO NOT write";
		break;
	case SENEW_COV_PENDING:
		verdict = "senew PENDING for this CID (see docs/CID53_RUTA.md); DO NOT flash";
		break;
	default:
		verdict = "no senew image for this CID yet";
		break;
	}
	printf("senew: board=%s (%s), CID=%d -> %s\n",
	       senew_board_name(b), senew_board_chipset(b), cid, verdict);
}

/* --check-senew ------------------------------------------------------------- */
static const char *cov_json(senew_coverage_t c)
{
	switch (c) {
	case SENEW_COV_IMAGE_AVAILABLE:     return "image_available";
	case SENEW_COV_PENDING_VERIFICATION: return "pending_verification";
	case SENEW_COV_PENDING:             return "pending";
	default:                            return "none";
	}
}

/* Where SENUS and callers can read about a non-trivial cell. */
static const char *cov_doc(senew_coverage_t c)
{
	switch (c) {
	case SENEW_COV_PENDING_VERIFICATION:
	case SENEW_COV_PENDING:
		return "docs/CID53_RUTA.md";
	default:
		return NULL;
	}
}

static void print_json(void)
{
	printf("{\n");
	printf("  \"tool\": \"seftool-senew\",\n");
	/* Schema 2: cells can now be objects {state, doc} as well as plain
	 * strings. Readers that pin schema 1 still work on image_available
	 * and none cells (plain strings); schema >=2 is required for
	 * pending/pending_verification cells with a doc field.
	 */
	printf("  \"schema\": 2,\n");
	printf("  \"boards\": [\n");
	senew_board_t boards[] = { SENEW_BOARD_W580, SENEW_BOARD_W600, SENEW_BOARD_W810 };
	for (int i = 0; i < 3; i++) {
		printf("    {\n");
		printf("      \"name\": \"%s\",\n", senew_board_name(boards[i]));
		printf("      \"chipset\": \"%s\",\n", senew_board_chipset(boards[i]));
		printf("      \"cids\": {\n");
		for (int j = 0; j < SUPPORTED_CIDS_N; j++) {
			senew_coverage_t cov = senew_coverage_for(boards[i], SUPPORTED_CIDS[j]);
			const char *doc = cov_doc(cov);
			if (doc) {
				printf("        \"%d\": {\"state\": \"%s\", \"doc\": \"%s\"}%s\n",
				       SUPPORTED_CIDS[j],
				       cov_json(cov),
				       doc,
				       (j + 1 < SUPPORTED_CIDS_N) ? "," : "");
			} else {
				printf("        \"%d\": \"%s\"%s\n",
				       SUPPORTED_CIDS[j],
				       cov_json(cov),
				       (j + 1 < SUPPORTED_CIDS_N) ? "," : "");
			}
		}
		printf("      }\n");
		printf("    }%s\n", (i < 2) ? "," : "");
	}
	printf("  ]\n");
	printf("}\n");
}

int senew_cmd_check(int as_json)
{
	if (as_json) {
		print_json();
		return 0;
	}
	senew_print_banner();
	senew_print_table();
	printf("\nSupported actions on A1 phones: ");
	for (int i = 0; i < ACTIONS_N; i++)
		printf("%s%s", ACTIONS[i], (i + 1 < ACTIONS_N) ? ", " : "\n");
	printf("\nUse --json for machine-readable output (for SENUS and other callers).\n");
	return 0;
}
