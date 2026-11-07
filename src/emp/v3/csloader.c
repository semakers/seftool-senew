#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <libserialport.h>

#ifdef _WIN32
#include <direct.h> // _mkdir
#define MKDIR(path) _mkdir(path)
#define RMDIR(path) _rmdir(path)
#define PATHSEP '\\'
#else
#include <sys/stat.h>
#include <unistd.h>
#ifndef F_OK
#define F_OK 0
#endif
#define MKDIR(path) mkdir(path, 0755)
#define RMDIR(path) rmdir(path)
#define PATHSEP '/'
#endif

#include <core/common.h>
#include <core/serial.h>
#include <emp/common/cmd.h>
#include <utils/scandir.h>
#include <utils/zipper.h>

#include "loader.h"
#include "csloader.h"
#include "gdfs.h"

#define GDFS_DATA_OFFSET 2

/* Ruta ASCII -> UCS-2 dentro de dst[cap]. Devuelve los bytes escritos, o 0
 * si no cabe (con `reserva` bytes libres para lo que se añada después).
 * ascii_to_ucs2 no mira el tamaño del destino. */
static size_t path_ucs2(const char *name, uint8_t *dst, size_t cap, size_t reserva)
{
	size_t need = name ? (strlen(name) + 1) * 2 : 0;
	if (!name || need + reserva > cap) {
		fprintf(stderr, "Path too long for the FSX command (%zu bytes of UCS-2, room for %zu): %s\n", need,
		        cap > reserva ? cap - reserva : 0, name ? name : "(null)");
		return 0;
	}
	return ascii_to_ucs2(name, dst);
}
int csloader_read_gdfs_var(struct sp_port *port, uint8_t block, uint16_t unit, uint8_t *outbuf, size_t outsize)
{
	uint8_t cmd_buf[16]; /* el paquete mide 4 + 1 + 3 + 1 = 9: con [8] se salía 1 byte */
	uint8_t resp[2048];

	uint8_t gdfsvar[3] = {block, unit & 0xFF, (unit >> 8) & 0xFF};

	int cmd_len = cmd_encode_csloader_packet(CMD_CS_GDFS, CMD_GDFS_READVAR, gdfsvar, sizeof(gdfsvar), cmd_buf);
	if (cmd_len <= 0)
		return -1;

	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	int rcv_len = serial_read(port, resp, sizeof(resp), 10 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.length < GDFS_DATA_OFFSET)
		return 0;

	size_t datalen = repl.length - GDFS_DATA_OFFSET;
	if (outbuf && outsize >= datalen) {
		memcpy(outbuf, repl.data + GDFS_DATA_OFFSET, datalen);
		return (int)datalen;
	}

	return -1;
}

int csloader_gdfs_get_phonename(struct sp_port *port, struct phone_info *phone)
{
	uint8_t block, lo, hi;

	switch (phone->chip_id) {
	case DB2000:
		block = phone->is_z1010 ? 0x04 : 0x02;
		lo = 0x8F;
		hi = 0x0C;
		break;

	case DB2010_1:
	case DB2010_2:
		block = 0x02;
		lo = 0x8F;
		hi = 0x0C;
		break;

	case DB2020:
	case PNX5230:
		block = 0x02;
		lo = 0xBB;
		hi = 0x0D;
		break;

	default:
		fprintf(stderr, "Unsupported platform for GDFS test.\n");
		return -1;
	}

	uint8_t gdfsbuf[256];
	int len = csloader_read_gdfs_var(port, block, (hi << 8) | lo, gdfsbuf, sizeof(gdfsbuf));

	if (len <= 0)
		return -1;

	/* UCS-2LE a ASCII a mano: wcstombs espera wchar_t de 4 bytes en
	 * Linux/macOS (salía vacío) y se le pasaba un límite de hasta 256
	 * para un destino de 8. */
	size_t j = 0;
	for (int i = 0; i + 1 < len && j + 1 < sizeof(phone->phone_name); i += 2) {
		uint16_t c = gdfsbuf[i] | (gdfsbuf[i + 1] << 8);
		if (c == 0)
			break;
		phone->phone_name[j++] = (c < 0x20 || c >= 0x80) ? '?' : (c == '/' || c == '\\' || c == ':') ? '_' : (char)c;
	}
	phone->phone_name[j] = '\0';

	return 0;
}

int csloader_gdfs_get_cxc_article(struct sp_port *port, struct phone_info *phone)
{
	uint8_t block, lo, hi;

	switch (phone->chip_id) {
	case DB2010_1:
	case DB2010_2:
		block = 0x02;
		lo = 0xE9;
		hi = 0x0C;
		break;

	case DB2020:
	case PNX5230:
		block = 0x02;
		lo = 0x15;
		hi = 0x0E;
		break;

	default:
		return 0;
	}

	uint8_t gdfsbuf[257];
	int len = csloader_read_gdfs_var(port, block, (hi << 8) | lo, gdfsbuf, sizeof(gdfsbuf) - 1);
	if (len <= 0)
		return -1;
	gdfsbuf[len] = '\0'; /* strstr de abajo necesita el terminador */

	// gdfsbuf contains ASCII like:
	// "R4EA031     prgCXC1250466_GENERIC_DO"
	// "R11AA011    prg1205-7395_CHINA_JH"

	const char *start = strstr((char *)gdfsbuf, "R"); // All SE firmware version start with 'R'
	const char *prg = strstr((char *)gdfsbuf, "prg");
	if (!start || !prg)
		return -1;

	// trim spaces before "prg"
	const char *p = prg;
	while (p > start && isspace((unsigned char)*(p - 1)))
		p--;

	char fw_ver[16] = {0};
	char fw_name[64] = {0};

	size_t len_ver = p - start;
	if (len_ver >= sizeof(fw_ver))
		len_ver = sizeof(fw_ver) - 1;
	strncpy(fw_ver, start, len_ver);
	fw_ver[len_ver] = '\0';

	// skip "prg"
	prg += 3;
	while (*prg && isspace((unsigned char)*prg))
		prg++;

	strncpy(fw_name, prg, sizeof(fw_name) - 1);

	int n = snprintf(phone->cxc_article, sizeof(phone->cxc_article), "%s_%s", fw_ver, fw_name);
	if (n < 0 || (size_t)n >= sizeof(phone->cxc_article)) {
		fprintf(stderr, "CXC article too long (%d chars): %s_%s\n", n, fw_ver, fw_name);
		phone->cxc_article[0] = '\0';
		return -1;
	}

	return 0;
}

