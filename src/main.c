#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#define access _access
#define F_OK 0
#else
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <libserialport.h>

#include <core/common.h>
#include <core/cli_args.h>
#include <core/connection.h>
#include <emp/v3/flash.h>

#include "action.h"
#include "senew/senew.h"

static void print_usage(const char *progname)
{
	printf("Usage: %s -p <port> -b <baud> -a <action> [options]\n\n", progname);
	printf("  -p, --port <name>                    Serial port name (e.g. COM2, /dev/ttyUSB0)\n");
	printf("  -b, --baud <rate>                    Baudrate (default: 115200)\n");
	printf("  -a, --action <action>                Specify an action (see below)\n\n");

	printf("General actions:\n");
	printf("  %-46s %s\n\n", "identify", "Identify phone and show basic info");

	printf("Platform: Ericsson Z80\n");
	printf("Available actions:\n");
	printf("  %-46s %s\n", "identify", "Identify phone and show basic info");
	printf("  %-46s %s\n", "read-eeprom", "Read full EEPROM");
	printf("  %-46s %s\n", "write-eeprom <eeprom_file>", "Write full EEPROM");
	printf("  %-46s %s\n\n", "unlock usercode", "Reset user code to default");

	printf("Platform: Ericsson AVR\n");
	printf("Available actions:\n");
	printf("  %-46s %s\n", "identify", "Identify phone and show basic info");

	printf("Platform: SE pre-A1 (DB1000)\n");
	printf("Available actions:\n");
	printf("  %-46s %s\n", "identify", "Identify phone and show basic info");
	printf("  %-46s %s\n", "flash-avr <avr_file>", "Flash AVR MCU firmware");
	printf("  %-46s %s\n", "flash-arm <arm_file>", "Flash ARM/DSP firmware");
	printf("  %-46s %s\n\n", "read-avr start <addr> size <bytes>", "Read AVR memory region");
	printf("  %-46s %s\n\n", "unlock-avr <device>",
	       "Unlock userlock (EMP v2 devices: T68,T200,T230,T290,T300,T310,T610,T630,P800,P900,P910,Z600)");

	printf("Platform: SE A1 (DB2000 / DB2010 / DB2012 / DB2020 / PNX5230)\n");
	printf("Available actions:\n");
	printf("  %-46s %s\n", "identify", "Identify phone and show basic info");
	printf("  %-46s %s\n", "flash [main <file>] [fs <file>] [cda <file>]",
	       "Flash MAIN / FS firmware and/or upload CDA");
	printf("  %-46s %s\n", "read-flash start <addr> size <bytes> | block <count>",
	       "Read flash (specify start & size or block count)");
	printf("  %-46s %s\n", "                        save-as-babe", "Optional: convert dump directly to BABE");
	printf("  %-46s %s\n", "                        --anycid --cid <N>",
	       "DB2020 RED: whole NOR via the anycid exploit (executer must be running)");
	printf("  %-46s %s\n", "                        --jdflasher --cid 52 --imei <S>",
	       "DB2020 CID52 RED: whole NOR via qhtry(2020_prologue)+R2A005, NO executer (lectura)");
	printf("  %-46s %s\n", "read-gdfs", "Backup GDFS area");
	printf("  %-46s %s\n", "write-gdfs <filename>", "Restore GDFS from file");
	printf("  %-46s %s\n", "write-script <file1> [file2 ...]", "Apply VKP or GDFS script(s)");
	printf("  %-46s %s\n", "unlock <usercode|simlock>", "Unlock user code or SIM lock");
	printf("  %-46s %s\n", "fsx-upload src <local...> [dest <fs path>]", "Upload files to internal FS");
	printf("  %-46s %s\n", "fsx-download src <fs path>/<file> [dest <local>]", "Download files from internal FS");
	printf("  %-46s %s\n", "upload-anycid", "Upload AnyCID package for exploit access");
	printf("  %-46s %s\n", "flash-anycid [main <f>] [fs <f>]",
	       "DB2020 only: flash through anycid loader (executer must be running)");
	printf("  %-46s %s\n", "jdflasher-prime --cid 52 --imei <S>",
	       "VÍA DE RESPALDO manual de jdflasher: carga 2020_prologue + R y sale. El aparato se reinicia; "
	       "reconecta en modo C y lanza read-flash/read-gdfs/write-script/flash-jdflasher con --skip-prologue. "
	       "Úsala solo si la reconexión automática (connection_restart tras el prologue) no alcanza.");
	printf("  %-46s %s\n\n", "flash-jdflasher main <f> [fs <f>] --cid 52 --imei <S>",
	       "DB2020 CID52 RED: flash SIN executer vía qhtry(2020_prologue)+R2A005. Exige "
	       "SEFTOOL_JDFLASHER_CONFIRM=1 y SEFTOOL_JDFLASHER_WRITE=1 (sin ambas, DRY-RUN).");

	printf("Utility actions:\n");
	printf("  %-46s %s\n", "convert babe2raw <filename>", "Convert BABE -> raw");
	printf("  %-46s %s\n\n", "convert raw2babe <filename> <addr>", "Convert raw -> BABE at address");

	printf("A1 Global options:\n");
	printf("  %-46s %s\n", "--anycid", "Ignore CID restrictions (DB2012/DB2020/PNX5230)");
	printf("  %-46s %s\n", "--jdflasher",
	       "DB2020 CID52 RED only: qhtry(2020_prologue)+R2A005, SIN executer. Tras el prologue reconecta solo "
	       "(connection_restart, hasta ~45 s) sin tocar el teléfono. Exige --cid 52, --imei y SEFTOOL_JDFLASHER_CONFIRM=1. "
	       "Incompatible con --anycid y --break-rsa.");
	printf("  %-46s %s\n", "--skip-prologue",
	       "Con --jdflasher: NO cargar 2020_prologue ni reconectar (asumir que `jdflasher-prime` ya corrió en sesión "
	       "anterior, el aparato quedó parcheado y el humano ya reconectó a mano). Vía de respaldo.");
	printf("  %-46s %s\n", "--break-rsa", "Break RSA on DB2000 & DB2010 RED49");
	printf("  %-46s %s\n", "--cid <N>", "flash-anycid / read-flash --anycid: the ACTIVE CID that identify printed (required)");
	printf("  %-46s %s\n", "--imei <S>",
	       "stop if the phone's OTP IMEI differs (checked when the first loader is up, before anything is written)");
	printf("  %-46s %s\n", "--restore-boot", "read-flash/write-script/fsx-download: also WRITE ./rest/*.rest to the NOR");
	printf("  %-46s %s\n", "--gdfs-max <N>", "write-gdfs: allow one-packet writes up to N bytes (default 0x600)");
	printf("  %-46s %s\n\n", "--skip-big", "write-gdfs: skip units above the limit and report a PARTIAL restore");

	printf("General options:\n");
	printf("  %-46s %s\n", "-h, --help", "Show this help message");
	printf("  %-46s %s\n", "--check-senew", "Print senew OS coverage table (offline, no port)");
	printf("  %-46s %s\n\n", "--json", "With --check-senew: emit machine-readable JSON");
}