int csloader_gdfs_get_cxc_version(struct sp_port *port, struct phone_info *phone)
{
	uint8_t block, lo, hi;

	switch (phone->chip_id) {
	case DB2010_1:
	case DB2010_2:
		block = 0x02;
		lo = 0xEA;
		hi = 0x0C;
		break;

	case DB2020:
		block = 0x02;
		lo = 0x16;
		hi = 0x0E;
		break;

	default:
		fprintf(stderr, "Unsupported platform for GDFS CXC version.\n");
		return -1;
	}

	uint8_t gdfsbuf[64];
	int len = csloader_read_gdfs_var(port, block, (hi << 8) | lo, gdfsbuf, sizeof(gdfsbuf));
	if (len <= 0)
		return -1;

	strncpy(phone->cxc_version, (char *)gdfsbuf, sizeof(gdfsbuf));

	return 0;
}

/* Tamaño máximo de una variable en UNA orden WRITEVAR. 0x600 es el tope que
 * lleva upstream y es el valor por omisión; con --gdfs-max N se puede probar
 * uno mayor (hasta GDFS_VAR_ABS, lo que cabe en el campo de longitud). Los
 * GDFS reales traen unidades de hasta 5950 bytes (el emulador del W580 tiene
 * 6 por encima de 0x600): upstream las recortaba en silencio. */
#define GDFS_VAR_MAX 0x600
#define GDFS_VAR_ABS 0xF000

int csloader_write_gdfs_var(struct sp_port *port, uint8_t block, uint8_t lo, uint8_t hi, uint8_t *data, uint32_t size)
{
	if (size > GDFS_VAR_ABS) {
		fprintf(stderr, "\nGDFS var %02x/%02X%02X: %u bytes do not fit one write packet\n", block, hi, lo, size);
		return -1;
	}

	int rc = -1;
	uint8_t *gdfs_var = malloc(7 + (size_t)size);
	uint8_t *cmd_buf = malloc((size_t)size + 7 + 8);
	if (!gdfs_var || !cmd_buf)
		goto fin;

	gdfs_var[0] = block;
	gdfs_var[1] = lo;
	gdfs_var[2] = hi;
	gdfs_var[3] = (uint8_t)(size);
	gdfs_var[4] = (uint8_t)(size >> 8);
	gdfs_var[5] = (uint8_t)(size >> 16);
	gdfs_var[6] = (uint8_t)(size >> 24);
	if (size)
		memcpy(&gdfs_var[7], data, size);

	uint8_t resp[8];

	int cmd_len = cmd_encode_csloader_packet(CMD_CS_GDFS, CMD_GDFS_WRITEVAR, gdfs_var, size + 7, cmd_buf);
	if (cmd_len <= 0)
		goto fin;

	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		goto fin;

	/* Escribir en la NOR puede tardar más de 100 ms. */
	int rcv_len = serial_read(port, resp, sizeof(resp), 20 * TIMEOUT);
	if (rcv_len <= 0)
		goto fin;

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		goto fin;

	/* Como en el resto del CS loader: data[0] = subcomando, data[1] = estado,
	 * 0 = bien. Antes `cmd != 0x04 && estado != 0` solo fallaba si FALLABAN
	 * las dos cosas a la vez: un estado de error con cmd correcto pasaba. */
	if (repl.cmd != CMD_CS_GDFS || repl.length < 2 || repl.data[1] != 0x00) {
		fprintf(stderr, "\nGDFS write refused by the phone: reply cmd %02X, status %02X\n", repl.cmd,
		        repl.length >= 2 ? repl.data[1] : 0xFF);
		goto fin;
	}

	rc = 0;
fin:
	free(gdfs_var);
	free(cmd_buf);
	return rc;
}

/* Recorre un volcado GDFS (formato de read-gdfs: u32 con el número de
 * variables, y por cada una block, lo, hi, u32 tamaño y los datos) y
 * comprueba que es coherente de punta a punta SIN tocar el teléfono. */
int csloader_gdfs_dump_check(const uint8_t *buf, size_t size, struct gdfs_dump_info *info)
{
	memset(info, 0, sizeof(*info));
	if (!buf || size < 4) {
		fprintf(stderr, "GDFS dump: file too short (%zu bytes)\n", size);
		return -1;
	}
	info->declared = get_word(buf);

	size_t pos = 4;
	while (pos < size) {
		if (size - pos < 7) {
			fprintf(stderr, "GDFS dump: truncated variable header at offset %zu\n", pos);
			return -1;
		}
		uint32_t varsize = get_word(buf + pos + 3);
		if (varsize > size - pos - 7) {
			fprintf(stderr, "GDFS dump: variable %02x/%02X%02X at offset %zu says %u bytes but only %zu remain\n",
			        buf[pos], buf[pos + 2], buf[pos + 1], pos, varsize, size - pos - 7);
			return -1;
		}
		if (varsize > info->maxsize) {
			info->maxsize = varsize;
			info->max_block = buf[pos];
			info->max_lo = buf[pos + 1];
			info->max_hi = buf[pos + 2];
		}
		pos += 7 + (size_t)varsize;
		info->found++;
	}
	info->used = pos;

	if (info->found != info->declared) {
		fprintf(stderr, "GDFS dump: header declares %u variables but the file holds %u\n", info->declared,
		        info->found);
		return -1;
	}
	return 0;
}

/* Coherencia + política de restauración. No toca el teléfono.
 * `max` = tamaño máximo por orden (0 = el de omisión); `skip_big` = en vez de
 * negarse, omitir las unidades mayores y dar la restauración por PARCIAL. */