/* El IMEI tal como lo imprime identify: hasta 15 letras, cifras o «_» (loader_get_otp_info cambia lo que no
 * sea alfanumérico por «_» porque entra en nombres de archivo). Y con alguna letra o cifra: un OTP en blanco
 * (todo 0x00/0xFF) sale como «______________», que no identifica a ningún teléfono (dos en blanco «cuadrarían»). */
static int imei_valido(const char *s)
{
	size_t n = s ? strlen(s) : 0;
	int con_identidad = 0;
	if (n == 0 || n > 15)
		return 0;
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'))
			return 0;
		if (c != '_')
			con_identidad = 1;
	}
	return con_identidad;
}

/* Un número de 32 bits y nada más («0x44000000», «1234»). strtoul sin comprobar truncaba en silencio
 * «0x144000000» a 32 bits y dejaba pasar basura al final («0x1000x»). */
static int parse_u32(const char *s, uint32_t *out)
{
	if (!s || *s < '0' || *s > '9')
		return -1;
	errno = 0;
	char *end = NULL;
	unsigned long long v = strtoull(s, &end, 0);
	if (errno != 0 || !end || *end != '\0' || v > 0xFFFFFFFFull)
		return -1;
	*out = (uint32_t)v;
	return 0;
}

int parse_args(int argc, char **argv, cli_args *args)
{
	memset(args, 0, sizeof(*args));
	args->baudrate = 115200;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
			if (i + 1 < argc)
				args->port_name = argv[++i];
			else
				return fprintf(stderr, "Error: -p requires an argument\n"), 0;
		} else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--baudrate") == 0) {
			if (i + 1 < argc)
				args->baudrate = atoi(argv[++i]);
			else
				return fprintf(stderr, "Error: -b requires an argument\n"), 0;
		} else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--action") == 0) {
			if (i + 1 < argc)
				args->action = argv[++i];
			else
				return fprintf(stderr, "Error: -a requires an argument\n"), 0;

			/* handle flash extra args — misma gramática que `flash`
			 * para `flash-anycid` y `flash-jdflasher`: `main <f>`
			 * [`fs <f>`] [`cda <f>`]. Ambas acciones FUERZAN su flag
			 * (anycid=1 / jdflasher=1) tras parse_args. Ni flash-anycid
			 * ni flash-jdflasher aceptan `cda`. */
			if (strcmp(args->action, "flash") == 0 ||
			    strcmp(args->action, "flash-anycid") == 0 ||
			    strcmp(args->action, "flash-jdflasher") == 0) {
				while (i + 1 < argc && argv[i + 1][0] != '-') {
					const char *arg = argv[++i];

					if ((strcmp(arg, "main") == 0 || strcmp(arg, "MAIN") == 0) && i + 1 < argc)
						args->flash_mainfw = argv[++i];
					else if ((strcmp(arg, "fs") == 0 || strcmp(arg, "FS") == 0) && i + 1 < argc)
						args->flash_fsfw = argv[++i];
					else if ((strcmp(arg, "cda") == 0 || strcmp(arg, "CDA") == 0) && i + 1 < argc) {
						/* flash-anycid / flash-jdflasher no flashean cda:
						 * aceptarlo y callar daba la impresión de que se
						 * había escrito. */
						if (strcmp(args->action, "flash-anycid") == 0 ||
						    strcmp(args->action, "flash-jdflasher") == 0)
							return fprintf(stderr, "Error: %s does not flash 'cda' "
							                       "(only 'main' and 'fs')\n",
							               args->action),
							       0;
						args->flash_cda = argv[++i];
					}
					else
						return fprintf(stderr,
						               "Error: Unknown or incomplete flash "
						               "argument '%s'\n",
						               arg),
						       0;
				}

				if (!args->flash_mainfw && !args->flash_fsfw && !args->flash_cda) {
					/* flash-anycid y flash-jdflasher no aceptan cda; el
					 * mensaje tampoco debe mencionarlo. */
					const char *opts =
					    (strcmp(args->action, "flash-anycid") == 0 ||
					     strcmp(args->action, "flash-jdflasher") == 0)
					        ? "'main' or 'fs'"
					        : "'main', 'fs', or 'cda'";
					return fprintf(stderr, "Error: %s requires at least one "
					                       "of %s\n",
					               args->action, opts),
					       0;
				}
			} else if (strcmp(args->action, "read-flash") == 0) {
				while (i + 1 < argc && argv[i + 1][0] != '-') {
					const char *arg = argv[++i];

					if (strcmp(arg, "start") == 0 && i + 1 < argc) {
						if (parse_u32(argv[++i], &args->dump_addr) != 0)
							return fprintf(stderr, "Error: read-flash: invalid start '%s' (a 32-bit number)\n",
							               argv[i]),
							       0;
					} else if (strcmp(arg, "size") == 0 && i + 1 < argc) {
						if (parse_u32(argv[++i], &args->dump_size) != 0)
							return fprintf(stderr, "Error: read-flash: invalid size '%s' (a 32-bit number)\n",
							               argv[i]),
							       0;
					} else if (strcmp(arg, "block") == 0 && i + 1 < argc) {
						uint32_t blocks;
						if (parse_u32(argv[++i], &blocks) != 0 || blocks > 0xFFFFFFFFu / BLOCK_SIZE)
							return fprintf(stderr, "Error: read-flash: invalid block count '%s' (at most %u)\n",
							               argv[i], 0xFFFFFFFFu / BLOCK_SIZE),
							       0;
						args->dump_size = blocks * BLOCK_SIZE;
					} else if (strcmp(arg, "save-as-babe") == 0)
						args->save_as_babe = 1;
					else
						return fprintf(stderr, "Error: read-flash requires start "
						                       "<addr> and "
						                       "(size <bytes> | block <count>)\n"),
						       0;
				}

				if (args->dump_addr == 0 || args->dump_size == 0)
					return fprintf(stderr, "Error: read-flash requires start "
					                       "<addr> and (size "
					                       "<bytes> | block <count>)\n"),
					       0;
			} else if (strcmp(args->action, "unlock") == 0) {
				if (i + 1 < argc) {
					args->unlock_target = argv[++i];
					if (strcmp(args->unlock_target, "usercode") != 0 &&
					    strcmp(args->unlock_target, "simlock") != 0)
						return fprintf(stderr, "Error: unlock requires "
						                       "<usercode|simlock>\n"),
						       0;
				} else
					return fprintf(stderr, "Error: unlock requires "
					                       "<usercode|simlock>\n"),
					       0;
			} else if (strcmp(args->action, "write-gdfs") == 0) {
				if (i + 1 < argc)
					args->gdfs_filename = argv[++i];
				else
					return fprintf(stderr, "Error: write-gdfs requires <filename>\n"), 0;
			} else if (strcmp(args->action, "write-script") == 0) {
				int start = i + 1, count = 0;
				while (start + count < argc && argv[start + count][0] != '-')
					count++;
				if (count == 0)
					return fprintf(stderr, "Error: write-script requires at "
					                       "least one <filename>\n"),
					       0;
				args->script_filenames = (const char **)&argv[start];
				args->script_count = count;
				i = start + count - 1;
			} else if (strcmp(args->action, "fsx-upload") == 0) {
				while (i + 1 < argc && argv[i + 1][0] != '-') {
					const char *arg = argv[++i];
					if (strcmp(arg, "src") == 0) {
						int start = i + 1, count = 0;
						while (start + count < argc && argv[start + count][0] != '-' &&
						       strcmp(argv[start + count], "dest") != 0)
							count++;
						if (count == 0)
							return fprintf(stderr, "Error: fsx-upload requires "
							                       "src <file(s)>\n"),
							       0;
						args->src_list = (const char **)&argv[start];
						args->upload_count = count;
						i = start + count - 1;
					} else if (strcmp(arg, "dest") == 0 && i + 1 < argc)
						args->dest_path = argv[++i];
					else
						return fprintf(stderr,
						               "Error: Unknown or incomplete "
						               "fsx-upload argument "
						               "'%s'\n",
						               arg),
						       0;
				}

				if (args->upload_count == 0)
					return fprintf(stderr, "Error: fsx-upload requires src "
					                       "<local path(s)>\n"),
					       0;
			} else if (strcmp(args->action, "fsx-download") == 0) {
				while (i + 1 < argc && argv[i + 1][0] != '-') {
					const char *arg = argv[++i];
					if (strcmp(arg, "src") == 0 && i + 1 < argc)
						args->src_path = argv[++i];
					else if (strcmp(arg, "dest") == 0 && i + 1 < argc)
						args->dest_path = argv[++i];
					else
						return fprintf(stderr,
						               "Error: Unknown or incomplete "
						               "fsx-download argument "
						               "'%s'\n",
						               arg),
						       0;
				}

				if (!args->src_path)
					return fprintf(stderr, "Error: fsx-download requires src "
					                       "<internal path>\n"),
					       0;
				if (!args->dest_path)
					args->dest_path = "./temp";
			} else if (strcmp(args->action, "convert") == 0) {
				if (i + 1 < argc) {
					const char *mode = argv[++i];
					if (strcmp(mode, "raw2babe") == 0) {
						if (i + 2 < argc) {
							args->cnv_mode = mode;
							args->cnv_filename = argv[++i];
							args->mem_addr = strtoul(argv[++i], NULL, 0);
						} else
							return fprintf(stderr, "Error: convert raw2babe "
							                       "requires "
							                       "<filename> <addr>\n"),
							       0;
					} else if (strcmp(mode, "babe2raw") == 0) {
						if (i + 1 < argc)
							args->cnv_mode = mode, args->cnv_filename = argv[++i];
						else
							return fprintf(stderr, "Error: convert babe2raw "
							                       "requires <filename>\n"),
							       0;
					} else
						return fprintf(stderr, "Error: convert requires "
						                       "<raw2babe|babe2raw>\n"),
						       0;
				} else
					return fprintf(stderr, "Error: convert requires "
					                       "<raw2babe|babe2raw> ...\n"),
					       0;
			} else if (strcmp(args->action, "flash-avr") == 0) {
				if (i + 1 < argc)
					args->flash_avr_fw = argv[++i];
				else
					return fprintf(stderr, "Error: flash-avr requires <filename>\n"), 0;
			} else if (strcmp(args->action, "read-avr") == 0) {
				while (i + 1 < argc && argv[i + 1][0] != '-') {
					const char *arg = argv[++i];

					if (strcmp(arg, "start") == 0 && i + 1 < argc)
						args->dump_addr = strtoul(argv[++i], NULL, 0);
					else if (strcmp(arg, "size") == 0 && i + 1 < argc)
						args->dump_size = strtoul(argv[++i], NULL, 0);
					else
						return fprintf(stderr, "Error: read-avr requires start "
						                       "<addr> and size <bytes>\n"),
						       0;
				}

				if (args->dump_addr == 0 || args->dump_size == 0)
					return fprintf(stderr, "Error: read-avr requires start "
					                       "<addr> and (size <bytes> \n"),
					       0;
			} else if (strcmp(args->action, "flash-arm") == 0) {
				if (i + 1 < argc) {
					args->baudrate = 115200; // flash-arm only works with 115200
					args->flash_arm_fw = argv[++i];
				} else
					return fprintf(stderr, "Error: flash-arm requires <filename>\n"), 0;
			} else if (strcmp(args->action, "write-eeprom") == 0) {
				if (i + 1 < argc)
					args->eeprom_file = argv[++i];
				else
					return fprintf(stderr, "Error: write-eeprom requires <filename>\n"), 0;
			} else if (strcmp(args->action, "unlock-avr") == 0) {
				if (i + 1 < argc) {
					args->avr_device_name = argv[++i];
					if (strcmp(args->avr_device_name, "T68") != 0 &&
					    strcmp(args->avr_device_name, "T200") != 0 &&
					    strcmp(args->avr_device_name, "T230") != 0 &&
					    strcmp(args->avr_device_name, "T290") != 0 &&
					    strcmp(args->avr_device_name, "T300") != 0 &&
					    strcmp(args->avr_device_name, "T310") != 0 &&
					    strcmp(args->avr_device_name, "T610") != 0 &&
					    strcmp(args->avr_device_name, "T630") != 0 &&
					    strcmp(args->avr_device_name, "P800") != 0 &&
					    strcmp(args->avr_device_name, "P900") != 0 &&
					    strcmp(args->avr_device_name, "P910") != 0 &&
					    strcmp(args->avr_device_name, "Z600") != 0)
						return fprintf(stderr, "Error: unlock-avr requires "
						                       "<T68|T200|T230|T290|T300|T310|"
						                       "T610|T630|P800|P900|P910|Z600>\n"),
						       0;
				} else
					return fprintf(stderr, "Error: unlock-avr requires "
					                       "<T68|T200|T230|T290|T300|T310|T610|T630|"
					                       "P800|P900|P910|Z600>\n"),
					       0;
			}
		} else if (strcmp(argv[i], "--cid") == 0) {
			if (i + 1 < argc)
				args->expect_cid = atoi(argv[++i]);
			else
				return fprintf(stderr, "Error: --cid requires an argument\n"), 0;
		} else if (strcmp(argv[i], "--imei") == 0) {
			if (i + 1 < argc)
				args->expect_imei = argv[++i];
			else
				return fprintf(stderr, "Error: --imei requires an argument\n"), 0;
			if (!imei_valido(args->expect_imei))
				return fprintf(stderr,
				               "Error: --imei expects the IMEI exactly as identify printed it (up to 15 letters, "
				               "digits or '_', with at least one letter or digit)\n"),
				       0;
		} else if (strcmp(argv[i], "--gdfs-max") == 0) {
			if (i + 1 < argc)
				args->gdfs_write_max = (uint32_t)strtoul(argv[++i], NULL, 0);
			else
				return fprintf(stderr, "Error: --gdfs-max requires an argument\n"), 0;
		} else if (strcmp(argv[i], "--skip-big") == 0)
			args->gdfs_skip_big = 1;
		else if (strcmp(argv[i], "--restore-boot") == 0)
			args->restore_boot = 1;
		else if (strcmp(argv[i], "--anycid") == 0)
			args->anycid = 1;
		else if (strcmp(argv[i], "--jdflasher") == 0)
			args->jdflasher = 1;
		else if (strcmp(argv[i], "--skip-prologue") == 0)
			args->skip_prologue = 1;
		else if (strcmp(argv[i], "--break-rsa") == 0)
			args->break_rsa = 1;
		else if (strcmp(argv[i], "--check-senew") == 0 || strcmp(argv[i], "--json") == 0)
			; /* handled before parse_args(); swallow silently */
		else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
			return ACT_NONE;
		else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			fprintf(stderr, "(if it is a file name that starts with '-', write it as ./%s)\n", argv[i]);
			return ACT_NONE;
		}
	}

	if (!args->action)
		return ACT_NONE;

	/* flash-jdflasher y jdflasher-prime fuerzan --jdflasher: así las compuertas comunes de
	 * abajo (--cid 52, --imei, SEFTOOL_JDFLASHER_CONFIRM=1, mutuamente excluyentes) también
	 * se aplican a la ESCRITURA y al prime, aunque el usuario no haya tecleado el flag. */
	if (strcmp(args->action, "flash-jdflasher") == 0 ||
	    strcmp(args->action, "jdflasher-prime") == 0)
		args->jdflasher = 1;

	/* --jdflasher: compuertas de LECTURA en línea de órdenes. Todo lo que rechaza se rechaza ANTES de
	 * abrir el puerto, antes de identificar el aparato. Mensajes para la terminal; la lista de
	 * precondiciones vive en docs/CID52_JDFLASHER.md. */
	if (args->jdflasher) {
		if (args->anycid) {
			fprintf(stderr, "--jdflasher y --anycid son mutuamente excluyentes: jdflasher no usa "
			                "executer; anycid lo exige. Elige uno.\n");
			return ACT_NONE;
		}
		if (args->break_rsa) {
			fprintf(stderr, "--jdflasher y --break-rsa son mutuamente excluyentes: --break-rsa es la "
			                "ruta de DB2010 CID49 RED.\n");
			return ACT_NONE;
		}
		if (args->expect_cid != 52) {
			fprintf(stderr, "--jdflasher solo cablea CID52 (DB2020 RED). Para CID49/51 va --anycid; "
			                "CID53 queda pendiente de verificación. Falta --cid 52.\n");
			return ACT_NONE;
		}
		if (!args->expect_imei || args->expect_imei[0] == '\0') {
			fprintf(stderr, "--jdflasher exige --imei explícito: jdflasher NO carga P3M antes del "
			                "prologue, así que no hay OTP previo que lea identify.\n");
			return ACT_NONE;
		}
		const char *confirm = getenv("SEFTOOL_JDFLASHER_CONFIRM");
		if (!confirm || strcmp(confirm, "1") != 0) {
			fprintf(stderr, "SEFTOOL_JDFLASHER_CONFIRM=1 obligatorio: --jdflasher destraba el "
			                "preloader, lee docs/CID52_JDFLASHER.md antes.\n");
			return ACT_NONE;
		}
		/* --jdflasher solo lo propagan a phone->jdflasher cinco acciones (read-flash,
		 * read-gdfs, write-script, flash-jdflasher, jdflasher-prime). En las demás el
		 * flag se silenciaba y el operador creía que iba por la ruta nueva. Rechazamos
		 * aquí para que el error sea explícito y antes de abrir el puerto. */
		if (strcmp(args->action, "read-flash") != 0 &&
		    strcmp(args->action, "read-gdfs") != 0 &&
		    strcmp(args->action, "write-script") != 0 &&
		    strcmp(args->action, "flash-jdflasher") != 0 &&
		    strcmp(args->action, "jdflasher-prime") != 0) {
			fprintf(stderr, "--jdflasher solo se acepta en las acciones que lo cablean:\n"
			                "  read-flash, read-gdfs, write-script, flash-jdflasher, jdflasher-prime.\n"
			                "La acción '%s' NO lo propaga; iría por la vía antigua y lo\n"
			                "silenciaría. Elige una acción compatible o quita --jdflasher.\n",
			        args->action);
			return ACT_NONE;
		}
	}
	/* --skip-prologue solo tiene sentido con --jdflasher y en una acción que de verdad use
	 * el prologue (jdflasher-prime no, él ES el que lo carga). */
	if (args->skip_prologue) {
		if (!args->jdflasher) {
			fprintf(stderr, "--skip-prologue solo tiene sentido con --jdflasher.\n");
			return ACT_NONE;
		}
		if (strcmp(args->action, "jdflasher-prime") == 0) {
			fprintf(stderr, "--skip-prologue no se mezcla con jdflasher-prime (ésta ES la "
			                "acción que carga el prologue).\n");
			return ACT_NONE;
		}
	}

	return action_from_string(args->action);
}