static int gdfs_dump_restorable(const uint8_t *buf, size_t size, const char *fname, uint32_t max, int skip_big,
                                struct gdfs_dump_info *info, uint32_t *grandes)
{
	if (max == 0)
		max = GDFS_VAR_MAX;
	*grandes = 0;
	if (max > GDFS_VAR_ABS) {
		fprintf(stderr, "--gdfs-max %#x is above the %#x one write packet can carry. Nothing was written.\n", max,
		        GDFS_VAR_ABS);
		return -1;
	}
	if (csloader_gdfs_dump_check(buf, size, info) != 0 || info->found == 0) {
		fprintf(stderr, "Refusing to restore: %s is not a complete GDFS dump. Nothing was written.\n", fname);
		return -1;
	}
	for (size_t pos = 4; pos < size;) {
		uint32_t sz = get_word((uint8_t *)buf + pos + 3);
		if (sz > max)
			(*grandes)++;
		pos += 7 + (size_t)sz;
	}
	if (*grandes && !skip_big) {
		fprintf(stderr,
		        "Refusing to restore: %u unit(s) are larger than %#x bytes (the largest is %02x/%02X%02X, %u bytes).\n"
		        "  Upstream wrote them TRUNCATED and reported success; that is not done here. Nothing was written.\n"
		        "  --gdfs-max N   try one-packet writes up to N bytes (never tried on a real phone)\n"
		        "  --skip-big     restore everything else and report the restore as PARTIAL\n",
		        *grandes, max, info->max_block, info->max_hi, info->max_lo, info->maxsize);
		return -1;
	}
	return 0;
}

/* Para llamar ANTES de subir ningún loader al teléfono. */
int csloader_gdfs_dump_restorable(const char *fname, uint32_t max, int skip_big)
{
	size_t size;
	uint8_t *buf = load_file(fname, &size);
	if (!buf) {
		fprintf(stderr, "can't read %s\n", fname);
		return -1;
	}
	struct gdfs_dump_info info;
	uint32_t grandes;
	int rc = gdfs_dump_restorable(buf, size, fname, max, skip_big, &info, &grandes);
	free(buf);
	if (rc == 0) {
		printf("Dump verified: %u variables, largest %u bytes.\n", info.found, info.maxsize);
		if (grandes)
			printf("%u unit(s) above %#x bytes will be SKIPPED: the restore will be PARTIAL.\n", grandes,
			       max ? max : GDFS_VAR_MAX);
	}
	return rc;
}

int csloader_write_gdfs(struct sp_port *port, const char *inputfname, uint32_t max, int skip_big)
{
	printf("Restore GDFS...\n");
	if (max == 0)
		max = GDFS_VAR_MAX;
	size_t datasize;
	uint8_t *buf = load_file(inputfname, &datasize);
	if (!buf) {
		fprintf(stderr, "can't read %s\n", inputfname);
		return -1;
	}

	/* Todo el volcado se valida ANTES de escribir la primera variable: un
	 * archivo cortado, con la cuenta mal o con una variable que no cabe en
	 * un paquete no puede dejar la GDFS a medio restaurar. */
	struct gdfs_dump_info info;
	uint32_t grandes;
	if (gdfs_dump_restorable(buf, datasize, inputfname, max, skip_big, &info, &grandes) != 0) {
		free(buf);
		return -1;
	}
	printf("Dump verified: %u variables, largest %u bytes. Attempting to write them...\n", info.found,
	       info.maxsize);

	uint32_t varcount = 0, omitidas = 0;
	const uint8_t *bufpointer = &buf[4];
	for (uint32_t i = 0; i < info.found; i++) {
		uint8_t block = bufpointer[0];
		uint8_t lo = bufpointer[1];
		uint8_t hi = bufpointer[2];
		uint32_t varsize = get_word(bufpointer + 3);
		bufpointer += 7;

		if (varsize > max) { /* solo llega aquí con --skip-big */
			printf("\nSKIPPED block 0x%02x unit 0x%02X%02X: %u bytes (> %#x)\n", block, hi, lo, varsize, max);
			omitidas++;
			bufpointer += varsize;
			continue;
		}

		printf("\rWriting %.04d bytes to block 0x%.02x, unit 0x%02X%02X", varsize, block, hi, lo);

		if (csloader_write_gdfs_var(port, block, lo, hi, (uint8_t *)bufpointer, varsize) != 0) {
			free(buf);
			printf("\n\nWrote %i of %u units!\n", varcount, info.found);
			printf("GDFS was not fully restored!\n");
			return -1;
		}
		bufpointer += varsize;
		varcount++;
	}
	free(buf);
	printf("\n\nWrote %i variables!\n", varcount);
	if (omitidas) {
		printf("GDFS was only PARTIALLY restored: %u unit(s) were skipped (see SKIPPED above).\n", omitidas);
		return -1;
	}
	printf("GDFS was restored successfully!\n");

	return 0;
}

int csloader_read_gdfs(struct sp_port *port, struct phone_info *phone)
{
	printf("Back up GDFS...\n");

	uint8_t cmd_buf[8];
	uint8_t resp[0x10000] = {0};

	int cmd_len = cmd_encode_csloader_packet(CMD_CS_GDFS, CMD_GDFS_READALL, NULL, 0, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	if (serial_wait_ack(port, 100 * TIMEOUT) < 0)
		return -1;

	int rcv_len = serial_read(port, &resp[0], 10, 500 * TIMEOUT);
	if (rcv_len != 10)
		return -1;

	uint32_t datasize = (resp[2] | (resp[3] << 8)) + 1;
	uint32_t varcount = get_word(&resp[6]);

	printf("stated number of vars: %d\n", varcount);
	if (varcount == 0) {
		fprintf(stderr, "The phone reports 0 GDFS variables: that is not a backup. Nothing was saved.\n");
		return -1;
	}

	/* El volcado se escribe SIEMPRE en <nombre>.INCOMPLETO y solo se renombra al
	 * nombre definitivo cuando está entero y verificado. Así una lectura que
	 * falla (o un corte de luz, un Ctrl-C) no destruye el respaldo bueno que
	 * hubiera y nunca deja un .bin truncado con aspecto de bueno. */
	char outfile[512], parcial[560], anterior[560];
	snprintf(outfile, sizeof(outfile), "./backup/GDFS_%s_%s.bin", phone->phone_name, phone->otp_imei);
	snprintf(parcial, sizeof(parcial), "%s.INCOMPLETO", outfile);
	snprintf(anterior, sizeof(anterior), "%s.anterior", outfile);

	FILE *out = fopen(parcial, "wb");
	if (!out) {
		fprintf(stderr, "Can not open backup file\n");
		return -1;
	}

	/* Cada lectura tiene que llegar entera y caber en resp. Antes nada se
	 * comprobaba: una desincronía escribía más allá de los 64 KiB y un
	 * respaldo cortado se anunciaba como bueno. Lo que se manda al
	 * teléfono (los ack) no cambia. */
#define LEER(pos, n)                                                                                    \
	do {                                                                                            \
		if ((uint64_t)(pos) + (n) > sizeof(resp) ||                                             \
		    serial_read(port, &resp[(pos)], (n), 100 * TIMEOUT) != (int)(n))                    \
			goto incompleto;                                                                \
	} while (0)

	uint32_t bytesread = 10;
	uint32_t chunkcount = 0;
	for (uint32_t i = 0; i < varcount; i++) {
		/* En la frontera de un mensaje este byte es el de cierre (o puede no
		 * llegar antes del ack): ahí se tolera su falta y la siguiente lectura
		 * estricta lo delata si el flujo está roto. Fuera de la frontera es el
		 * primer byte de la variable y tiene que llegar. */
		if ((uint64_t)bytesread + 1 > sizeof(resp))
			goto incompleto;
		int cola = serial_read(port, &resp[bytesread], 1, 100 * TIMEOUT);

		if (bytesread >= datasize) {
			printf("Found: %d units\n", chunkcount);
			if (fwrite(&resp[6], sizeof(uint8_t), bytesread - 6, out) != bytesread - 6)
				goto incompleto;
			serial_send_ack(port);
			LEER(0, 6);
			datasize = (resp[2] | (resp[3] << 8)) + 1;
			bytesread = 6;
			chunkcount = 0;
			LEER(bytesread, 1);
		} else if (cola != 1) {
			goto incompleto;
		}
		bytesread++;
		LEER(bytesread, 6);
		bytesread += 2;
		uint32_t varsize = get_word(&resp[bytesread]);
		bytesread += 4;
		LEER(bytesread, varsize);

		bytesread += varsize;
		chunkcount++;
	}
#undef LEER
	printf("Found: %d units\n\n", chunkcount);
	if (fwrite(&resp[6], sizeof(char), bytesread - 6, out) != bytesread - 6)
		goto incompleto;
	if (fflush(out) != 0)
		goto incompleto;
#ifndef _WIN32
	fsync(fileno(out));
#endif
	if (fclose(out) != 0) {
		out = NULL;
		goto incompleto;
	}
	out = NULL;

	/* El archivo que quedó en disco se relee y se recorre entero: es lo que
	 * write-gdfs va a necesitar para restaurar, así que si no pasa esta
	 * comprobación no es un respaldo. */
	{
		size_t fsz;
		uint8_t *chk = load_file(parcial, &fsz);
		struct gdfs_dump_info info;
		int bien = chk && csloader_gdfs_dump_check(chk, fsz, &info) == 0 && info.found == varcount &&
		           info.found > 0;
		free(chk);
		if (!bien)
			goto incompleto;
		printf("GDFS dump verified: %u variables, %zu bytes, largest variable %u bytes\n", info.found, fsz,
		       info.maxsize);
		if (info.maxsize > GDFS_VAR_MAX)
			printf("NOTE: the largest variable (%02x/%02X%02X, %u bytes) is above %#x: write-gdfs will need "
			       "--gdfs-max or --skip-big to restore it.\n",
			       info.max_block, info.max_hi, info.max_lo, info.maxsize, GDFS_VAR_MAX);
	}

	/* Verificado: pasa a su nombre definitivo. Un respaldo bueno anterior no se
	 * destruye, se aparta a .anterior. */
	if (access(outfile, F_OK) == 0)
		rename(outfile, anterior);
	if (rename(parcial, outfile) != 0) {
		fprintf(stderr, "Can not move %s to %s\n", parcial, outfile);
		return -1;
	}

	printf("GDFS saved %s\n", outfile);
	printf("GDFS backup successfully!\n");

	// read the byte left on the phone queue
	uint8_t byteleft;
	serial_read(port, &byteleft, 1, 50 * TIMEOUT);

	return 0;

incompleto:
	if (out)
		fclose(out);
	fprintf(stderr, "\nGDFS backup INCOMPLETE: partial data left in %s, do NOT use it to restore\n", parcial);
	return -1;
}

int csloader_parse_gdfs_script(struct sp_port *port, const char *inputfname, const char *outputfname)
{
	printf("\nRun GDFS-script... %s\n", inputfname);

	FILE *f = fopen(inputfname, "r");
	if (!f) {
		fprintf(stderr, "GDFS-Script: Error (Couldn't open input file!)\n");
		return -1;
	}

	FILE *fout = fopen(outputfname, "w");
	if (!fout) {
		fprintf(stderr, "GDFS-Script: Error (Couldn't open output file!)\n");
		fclose(f);
		return -1;
	}

	// --- Write header at top ---
	time_t now = time(NULL);
	char timestr[64];
	strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", localtime(&now));
	fprintf(fout, "; Created with seftool\n; Creation time and date: %s\n\n", timestr);

	char linebuf[4096];
	uint32_t varcount = 0, varreadcount = 0, fallos = 0;

	while (fgets(linebuf, sizeof(linebuf), f)) {
		/* Una línea más larga que el búfer llega en trozos: se descarta el
		 * resto en vez de interpretarlo como otra línea. */
		size_t ll = strlen(linebuf);
		if (ll > 0 && linebuf[ll - 1] != '\n' && !feof(f)) {
			int c;
			while ((c = fgetc(f)) != EOF && c != '\n')
				;
			fprintf(stderr, "Warning: script line too long (over %zu bytes), skipped\n", sizeof(linebuf) - 1);
			fallos++;
			continue;
		}

		// skip empty or comment lines
		if (linebuf[0] == '\0' || linebuf[0] == '\n' || linebuf[0] == '#' || linebuf[0] == ';')
			continue;

		if (strncmp(linebuf, "gdfswrite:", 10) == 0) {
			uint32_t block, hi, lo;
			char datahex[0x680 * 2 + 1] = {0};

			/* Ancho explícito: sin él una línea con más de 3328 dígitos
			 * hexadecimales desbordaba datahex. */
			int n = sscanf(linebuf + 10, "%4x%2x%2x%3328[0-9A-Fa-f]", &block, &hi, &lo, datahex);
			if (n >= 3) {
				uint8_t vardata[0x680];
				size_t datalen = 0;
				size_t hexlen = strlen(datahex);

				if (hexlen % 2 != 0) {
					fprintf(stderr, "GDFS var %.04X/%02X%02X: odd number of hex digits, skipped\n", block, hi, lo);
					fallos++;
					continue;
				}
				/* Más digitos de los que caben: la línea trae datos de más. */
				if (linebuf[10 + 8 + hexlen] != '\0' && linebuf[10 + 8 + hexlen] != '\n' &&
				    linebuf[10 + 8 + hexlen] != '\r') {
					fprintf(stderr, "GDFS var %.04X/%02X%02X: data longer than %#x bytes, skipped\n", block, hi, lo,
					        (unsigned)sizeof(vardata));
					fallos++;
					continue;
				}

				for (char *p = datahex; *p && *(p + 1); p += 2) {
					unsigned byte;
					if (sscanf(p, "%2x", &byte) != 1)
						break;
					vardata[datalen++] = (uint8_t)byte;
					if (datalen >= sizeof(vardata))
						break;
				}

				printf("Writing GDFS var %.04X/%02X%02X\n", block, hi, lo);
				if (csloader_write_gdfs_var(port, block, lo, hi, vardata, datalen) != 0) {
					fprintf(stderr, "Failed to write GDFS var %.04X/%02X%02X\n", block, hi, lo);
					fallos++;
				}
				varcount++;
			}
		} else if (strncmp(linebuf, "gdfsread:", 9) == 0) {
			uint32_t block, hi, lo;
			if (sscanf(linebuf + 9, "%4x%2x%2x", &block, &hi, &lo) == 3) {
				uint8_t vardata[0x10000];
				memset(vardata, 0, sizeof(vardata));

				uint16_t unit = (hi << 8) | lo;
				int len = csloader_read_gdfs_var(port, (uint8_t)block, unit, vardata, sizeof(vardata));

				if (len > 0) {
					printf("Reading GDFS var %.04X/%02X%02X\n", block, hi, lo);
					fprintf(fout, "gdfswrite:%04X%02X%02X", block, hi, lo);
					for (int i = 0; i < len; i++)
						fprintf(fout, "%02X", vardata[i]);
					fprintf(fout, "\n");

					varreadcount++;
				} else {
					fprintf(stderr, "Failed to read GDFS var %.04X/%02X%02X\n", block, hi, lo);
				}
			}
		} else {
			fprintf(stderr, "Warning: Unknown or unsupported script line skipped: %s", linebuf);
		}
	}

	fclose(f);
	fclose(fout);

	printf("Wrote %u variables!\n", varcount);
	printf("Read %u variables to %s!\n", varreadcount, outputfname);
	if (varreadcount == 0)
		remove(outputfname);

	if (fallos) {
		fprintf(stderr, "GDFS-Script finished WITH %u ERROR(S): check the messages above\n", fallos);
		return -1;
	}
	printf("GDFS-Script was run successfully!\n");
	return 0;
}

// FSX Operation

int csloader_start_fsx_server(struct sp_port *port)
{
	printf("Starting FSX server...");
	uint8_t cmd_buf[8];
	int cmd_len = cmd_encode_csloader_packet(CMD_CS_FSX, CMD_FSX_START, NULL, 0, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	// --- send command
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	uint8_t resp[8];
	int rcv_len = serial_wait_packet(port, resp, sizeof(resp), 1000 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl = {0};
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.cmd != CMD_CS_FSX) {
		printf("failed\n");
		return -1;
	}

	printf("started\n");
	return 0;
}

int csloader_get_working_directory(struct sp_port *port, char *cwd, size_t max_len)
{
	if (!cwd || max_len == 0)
		return -1;

	uint8_t cmd_buf[16];
	struct packetdata_t repl = {0};

	// build GETDIR command
	int cmd_len = cmd_encode_csloader_packet(CMD_CS_FSX, CMD_FSX_GETDIR, NULL, 0, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	if (serial_wait_ack(port, 10 * TIMEOUT) != 0)
		return -1;

	uint8_t resp[256];
	int rcv_len = serial_read(port, resp, sizeof(resp), 3 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.cmd != CMD_CS_FSX || repl.length < 4)
		return -1;

	// repl.data[0..1] = status code
	if (repl.data[1] != 0)
		return -1;

	// repl.data[2...] = UCS2 string (null-terminated)
	size_t cwd_len;
	ucs2_to_ascii(repl.data, repl.length, GDFS_DATA_OFFSET, cwd, max_len, &cwd_len);

	return cwd_len;
}

int csloader_change_directory(struct sp_port *port, const char *dirname)
{
	// printf("cd %s\n", dirname);

	uint8_t data[0x100];
	size_t size = 0;

	size = path_ucs2(dirname, data, sizeof(data), 0);
	if (size == 0)
		return -1;

	uint8_t cmd_buf[0x100 + 7];
	int cmd_len = cmd_encode_csloader_packet(CMD_CS_FSX, CMD_FSX_CHDIR, data, size, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	uint8_t resp[32];
	struct packetdata_t repl = {0};
	int rcv_len = serial_read(port, resp, sizeof(resp), 3 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.cmd != CMD_CS_FSX || repl.length < 2)
		return -1;
	if (repl.data[1] != 0)
		return -1;

	return 0;
}

int csloader_stat(struct sp_port *port, const char *filename, ose_stat_t *st)
{
	if (!filename || !st)
		return -1;

	uint8_t data[0x256];
	uint8_t cmd_buf[sizeof(data) + 8];
	struct packetdata_t repl;

	size_t size = path_ucs2(filename, data, sizeof(data), 0);
	if (size == 0)
		return -1;

	int cmd_len = cmd_encode_csloader_packet(CMD_CS_FSX, CMD_FSX_STAT, data, size, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	uint8_t resp[0x20];
	int rcv_len = serial_read(port, resp, sizeof(resp), 3 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;
	if (repl.length < sizeof(ose_stat_t) || repl.data[1] != 0)
		return -1;

	// --- Copy into ose_stat_t ---
	memcpy(st, &repl.data[2], sizeof(ose_stat_t));

	uint16_t attr = st->st_mode; // swap
	st->st_mode = 0;

	// --- Base permissions ---
	if (attr & 0x0001)
		st->st_mode |= OSEATTR_EXECUTABLE;
	if (attr & 0x0002)
		st->st_mode |= OSEATTR_WRITEABLE;
	if (attr & 0x0004)
		st->st_mode |= OSEATTR_READABLE;

	// --- Owner / group bits if they exist ---
	if (attr & 0x0010)
		st->st_mode |= OSEATTR_OWNER_WRITEABLE;
	if (attr & 0x0020)
		st->st_mode |= OSEATTR_OWNER_READABLE;

	// --- Directory flag ---
	if (attr & 0x4000)
		st->st_mode |= OSEATTR_DIRECTORY;

	return 0;
}

// --- Helper macro ---
#define ADD_ENTRY(src_name, d_type_flag)                                                                               \
	do {                                                                                                           \
		if (count >= capacity) {                                                                               \
			capacity = capacity ? capacity * 2 : 16;                                                       \
			cs_entry_t *tmp = realloc(list, capacity * sizeof(cs_entry_t));                                \
			if (!tmp) {                                                                                    \
				free(list);                                                                            \
				return -1;                                                                             \
			}                                                                                              \
			list = tmp;                                                                                    \
		}                                                                                                      \
		strncpy(list[count].name, src_name, sizeof(list[count].name) - 1);                                     \
		list[count].name[sizeof(list[count].name) - 1] = '\0';                                                 \
		list[count].d_type = d_type_flag;                                                                      \
		count++;                                                                                               \
	} while (0)

int csloader_list(struct sp_port *port, cs_entry_t **out_list, size_t *out_count)
{
	if (!out_list || !out_count)
		return -1;

	uint8_t resp[0x1000] = {0};
	struct packetdata_t repl;
	size_t pos;

	cs_entry_t *list = NULL;
	size_t count = 0;
	size_t capacity = 0;

	// --- Directories ---
	uint8_t cmd_buf[16];
	int cmd_len = cmd_encode_csloader_packet(CMD_CS_FSX, CMD_FSX_LISTDIRS, NULL, 0, cmd_buf);
	if (cmd_len <= 0)
		goto fail;
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		goto fail;

	int rcv_len = serial_read(port, resp, sizeof(resp), 10 * TIMEOUT);
	if (rcv_len <= 0)
		goto fail;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		goto fail;
	if (repl.length < 4 || repl.data[1] != 0)
		goto fail;

	pos = 2;
	uint16_t numdirs = *(uint16_t *)&repl.data[pos];
	pos += 2;

	for (int i = 0; i < numdirs; i++) {
		char dirname[256];
		size_t len;
		if (ucs2_to_ascii(repl.data, repl.length, pos, dirname, sizeof(dirname), &len) != 0)
			goto fail;

		ADD_ENTRY(dirname, 1); // Directory
		pos += len;
	}

	// --- Files ---
	cmd_len = cmd_encode_csloader_packet(CMD_CS_FSX, CMD_FSX_LISTFILES, NULL, 0, cmd_buf);
	if (cmd_len <= 0)
		goto fail;
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		goto fail;

	rcv_len = serial_read(port, resp, sizeof(resp), 10 * TIMEOUT);
	if (rcv_len <= 0)
		goto fail;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		goto fail;
	if (repl.length < 4 || repl.data[1] != 0)
		goto fail;

	pos = 2;
	uint16_t numfiles = *(uint16_t *)&repl.data[pos];
	pos += 2;

	for (int i = 0; i < numfiles; i++) {
		char filename[256];
		size_t len;
		if (ucs2_to_ascii(repl.data, repl.length, pos, filename, sizeof(filename), &len) != 0)
			goto fail;

		ADD_ENTRY(filename, 0); // File
		pos += len;
	}

	*out_list = list;
	*out_count = count;
	return 0;

fail:
	free(list);
	return -1;
}

int csloader_make_directory(struct sp_port *port, const char *dirname)
{
	if (!dirname) {
		fprintf(stderr, "Invalid argument to csloader_make_directory()\n");
		return -1;
	}

	// printf("mkdir %s\n", dirname);

	uint8_t data[0x100];
	size_t size = 0;

	// UCS2 dirname (quedan 4 bytes para los atributos de abajo)
	size = path_ucs2(dirname, data, sizeof(data), 4);
	if (size == 0)
		return -1;

	// Attributes (DWORD, little endian)
	uint32_t ose_attr = OSEATTR_WRITEABLE | OSEATTR_READABLE;
	data[size++] = (uint8_t)(ose_attr & 0xFF);
	data[size++] = (uint8_t)((ose_attr >> 8) & 0xFF);
	data[size++] = (uint8_t)((ose_attr >> 16) & 0xFF);
	data[size++] = (uint8_t)((ose_attr >> 24) & 0xFF);

	uint8_t cmd_buf[0x100 + 7] = {0};
	int cmd_len = cmd_encode_csloader_packet(CMD_CS_FSX, CMD_FSX_MKDIR, data, size, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	// --- send command
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	uint8_t resp[8];
	int rcv_len = serial_wait_packet(port, resp, sizeof(resp), 3 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.cmd != CMD_CS_FSX) {
		fprintf(stderr, "Bad reply %02X expected %02X\n", repl.cmd, CMD_CS_FSX);
		return -1;
	}

	// if (repl.data[1] == 0)
	//     printf("OK\n");
	// else if (repl.data[1] == 0x6)
	//     printf("dir already exist\n");
	// else
	//     printf("Not OK\n");

	return 0;
}

int csloader_delete_file(struct sp_port *port, const char *filename)
{
	// printf("rmfile %s\n", filename);

	uint8_t data[1000] = {0};
	size_t size = path_ucs2(filename, data, sizeof(data), 0);
	if (size == 0)
		return -1;

	uint8_t cmd_buf[sizeof(data) + 8] = {0};
	int cmd_len = cmd_encode_csloader_packet(CMD_CS_FSX, CMD_FSX_RMFILE, data, size, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	// --- send command
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	uint8_t resp[8];
	int rcv_len = serial_read(port, resp, sizeof(resp), 10 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.cmd != CMD_CS_FSX) {
		fprintf(stderr, "Bad reply %02X expected %02X\n", repl.cmd, CMD_CS_FSX);
		return -1;
	}

	return 0;
}

int csloader_delete_directory(struct sp_port *port, const char *dirname)
{
	// printf("rmdir %s\n", dirname);

	uint8_t data[1000] = {0};
	size_t size = path_ucs2(dirname, data, sizeof(data), 0);
	if (size == 0)
		return -1;

	uint8_t cmd_buf[sizeof(data) + 8] = {0};
	int cmd_len = cmd_encode_csloader_packet(CMD_CS_FSX, CMD_FSX_RMDIR, data, size, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	// --- send command
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	uint8_t resp[8] = {0};
	int rcv_len = serial_read(port, resp, sizeof(resp), 10 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.cmd != CMD_CS_FSX) {
		fprintf(stderr, "Bad reply %02X expected %02X\n", repl.cmd, CMD_CS_FSX);
		return -1;
	}

	return 0;
}

int csloader_put_file(struct sp_port *port, const char *name, size_t filesize, const uint8_t *filebuffer)
{
	if (!name || !filebuffer) {
		fprintf(stderr, "Invalid argument(s) passed to csloader_put_file()\n");
		return -1;
	}

	size_t name_len = strlen(name);
	char namepart[1024] = {0};
	/* La primera orden lleva el nombre en UCS-2 + 8 bytes de atributos y
	 * tamaño dentro de 0x200: un nombre largo desbordaba el montón. */
	if (name_len >= sizeof(namepart) - 1 || (name_len + 1) * 2 + 8 >= 0x200) {
		fprintf(stderr, "Path too long for csloader_put_file(): %s\n", name);
		return -1;
	}
	for (size_t i = 0; i < name_len; i++) {
		namepart[i] = (char)name[i];
		namepart[i + 1] = '\0';

		// mkdir if directory is not exist
		if (name[i + 1] == '/')
			csloader_make_directory(port, namepart);
		// remove old file if already exist
		else if (name[i + 1] == '\0')
			csloader_delete_file(port, namepart);
	}

	// printf("putfile %s\n", name);

	uint8_t cmd_buf[6 + 0x400] = {0}; /* cabecera+subcmd + datos + checksum */
	size_t offset = 0;
	printf("\rwrite file: %s %zu/%zu bytes ", name, offset, filesize);

	while (offset < filesize) {
		size_t chunk_size;
		size_t size = 0;

		// --- allocate enough room for metadata + chunk ---
		size_t max_alloc = 0x400; // safe upper bound per block
		uint8_t *data = malloc(max_alloc);
		if (!data) {
			fprintf(stderr, "Failed to allocate memory (size=%zu)\n", max_alloc);
			return -1;
		}

		// --- first block: include filename and metadata ---
		if (offset == 0) {
			size += ascii_to_ucs2(name, &data[size]);

			// attributes
			uint32_t attr = (OSEATTR_READABLE | OSEATTR_WRITEABLE | OSEATTR_EXECUTABLE);
			data[size++] = (uint8_t)(attr);
			data[size++] = (uint8_t)(attr >> 8);
			data[size++] = (uint8_t)(attr >> 16);
			data[size++] = (uint8_t)(attr >> 24);

			// total file size (little endian)
			data[size++] = (uint8_t)(filesize);
			data[size++] = (uint8_t)(filesize >> 8);
			data[size++] = (uint8_t)(filesize >> 16);
			data[size++] = (uint8_t)(filesize >> 24);

			// compute chunk size = 0x200 - header
			chunk_size = 0x200 - size;
			if (chunk_size > filesize)
				chunk_size = filesize;
		} else {
			// subsequent chunks are up to 0x400 bytes
			chunk_size = (filesize - offset > 0x400) ? 0x400 : (filesize - offset);
		}

		// --- copy file data ---
		memcpy(&data[size], &filebuffer[offset], chunk_size);
		size += chunk_size;

		// --- encode into CS packet ---
		int cmd_len = cmd_encode_csloader_packet(CMD_CS_FSX, CMD_FSX_PUTFILE, data, size, cmd_buf);
		free(data);

		if (cmd_len <= 0)
			return -1;

		// --- send command
		if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
			return -1;

		uint8_t resp[8] = {0};
		int rcv_len = serial_wait_packet(port, resp, sizeof(resp), 10 * TIMEOUT);
		if (rcv_len <= 0)
			return -1;

		struct packetdata_t repl;
		if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
			return -1;

		/* Antes solo se miraba repl.cmd; un `CMD_CS_FSX` con `data[1]!=0` (ENOSPC,
		 * EACCES, …) daba por buena la escritura y el archivo quedaba truncado o
		 * sucio sin aviso. Mismo patrón que `csloader_write_gdfs_var`. */
		if (repl.cmd != CMD_CS_FSX || repl.length < 2 || repl.data[1] != 0x00) {
			fprintf(stderr,
			        "\nPutfile refused by phone: cmd 0x%02X status 0x%02X at offset %zu/%zu of %s\n",
			        repl.cmd,
			        repl.length >= 2 ? repl.data[1] : 0xFF,
			        offset,
			        filesize,
			        name);
			return -1;
		}

		offset += chunk_size;
		printf("\rwrite file: %s %zu/%zu bytes ", name, offset, filesize);
	}

	printf("\n");

	return 0;
}

int csloader_get_file(struct sp_port *port, const char *local_path, const char *fname)
{
	uint8_t data[0x256] = {0};
	uint8_t cmd_buf[sizeof(data) + 8] = {0};

	// --- Encode filename (UCS2) ---
	size_t size = path_ucs2(fname, data, sizeof(data), 0);
	if (size == 0)
		return -1;

	// --- Build FSX GETFILE command ---
	int cmd_len = cmd_encode_csloader_packet(CMD_CS_FSX, CMD_FSX_GETFILE, data, size, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	// --- Send command ---
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	uint8_t resp[0x1000 + 7] = {0};
	int rcv_len = serial_wait_packet(port, resp, sizeof(resp), 10 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	// --- Get file size ---
	uint32_t fsize = get_word(&resp[7]);
	uint32_t buf_size = 0x1000 - 0x7;

	// --- Small file fits entirely in first packet ---
	if (fsize < buf_size) {
		FILE *out = fopen(local_path, "wb");
		if (!out)
			return -1;

		printf("\rread file: %s %u bytes", fname, fsize);
		fflush(stdout);
		fwrite(&resp[0xB], 1, fsize, out);
		fclose(out);
		printf("\n");
		return 0;
	}

	// --- Multi-chunk path ---
	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	uint8_t *file_buf = malloc(fsize);
	if (!file_buf)
		return -1;

	uint32_t offset = 0;
	uint32_t first_chunk = repl.length - 6;

	memcpy(file_buf + offset, &repl.data[6], first_chunk);
	offset += first_chunk;

	while (offset < fsize) {
		if (serial_send_ack(port) != 0)
			goto exit_err;

		rcv_len = serial_read(port, resp, sizeof(resp), TIMEOUT);
		if (rcv_len <= 0)
			goto exit_err;

		if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
			goto exit_err;

		if (repl.data[1] != 0) {
			printf("\nBad data: %02X\n", repl.data[1]);
			goto exit_err;
		}

		uint32_t chunk_size = repl.length - 2;
		if (offset + chunk_size > fsize)
			chunk_size = fsize - offset;

		memcpy(file_buf + offset, &repl.data[2], chunk_size);
		offset += chunk_size;

		printf("\rread file: %s %u/%u bytes ", fname, offset, fsize);
		fflush(stdout);
	}

	FILE *out = fopen(local_path, "wb");
	if (!out)
		goto exit_err;

	fwrite(file_buf, 1, fsize, out);
	fclose(out);

	printf("\n");
	free(file_buf);
	return 0;

exit_err:
	free(file_buf);
	return -1;
}

int csloader_read_directory(struct sp_port *port, const char *src_dir, const char *dest_dir)
{
	// --- Create local directory ---
	MKDIR(dest_dir);

	// --- Step 1: change & confirm directory ---
	if (csloader_change_directory(port, src_dir) != 0)
		return -1;

	char cwd[256];
	int cwd_len = csloader_get_working_directory(port, cwd, sizeof(cwd));
	if (cwd_len <= 0)
		return -1;

	printf("read directory: %s\n", cwd);

	cs_entry_t *entries = NULL;
	size_t count = 0;
	if (csloader_list(port, &entries, &count) == 0) {
		for (size_t i = 0; i < count; i++) {
			const cs_entry_t *e = &entries[i];

			if (e->d_type) {
				// --- Build full paths for recursion ---
				char new_src[512], new_dest[512];
				if (strcmp(src_dir, "/") == 0)
					snprintf(new_src, sizeof(new_src), "/%s", e->name);
				else
					snprintf(new_src, sizeof(new_src), "%s/%s", src_dir, e->name);
				snprintf(new_dest, sizeof(new_dest), "%s/%s", dest_dir, e->name);

				// --- Recursively download subdirectory ---
				csloader_read_directory(port, new_src, new_dest);

				// --- Go back to previous directory after recursion
				csloader_change_directory(port, cwd);
			} else {
				// --- Build full local path ---
				char local_path[512];
				snprintf(local_path, sizeof(local_path), "%s/%s", dest_dir, e->name);
				csloader_get_file(port, local_path, e->name);
			}
		}
	}

	free(entries);
	return 0;
}

int csloader_write_directory(struct sp_port *port, const char *src_dir, const char *src_subdir, const char *dest_dir)
{
	size_t filesize = 0;
	uint8_t *filebuff = NULL;
	char local_filename[4096];
	char phone_filename[4096];
	char basedirname[2048];
	struct dirent **namelist = NULL;

	// Build current local directory
	if (strlen(src_subdir) > 0)
		snprintf(basedirname, sizeof(basedirname), "%s/%s", src_dir, src_subdir);
	else
		snprintf(basedirname, sizeof(basedirname), "%s", src_dir);

	int n = scandir(basedirname, &namelist, 0, alphasort);
	if (n < 0) {
		fprintf(stderr, "Error scanning directory %s\n", basedirname);
		return -1;
	}

	for (int i = 0; i < n; i++) {
		const char *entry = namelist[i]->d_name;
		if (entry[0] == '.') {
			free(namelist[i]);
			continue;
		}

		char local_path[4096];
		snprintf(local_path, sizeof(local_path), "%s/%s", basedirname, entry);

		if (is_directory(local_path)) {
			char newsub[4096];
			if (strlen(src_subdir) > 0)
				snprintf(newsub, sizeof(newsub), "%s/%s", src_subdir, entry);
			else
				snprintf(newsub, sizeof(newsub), "%s", entry);

			if (csloader_write_directory(port, src_dir, newsub, dest_dir) < 0) {
				free(namelist[i]);
				free(namelist);
				return -1;
			}
		} else {
			// --- Build full local filename ---
			if (strlen(src_subdir) > 0)
				snprintf(local_filename, sizeof(local_filename), "%s/%s/%s", src_dir, src_subdir,
				         entry);
			else
				snprintf(local_filename, sizeof(local_filename), "%s/%s", src_dir, entry);

			// --- Derive phone path ---
			const char *rel_path = local_filename + strlen(src_dir);
			while (*rel_path == '/' || *rel_path == '\\')
				rel_path++; // strip leading slash

			if (strcmp(dest_dir, "/") == 0)
				snprintf(phone_filename, sizeof(phone_filename), "/%s", rel_path);
			else
				snprintf(phone_filename, sizeof(phone_filename), "%s/%s", dest_dir, rel_path);

			filebuff = load_file(local_filename, &filesize);
			if (!filebuff) {
				fprintf(stderr, "Failed to read %s\n", local_filename);
				free(namelist[i]);
				continue;
			}

			int ret = 0;
			if (filesize == 0)
				ret = csloader_delete_file(port, phone_filename);
			else
				ret = csloader_put_file(port, phone_filename, filesize, filebuff);

			free(filebuff);
			if (ret < 0) {
				free(namelist[i]);
				free(namelist);
				return -1;
			}
		}

		free(namelist[i]);
	}

	free(namelist);
	return 0;
}

int csloader_upload_zip(struct sp_port *port, const char *zip_filename)
{
	/* Carpeta propia de seftool: antes ./tmp, y csloader_upload_zip la BORRABA
	 * (recursivamente) al empezar y al terminar, fuera lo que fuese. */
	const char *tmp_dir = "./.seftool_tmp";

	// --- clean tmp dir ---
	remove_recursive(tmp_dir);
	MKDIR(tmp_dir);

	printf("Extracting %s to %s ...\n", zip_filename, tmp_dir);
	if (zip_extract(zip_filename, tmp_dir) < 0) {
		fprintf(stderr, "Failed to extract ZIP archive\n");
		return -1;
	}

	printf("Uploading extracted files...\n");
	int ret = csloader_write_directory(port, tmp_dir, "", "/");

	// optional: cleanup after upload
	remove_recursive(tmp_dir);

	return ret;
}

int csloader_shutdown_fsx_server(struct sp_port *port)
{
	// shutdown
	printf("Shutdown FSX server... ");

	uint8_t cmd_buf[8];
	int cmd_len = cmd_encode_csloader_packet(CMD_CSLOADER, CMD_CS_SHUTDOWN, NULL, 0, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	uint8_t resp[8];
	int rcv_len = serial_wait_packet(port, resp, sizeof(resp), 10 * TIMEOUT);
	if (rcv_len <= 0) {
		printf("failed\n\n");
		return -1;
	}

	printf("OK\n\n");

	return 0;
}