int main(int argc, char **argv)
{
	/* Por línea: si algo aborta con la salida en un archivo, no se pierde
	 * lo último que se imprimió. */
	setvbuf(stdout, NULL, _IOLBF, 0);

	/* Fork extension: --check-senew runs offline and short-circuits the
	 * usual parse_args flow (no port, no serial, no phone). We also honor
	 * an optional --json flag for machine-readable output.
	 */
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--check-senew") == 0) {
			int as_json = 0;
			for (int j = 1; j < argc; j++)
				if (strcmp(argv[j], "--json") == 0)
					as_json = 1;
			return senew_cmd_check(as_json);
		}
	}

	/* parse args */
	cli_args args = {0};
	action_t act = parse_args(argc, argv, &args);

	/* print usage when no action */
	if (act == ACT_NONE) {
		print_usage(argv[0]);
		return 1;
	}

	/* action convert does not need a port */
	if (act == ACT_CONVERT) {
		printf("convert %s\n", args.cnv_mode);
		return action_convert(args.cnv_mode, args.cnv_filename, args.mem_addr);
	}

	/* For all other actions, we need a port */
	if (!args.port_name) {
		print_usage(argv[0]);
		return 1;
	}

	/* Todas las rutas de datos (./loader, ./anycid, ./rest, ./backup) son
	 * relativas al directorio actual. Lanzado desde otro sitio, antes fallaba
	 * tarde (con el teléfono ya a medias) y dejaba un backup/ suelto. */
	if (access("./loader", F_OK) != 0) {
		fprintf(stderr,
		        "Error: ./loader not found. Run seftool from the folder that contains loader/, anycid/ and rest/ "
		        "(the repository root).\n");
		return 1;
	}

	/* create backup for a set of actions */
	switch (act) {
	case ACT_IDENTIFY:
	case ACT_READ_GDFS:
	case ACT_WRITE_GDFS:
	case ACT_READ_FLASH:
	case ACT_READ_EEPROM:
	case ACT_WRITE_EEPROM:
		if (create_backup_dir("backup") != 0)
			return 1;
		break;
	default:
		break;
	}

	/* print parsed args */
	action_print_summary(act, &args);

	/* open port etc */
	struct sp_port *port;
	if (sp_get_port_by_name(args.port_name, &port) != SP_OK) {
		fprintf(stderr, "Error: Cannot open %s\n", args.port_name);
		return 1;
	}

	int res = -1;
	struct phone_info phone = {0};
	phone.baudrate = args.baudrate;
	phone.expect_cid = args.expect_cid;
	phone.skip_prologue = args.skip_prologue;
	snprintf(phone.expect_imei, sizeof(phone.expect_imei), "%s", args.expect_imei ? args.expect_imei : "");
	phone.gdfs_write_max = args.gdfs_write_max;
	phone.gdfs_skip_big = args.gdfs_skip_big;

	if (connection_open(port, &phone) != 0)
		goto cleanup;

	// execute pre Ericsson Mobile Protocol
	if (phone.type != ERICSSON_MP) {
		printf("\n");

		if (phone.chipset == PHONE_Z80)
			res = action_phone_z80(port, &phone, act, &args);
		else if (phone.chipset == PHONE_AVR)
			res = action_phone_avr(port, &phone, act, &args);

		goto cleanup;
	}

	switch (phone.protocol_major) {
	case EMP_PROTOCOL_1:
		res = action_emp_v1(port, &phone, act, &args);
		break;
	case EMP_PROTOCOL_2:
		res = action_emp_v2(port, &phone, act, &args);
		break;
	case EMP_PROTOCOL_3:
		res = action_emp_v3(port, &phone, act, &args);
		break;
	default:
		fprintf(stderr, "EMP Protocol %02d.%02d is not supported (yet)", phone.protocol_major,
		        phone.protocol_minor);
		break;
	}

cleanup:
	connection_close(port);
	sp_free_port(port);
	return res;
}
