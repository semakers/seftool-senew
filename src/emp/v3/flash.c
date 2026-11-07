#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <libserialport.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#define access _access
#else
#include <unistd.h>
#endif

#include <core/common.h>
#include <core/serial.h>
#include <emp/common/cmd.h>
#include <utils/sha1.h>
#include <utils/vkp.h>

#include "babe.h"
#include "flash.h"
#include "loader.h"
#include "gdfs.h"

#define FLASH_OK 0
#define FLASH_ERROR -1

// ------------- Write to Flash -------------

int flash_babe(struct sp_port *port, uint8_t *babe_buf, size_t size, int flashfull)
{
	struct babehdr_t *hdr = (struct babehdr_t *)babe_buf;
	int fileformatver = hdr->ver;
	size_t hashsize = fileformatver >= 4 ? 20 : 1;
	int blocks = hdr->payloadsize1;
	size_t hdrsize = (fileformatver <= 2) ? 0x480 : blocks * hashsize + 0x380;

	/* La cabecera declarada no puede ser mayor que el archivo: el bucle de
	 * abajo mandaría memoria que no es del archivo. */
	if (size < sizeof(struct babehdr_t) || hdrsize > size) {
		printf("BABE header (%zu bytes) does not fit the file (%zu bytes)\n", hdrsize, size);
		return FLASH_ERROR;
	}

	// --- send header ---
	size_t curpos = 0;
	while (curpos < hdrsize) {
		int chunk = hdrsize - curpos;
		if (chunk > 0x800)
			chunk = 0x800;

		uint8_t cmd_buf[0x1000];
		int cmd_len = cmd_encode_binary_packet(0x0E, babe_buf + curpos, chunk, cmd_buf);
		if (cmd_len <= 0)
			return FLASH_ERROR;

		if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
			return FLASH_ERROR;

		uint8_t resp[7];
		int rcv_len = serial_read(port, resp, sizeof(resp), TIMEOUT);
		if (rcv_len <= 0)
			return FLASH_ERROR;

		struct packetdata_t repl;
		if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
			return FLASH_ERROR;

		if (repl.cmd != 0x0F || repl.length != 1 || repl.data[0] != 0x00) {
			printf("send header error\n");
			return FLASH_ERROR;
		}
		curpos += chunk;
	}

	// --- detect real block count ---
	curpos = hdrsize;
	int bl;
	for (bl = 0; bl < blocks; bl++) {
		if (curpos + 8 >= size)
			break;
		/* int32_t: con `long` (8 bytes en LP64) leía datos en vez del
		 * tamaño y salían 0 bloques, «0 blocks flashed ok». */
		int32_t bsize = (int32_t)get_word(babe_buf + curpos + 4);
		if (bsize > BLOCK_SIZE || bsize < 0)
			break;
		curpos += 8;
		if (curpos + bsize > size)
			break;
		curpos += bsize;
	}
	blocks = bl;
	printf("flashing %d blocks\n", blocks);

	// --- flash blocks ---
	curpos = hdrsize;
	for (bl = 0; bl < blocks; bl++) {
		if (curpos + 8 >= size)
			break;

		int addr_val = (int)get_word(babe_buf + curpos);
		int bsize = (int)get_word(babe_buf + curpos + 4);

		printf("\rflashing block %d/%d (addr %08X size %08X)", bl + 1, blocks, addr_val, bsize);
		fflush(stdout);

		// send block header
		uint8_t cmd_buf[0xD];
		int cmd_len = cmd_encode_binary_packet(0x10, babe_buf + curpos, 8, cmd_buf);
		if (cmd_len <= 0)
			return FLASH_ERROR;

		if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
			return FLASH_ERROR;

		if (serial_wait_ack(port, 5 * TIMEOUT) < 0)
			return FLASH_ERROR;

		if (bsize > BLOCK_SIZE || bsize < 0)
			break;

		curpos += 8;

		if ((curpos + bsize > size))
			break;

		// send block data
		while (bsize > 0) {
			uint8_t data_buf[4 + 0x800 + 1]; /* cabecera + datos + checksum */
			uint32_t tsize = (bsize > 0x800) ? 0x800 : bsize;
			cmd_len = cmd_encode_binary_packet(0x01, babe_buf + curpos, tsize, data_buf);
			if (cmd_len <= 0)
				return FLASH_ERROR;
			if (serial_write_chunks(port, data_buf, cmd_len, 0x400) < 0)
				return FLASH_ERROR;
			if (serial_wait_ack(port, 5 * TIMEOUT) < 0)
				return FLASH_ERROR;

			curpos += tsize;
			bsize -= tsize;
		}

		// wait for block reply
		uint8_t resp[6];
		int rcv_len = serial_wait_packet(port, resp, sizeof(resp), 25 * TIMEOUT);
		if (rcv_len <= 0)
			return FLASH_ERROR;

		struct packetdata_t repl;
		if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
			return FLASH_ERROR;

		if (repl.cmd != 0x13) {
			printf("\nunexpected reply during flash block: 0x%02X\n", repl.cmd);
			return FLASH_ERROR;
		}
		if (repl.length != 1 || repl.data[0] != 0x00) {
			printf("\nsend block error\n");
			return FLASH_ERROR;
		}
	}

	// --- finalization ---
	if (flashfull) {
		uint8_t cmd_buf[0x6];
		int cmd_len = cmd_encode_binary_packet(0x11, NULL, 0, cmd_buf);
		if (cmd_len <= 0)
			return FLASH_ERROR;

		if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
			return FLASH_ERROR;

		uint8_t resp[7];
		int rcv_len = serial_wait_packet(port, resp, sizeof(resp), 100 * TIMEOUT);
		if (rcv_len <= 0)
			return FLASH_ERROR;

		struct packetdata_t repl;
		if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
			return FLASH_ERROR;

		if (repl.cmd != 0x12) {
			printf("\nUnexpected reply during final check: 0x%02X\n", repl.cmd);
			return FLASH_ERROR;
		}
		if (repl.length != 1 || repl.data[0] != 0x00) {
			printf("final error\n");
			return FLASH_ERROR;
		}
	}

	printf("\n%d blocks flashed ok\n", blocks);

	return FLASH_OK;
}

static int babe_check_report(int babe)
{
	switch (babe) {
	case CHECKBABE_NOTBABE:
		printf("This is not BABE file\n");
		return FLASH_ERROR;

	case CHECKBABE_BADFILE:
		printf("Bad BABE file\n");
		return FLASH_ERROR;

	case CHECKBABE_CANTCHECK:
		printf("Can not check BABE file\n");
		return FLASH_ERROR;

	case CHECKBABE_NOTFULL:
		printf("This is not full BABE file\n");
		return FLASH_ERROR;

	case CHECKBABE_OK:
		return FLASH_OK;

	default:
		printf("Unknown BABE check result: %d\n", babe);
		return FLASH_ERROR;
	}
}

/* Comprobación PREVIA de un firmware completo (main o fs) que se va a
 * escribir en la NOR. No toca el teléfono. Va más allá de babe_check:
 *  - verifica los SHA-1 de todos los bloques (CHECKBABE_CHECKFULL);
 *  - exige que la tabla de bloques se recorra ENTERA y coincida con el
 *    recuento de la cabecera, y que ningún bloque pase de BLOCK_SIZE.
 * flash_babe acepta menos bloques de los declarados porque los .rest de
 * restauración llevan la cabecera de 460 bloques y solo 4 de contenido;
 * para un main/fs completo eso sería un archivo recortado y flash_babe
 * terminaría diciendo «ok» habiendo escrito la mitad. */
int flash_babe_precheck(const char *filename, int expect_cid, int force)
{
	size_t size;
	uint8_t *buffer = load_file(filename, &size);
	if (!buffer || size < sizeof(struct babehdr_t)) {
		printf("Can not read %s (or shorter than a BABE header)\n", filename);
		free(buffer);
		return FLASH_ERROR;
	}

	/* Verificación completa de SHA-1 por bloque. Una imagen de Sony pasa
	 * (main y fs del W580: OK). Una imagen hecha por `convert raw2babe`
	 * (la de senew) no lleva hashes ni firma que comprobar y da CANTCHECK:
	 * en ese caso solo vale la comprobación estructural de abajo y se avisa.
	 * Un BADFILE (hash que no coincide, bloque fuera del archivo) siempre se
	 * rechaza. */
	int rc;
	int full = babe_check(buffer, size, CHECKBABE_CHECKFULL);
	if (full == CHECKBABE_CANTCHECK) {
		printf("%s: no signature/hash data to verify (custom image); checking the block table only\n", filename);
		rc = babe_check_report(babe_check(buffer, size, CHECKBABE_CHECKFAST));
	} else {
		rc = babe_check_report(full);
	}
	if (rc != FLASH_OK) {
		free(buffer);
		return rc;
	}

	struct babehdr_t *hdr = (struct babehdr_t *)buffer;
	size_t hashsize = hdr->ver >= 4 ? 20 : 1;
	uint32_t declared = hdr->payloadsize1;

	/* babe_check deja pasar una cuenta de 0 o con el bit alto puesto. */
	if (declared == 0 || declared > 0x10000) {
		printf("%s: absurd block count in the header (%u)\n", filename, declared);
		free(buffer);
		return FLASH_ERROR;
	}

	size_t cur = (hdr->ver <= 2) ? 0x480 : (size_t)declared * hashsize + 0x380;
	uint32_t walked = 0;
	uint64_t amin = UINT64_MAX, amax = 0;
	int desalineado = 0;

	while (walked < declared) {
		if (cur > size || size - cur < 8)
			break;
		uint32_t addr = get_word(buffer + cur);
		uint32_t bsize = get_word(buffer + cur + 4);
		if (bsize > BLOCK_SIZE || bsize > size - cur - 8)
			break;
		if (addr < amin)
			amin = addr;
		if ((uint64_t)addr + bsize > amax)
			amax = (uint64_t)addr + bsize;
		if (addr % BLOCK_SIZE)
			desalineado = 1;
		cur += 8 + (size_t)bsize;
		walked++;
	}
	if (walked != declared) {
		printf("%s: header declares %u blocks but only %u are valid (stops at offset %zu); refusing to write a "
		       "partial image\n",
		       filename, declared, walked, cur);
		rc = FLASH_ERROR;
	} else {
		printf("%s: %u blocks, %s, %zu bytes%s\n", filename, declared,
		       full == CHECKBABE_OK ? "SHA-1 verified" : "block table verified", cur,
		       cur == size ? "" : " (extra bytes after the last block)");
		printf("%s: writes NOR %#llx..%#llx\n", filename, (unsigned long long)amin, (unsigned long long)amax);

		/* Guardas de lo que se escribiría. Una imagen hecha con raw2babe lleva
		 * plataforma y CID a 0 (nada que comparar); una de Sony los lleva. */
		const char *motivo = NULL;
		if (amin < NOR_WRITE_MIN || amax > NOR_WRITE_MAX)
			motivo = "writes outside the main/fs window (below the main start is boot, CID and security units)";
		else if (desalineado)
			motivo = "block addresses are not aligned to 64 KiB";
		else if (hdr->platform != 0 && hdr->platform != CHIPID_DB2020)
			motivo = "the header is not for a DB2020";
		else if (hdr->cid != 0 && expect_cid != 0 && (int)hdr->cid != expect_cid)
			motivo = "the image CID differs from the phone's active CID";
		if (motivo) {
			if (force) {
				printf("%s: WARNING (%s): continuing because it was forced\n", filename, motivo);
			} else {
				printf("%s: REFUSED (%s). SEFTOOL_SENEW_FORCE=1 overrides this on purpose.\n", filename, motivo);
				rc = FLASH_ERROR;
			}
		}
	}
	free(buffer);
	return rc;
}

int flash_babe_fw(struct sp_port *port, const char *filename, int fw_check)
{
	printf("\nflashing babe: %s\n", filename);

	size_t size;
	uint8_t *buffer = load_file(filename, &size);
	if (!buffer || size < 0x380) {
		printf("Can not read %s (or shorter than a BABE header)\n", filename);
		free(buffer);
		return FLASH_ERROR;
	}

	if (babe_check_report(babe_check(buffer, size, fw_check)) != FLASH_OK)
		goto exit_error;

	if (flash_babe(port, buffer, size, 1) == 0) {
		free(buffer);
		return FLASH_OK;
	}

exit_error:
	free(buffer);
	return FLASH_ERROR;
}

uint8_t *flash_convert_raw_to_babe(uint8_t *raw, size_t size, uint32_t raw_addr, size_t *babe_size_out)
{
	size_t numblocks = (size + 0xFFFF) / BLOCK_SIZE;
	size_t babesize = numblocks * (8 + 1) + size + sizeof(struct babehdr_t);

	/* calloc: la cabecera (color, CID, hashes, firma…) y los hashes por bloque
	 * salían de malloc, es decir, basura del montón que acababa en el archivo
	 * y en lo que se manda al teléfono. */
	uint8_t *babe = calloc(1, babesize);
	if (!babe)
		return NULL;

	struct babehdr_t *babe_hdr = (struct babehdr_t *)babe;
	babe_hdr->sig = 0xBEBA;
	babe_hdr->ver = 3;
	babe_hdr->payloadsize1 = numblocks;

	size_t pos = sizeof(struct babehdr_t) + numblocks;
	uint8_t *rawp = raw;
	size_t left = size;

	while (left > 0) {
		int bsize = left > BLOCK_SIZE ? BLOCK_SIZE : left;
		/* pos = 896 + numblocks puede ser impar: escritura byte a byte */
		set_word(babe + pos, raw_addr);
		pos += 4;
		set_word(babe + pos, (uint32_t)bsize);
		pos += 4;
		memcpy(babe + pos, rawp, bsize);
		pos += bsize;

		rawp += bsize;
		left -= bsize;
		raw_addr += bsize;
	}

	if (babe_size_out)
		*babe_size_out = babesize;

	return babe;
}

int flash_cnv_babe_to_raw_file(const char *babe_filename, const char *raw_filename)
{
	size_t fsize;
	uint8_t *buf = load_file(babe_filename, &fsize);
	if (!buf) {
		fprintf(stderr, "can't read %s\n", babe_filename);
		return -1;
	}

	struct babehdr_t *hdr = (struct babehdr_t *)buf;
	if (hdr->sig != 0xBEBA) {
		fprintf(stderr, "Error: not a BABE file\n");
		free(buf);
		return -1;
	}

	uint8_t ver = hdr->ver;
	uint32_t blocks = hdr->payloadsize1;
	uint32_t bsize = hdr->payloadsize2;

	printf("BABE version %u, blocks=%u, blocksize=0x%X\n", ver, blocks, bsize);

	// decide hash mode by "ver"
	size_t hptr = sizeof(struct babehdr_t);
	size_t dptr = 0;
	if (ver == 2)
		dptr = hptr + 0x100; // static hash
	else if (ver == 3)
		dptr = blocks + hptr; // dynamic hash
	else if (ver == 4)
		dptr = (size_t)blocks * 20 + hptr; // full hash
	else {
		fprintf(stderr, "Error: Unknown hash type %u\n", ver);
		free(buf);
		return -1;
	}

	FILE *out = fopen(raw_filename, "wb");
	if (!out) {
		fprintf(stderr, "Failed to open raw file (%s)\n", raw_filename);
		free(buf);
		return -1;
	}

	uint32_t offset = get_word(buf + dptr);
	size_t curptr = dptr;

	printf("Converting...\n");

	for (uint32_t i = 0; i < blocks; i++) {
		uint32_t cur_bsize = get_word(buf + curptr + 4);
		uint32_t cur_bdata = get_word(buf + curptr) - offset;

		if (cur_bdata > BLOCK_SIZE)
			cur_bdata = 0;

		offset += cur_bdata;

		if (curptr + 8 + cur_bsize > fsize)
			break;

		if (fwrite(buf + curptr + 8, 1, cur_bsize, out) != cur_bsize) {
			fclose(out);
			free(buf);
			fprintf(stderr, "Failed to write raw file (%s)\n", babe_filename);
			return -1;
		}

		offset += cur_bsize;
		curptr += cur_bsize + 8;
	}

	fclose(out);
	free(buf);
	printf("Converted %s -> %s\n", babe_filename, raw_filename);
	return 0;
}

int flash_cnv_raw_to_babe_file(const char *raw_filename, const char *babe_filename, uint32_t raw_addr)
{
	size_t fsize;
	uint8_t *raw = load_file(raw_filename, &fsize);
	if (!raw) {
		fprintf(stderr, "can't read %s\n", raw_filename);
		return -1;
	}

	// Convert
	size_t babe_size;
	uint8_t *babe = flash_convert_raw_to_babe(raw, fsize, raw_addr, &babe_size);
	free(raw);

	if (!babe) {
		fprintf(stderr, "Failed to allocate babe buffer\n");
		return FLASH_ERROR;
	}

	// Write out babe
	FILE *fw = fopen(babe_filename, "wb");
	if (!fw) {
		free(babe);
		fprintf(stderr, "Failed to create babe file (%s)\n", babe_filename);
		return FLASH_ERROR;
	}

	if (fwrite(babe, 1, babe_size, fw) != babe_size) {
		fclose(fw);
		free(babe);
		fprintf(stderr, "Failed to write babe file (%s)\n", babe_filename);
		return FLASH_ERROR;
	}

	fclose(fw);
	free(babe);

	printf("Converted %s -> %s (%zu bytes babe)\n", raw_filename, babe_filename, babe_size);
	return FLASH_OK;
}

int flash_raw(struct sp_port *port, const char *filename, uint32_t raw_addr)
{
	size_t fsize;
	uint8_t *raw = load_file(filename, &fsize);
	if (!raw) {
		fprintf(stderr, "can't read %s\n", filename);
		return -1;
	}

	printf("converting raw->babe\n");
	size_t babesize;
	uint8_t *babe = flash_convert_raw_to_babe(raw, fsize, raw_addr, &babesize);
	free(raw);

	if (!babe) {
		fprintf(stderr, "Failed to allocate babe buffer\n");
		return FLASH_ERROR;
	}

	int ret = flash_babe(port, babe, babesize, 1);
	free(babe);
	return ret;
}

int flash_restore_boot_area(struct sp_port *port, struct phone_info *phone)
{
	if (check_restore_file(phone) == 0) {
		if (flash_detect_fw_version(port, phone) != 0)
			return -1;
	}

	printf("\nRestoring boot area\n");

	// Build expected REST filename
	char restfile[256];
	snprintf(restfile, sizeof(restfile), "./rest/%s.rest", phone->rest_name);

	// Check if file exists
	FILE *frest = fopen(restfile, "rb");
	if (frest) {
		fclose(frest);

		printf("Flashing REST: %s\n", restfile);

		size_t size;
		uint8_t *rest = load_file(restfile, &size);
		if (!rest) {
			fprintf(stderr, "can't read %s\n", restfile);
			return -1;
		}
		if (flash_babe(port, rest, size, 1) != 0) {
			free(rest);
			return -1;
		}
		free(rest);
	} else {
		fprintf(stderr, "Missing REST file: %s\n", restfile);
		// Try RAW
		char raw_file[256];
		snprintf(raw_file, sizeof(raw_file), "./rest/%s.raw", phone->rest_name);
		FILE *fraw = fopen(raw_file, "rb");
		if (!fraw) {
			fprintf(stderr, "Missing RAW file: %s\n", raw_file);
			return -1;
		}
		fclose(fraw);

		printf("Flashing RAW: %s\n", raw_file);

		if (phone->chip_id == PNX5230) {
			if (flash_raw(port, raw_file, 0x20100000) != 0)
				return -1;
		} else {
			if (flash_raw(port, raw_file, 0x44140000) != 0)
				return -1;
		}
	}

	return 0;
}

// ------------- Read from Flash -------------

// --- receive one flash block ---
int flash_recv_block(struct sp_port *port, uint32_t expected_addr, uint8_t *buf, int maxlen)
{
	// read 4-byte header
	uint8_t hdr_buf[4];
	int rcv_len = serial_read(port, hdr_buf, sizeof(hdr_buf), 5 * TIMEOUT);
	if (rcv_len != (int)sizeof(hdr_buf))
		return FLASH_ERROR;

	int length = get_half(&hdr_buf[2]); // little endian u16

	if (hdr_buf[0] != SERIAL_HDR89) {
		fprintf(stderr, "Bad HDR: 0x%02X\n", hdr_buf[0]);
		return FLASH_ERROR;
	}
	if (hdr_buf[1] != 0x33) {
		fprintf(stderr, "Unexpected CMD 0x33 got 0x%X\n", hdr_buf[1]);
		return FLASH_ERROR;
	}
	if (length < 6) {
		fprintf(stderr, "Bad reply size\n");
		return FLASH_ERROR;
	}

	// read block: `length` es un u16 que manda el teléfono, el búfer cubre
	// cualquier valor (antes 0x800: un paquete lleno de 0x800 + 6 lo pasaba)
	uint8_t resp[0x10000];
	rcv_len = serial_read(port, resp, length, 5 * TIMEOUT);
	if (rcv_len != length)
		return FLASH_ERROR;

	// read checksum
	uint8_t checksum;
	rcv_len = serial_read(port, &checksum, 1, 5 * TIMEOUT);
	if (rcv_len != 1)
		return FLASH_ERROR;

	// verify checksum
	uint8_t sum = 0;
	for (int i = 0; i < 4; i++)
		sum ^= hdr_buf[i];
	for (int i = 0; i < length; i++)
		sum ^= resp[i];
	sum = (sum + 7) & 0xFF;

	if (sum != checksum) {
		uint8_t nak = SERIAL_NAK;
		serial_write(port, &nak, 1);
		fprintf(stderr, "Checksum mismatch! got 0x%02X expected 0x%02X\n", checksum, sum);
		return FLASH_ERROR;
	}

	// parse address
	uint32_t rpl_addr = get_word(&resp[2]);
	if (rpl_addr != expected_addr) {
		fprintf(stderr, "Bad reply addr: expected 0x%08X got 0x%08X\n", expected_addr, rpl_addr);
		return FLASH_ERROR;
	}

	// copy data into buf
	int data_len = length - 6; // subtract 4-byte addr + 2-byte length
	if (data_len > maxlen)
		data_len = maxlen; // truncate if needed

	memcpy(buf, &resp[6], data_len);
	return data_len;
}

uint8_t *flash_read_raw(struct sp_port *port, uint32_t addr, size_t size)
{
	uint32_t addr_range[2] = {addr, addr + size};

	uint8_t cmd_buf[32];
	int cmd_len = cmd_encode_binary_packet(0x32, (uint8_t *)addr_range, sizeof(addr_range), cmd_buf);
	if (cmd_len <= 0)
		return NULL;

	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return NULL;

	uint8_t ack;
	int rcv_len = serial_read(port, &ack, 1, 50 * TIMEOUT);
	if (rcv_len <= 0)
		return NULL;

	uint8_t *buf = malloc(size);
	if (!buf)
		return NULL;

	size_t pos = 0;
	while (pos < size) {
		uint8_t tmp[0x800];
		int data_len = flash_recv_block(port, addr + pos, tmp, sizeof(tmp));
		if (data_len < 0) {
			free(buf);
			return NULL;
		}

		if (pos + data_len > size)
			data_len = size - pos;

		memcpy(buf + pos, tmp, data_len);
		pos += data_len;

		if (pos < size) {
			uint8_t ack = SERIAL_ACK;
			if (serial_write(port, &ack, 1) < 0) {
				free(buf);
				return NULL;
			}
		}
	}

	return buf; // caller must free()
}

int flash_scan_fw_version(struct sp_port *port, struct phone_info *phone, uint32_t addr, size_t size)
{
	uint8_t *buf = flash_read_raw(port, addr, size);
	if (!buf)
		return FLASH_ERROR;

	char fw_id[128];
	if (scan_fw_version(buf, size, fw_id, sizeof(fw_id)) == 0) {
		printf("\nFW Version: %s\n", fw_id);

		size_t len = strlen(fw_id);
		if (len >= sizeof(phone->rest_name))
			len = sizeof(phone->rest_name) - 1;

		memcpy(phone->rest_name, fw_id, len);
		phone->rest_name[len] = '\0';

		free(buf);
		return FLASH_OK;
	}

	free(buf);
	return FLASH_ERROR;
}

int flash_detect_fw_version(struct sp_port *port, struct phone_info *phone)
{
	if (phone->chip_id == PNX5230) {
		if (flash_scan_fw_version(port, phone, 0x216E0000, 4 * BLOCK_SIZE) == 0) // W350/W380/Z555
			return FLASH_OK;

		return flash_scan_fw_version(port, phone, 0x213FC000, BLOCK_SIZE); // Z310
	} else if (phone->chip_id == DB2000) {
		if (flash_scan_fw_version(port, phone, 0x21A00000, 8 * BLOCK_SIZE) == 0) // W900
			return FLASH_OK;

		return flash_scan_fw_version(port, phone, 0x21400000,
		                             4 * BLOCK_SIZE); // // K600/K608/V600
	} else if (phone->chip_id == DB2010_1 || phone->chip_id == DB2010_2) {
		if (flash_scan_fw_version(port, phone, 0x44800000, 8 * BLOCK_SIZE) == 0) // W200
			return FLASH_OK;
		if (flash_scan_fw_version(port, phone, 0x44880000, 16 * BLOCK_SIZE) == 0) // K510/W810/Z550
			return FLASH_OK;
		if (flash_scan_fw_version(port, phone, 0x44B00000, 4 * BLOCK_SIZE) == 0) // Z558
			return FLASH_OK;

		return flash_scan_fw_version(port, phone, 0x447C0000, 4 * BLOCK_SIZE);
	} else if (phone->chip_id == DB2020) {
		if (flash_scan_fw_version(port, phone, 0x45A00000, 16 * BLOCK_SIZE) == 0)
			return FLASH_OK;
		if (flash_scan_fw_version(port, phone, 0x45B00000, 16 * BLOCK_SIZE) == 0)
			return FLASH_OK;
	}

	printf("Unsupported chip id: %08X\n", phone->chip_id);
	return FLASH_ERROR;
}

/* ---- Ventana y plan de lectura ---- */

int flash_read_window_check(uint32_t addr, uint64_t size, const char **why)
{
	const char *motivo = NULL;

	/* Comparaciones por resta: addr + size en 64 bits también da la vuelta con un tamaño enorme. */
	if (size == 0)
		motivo = "el tamaño es 0";
	else if (size > 0xFFFFFFFFull - addr)
		motivo = "inicio + tamaño no caben en 32 bits (daría la vuelta)";
	else if (addr < NOR_READ_MIN)
		motivo = "la ventana empieza por debajo de la NOR (0x44000000)";
	else if (addr >= NOR_READ_MAX || size > (uint64_t)NOR_READ_MAX - addr)
		motivo = "la ventana acaba más allá de la NOR (0x48000000)";

	if (motivo) {
		if (why)
			*why = motivo;
		return -1;
	}
	return 0;
}

size_t flash_read_plan_count(uint64_t size) { return (size_t)(size / BLOCK_SIZE + (size % BLOCK_SIZE ? 1 : 0)); }

int flash_read_plan_at(uint32_t addr, uint64_t size, size_t index, uint32_t *chunk_addr, size_t *chunk_len)
{
	uint64_t off = (uint64_t)index * BLOCK_SIZE;
	if (off >= size)
		return -1;

	uint64_t left = size - off;
	*chunk_addr = (uint32_t)((uint64_t)addr + off);
	*chunk_len = left < BLOCK_SIZE ? (size_t)left : (size_t)BLOCK_SIZE;
	return 0;
}

/* ---- Volcado atómico ---- */

struct flash_dump {
	FILE *f;
	char path[1024]; /* nombre definitivo */
	char part[1056]; /* <path>.INCOMPLETO */
	char prev[1056]; /* <path>.anterior */
	size_t bytes;
	SHA1_CTX sha;
};

/* rename() no pisa un destino que ya existe en Windows. */
static int move_over(const char *from, const char *to)
{
#ifdef _WIN32
	remove(to);
#endif
	return rename(from, to);
}

struct flash_dump *flash_dump_open(const char *path)
{
	struct flash_dump *d = calloc(1, sizeof(*d));
	if (!d)
		return NULL;

	int n = snprintf(d->path, sizeof(d->path), "%s", path);
	if (n < 0 || (size_t)n >= sizeof(d->path)) {
		fprintf(stderr, "read-flash: output path too long (%.60s...)\n", path);
		free(d);
		return NULL;
	}
	snprintf(d->part, sizeof(d->part), "%s.INCOMPLETO", d->path);
	snprintf(d->prev, sizeof(d->prev), "%s.anterior", d->path);

	d->f = fopen(d->part, "wb");
	if (!d->f) {
		perror("open output");
		free(d);
		return NULL;
	}
	sha1_init(&d->sha);
	return d;
}

int flash_dump_write(struct flash_dump *d, const uint8_t *data, size_t len)
{
	if (fwrite(data, 1, len, d->f) != len) {
		perror("write output");
		return -1;
	}
	sha1_update(&d->sha, data, len);
	d->bytes += len;
	return 0;
}

void flash_dump_abort(struct flash_dump *d, const char *why)
{
	if (d->f)
		fclose(d->f);
	fprintf(stderr, "\nread-flash INCOMPLETE (%s): partial data left in %s, do NOT use it\n",
	        why ? why : "read failed", d->part);
	free(d);
}

int flash_dump_commit(struct flash_dump *d, size_t expected, char sha1_hex[41])
{
	int ok = 1;

	if (d->bytes != expected) {
		fprintf(stderr, "read-flash: %zu bytes were written, %zu were expected\n", d->bytes, expected);
		ok = 0;
	}
	if (ok && fflush(d->f) != 0) {
		perror("flush output");
		ok = 0;
	}
#ifndef _WIN32
	if (ok && fsync(fileno(d->f)) != 0) {
		perror("fsync output");
		ok = 0;
	}
#endif
	if (fclose(d->f) != 0) {
		perror("close output");
		ok = 0;
	}
	d->f = NULL;

	/* Lo que quedó en el disco, no lo que creemos haber escrito. */
	if (ok) {
		struct stat st;
		if (stat(d->part, &st) != 0 || (uint64_t)st.st_size != (uint64_t)expected) {
			fprintf(stderr, "read-flash: %s does not have the expected size (%zu bytes)\n", d->part, expected);
			ok = 0;
		}
	}
	if (!ok) {
		flash_dump_abort(d, "the dump could not be verified");
		return FLASH_ERROR;
	}

	/* Verificado: pasa a su nombre definitivo. Un volcado bueno anterior no se destruye,
	 * se aparta a .anterior. */
	int apartado = 0;
	if (access(d->path, 0) == 0) {
		if (move_over(d->path, d->prev) != 0) {
			fprintf(stderr, "read-flash: cannot move the previous dump %s aside\n", d->path);
			flash_dump_abort(d, "the previous dump could not be moved aside");
			return FLASH_ERROR;
		}
		apartado = 1;
	}
	if (move_over(d->part, d->path) != 0) {
		fprintf(stderr, "read-flash: cannot move %s to %s\n", d->part, d->path);
		if (apartado && move_over(d->prev, d->path) != 0)
			fprintf(stderr, "read-flash: ATENCIÓN: no pude devolver el volcado bueno anterior a su sitio; "
			                "está en %s\n", d->prev);
		flash_dump_abort(d, "the dump could not be published");
		return FLASH_ERROR;
	}

	BYTE hash[SHA1_BLOCK_SIZE];
	sha1_final(&d->sha, hash);
	for (int i = 0; i < SHA1_BLOCK_SIZE; i++)
		snprintf(sha1_hex + 2 * i, 3, "%02x", hash[i]);

	printf("Flash dump saved %s (%zu bytes, SHA-1 %s)\n", d->path, d->bytes, sha1_hex);
	free(d);
	return FLASH_OK;
}

int flash_read(struct sp_port *port, struct phone_info *phone, uint32_t addr, size_t size)
{
	/* La orden de lectura lleva {addr, addr+size} en 32 bits: si el final pasa de 0xFFFFFFFF (incluido
	 * addr+size == 2^32, que se trunca a 0), daría la vuelta. */
	if (size == 0 || (uint64_t)size > 0xFFFFFFFFull - addr) {
		fprintf(stderr, "read-flash: 0x%08X + 0x%zX is not a window that fits in 32 bits\n", addr, size);
		return FLASH_ERROR;
	}

	char rawfile[1024];
	snprintf(rawfile, sizeof(rawfile), "./backup/flashdump_%s_%08X_%08zX.bin", phone->otp_imei, addr, size);

	struct flash_dump *dump = flash_dump_open(rawfile);
	if (!dump)
		return FLASH_ERROR;

	printf("\nreading raw: %s\n", rawfile);
	size_t total_blocks = flash_read_plan_count(size);
	printf("reading %zu blocks\n", total_blocks);

	for (size_t i = 0; i < total_blocks; i++) {
		uint32_t chunk_addr;
		size_t chunk;
		if (flash_read_plan_at(addr, size, i, &chunk_addr, &chunk) != 0) {
			flash_dump_abort(dump, "internal error in the read plan");
			return FLASH_ERROR;
		}

		uint8_t *buf = flash_read_raw(port, chunk_addr, chunk);
		if (!buf) {
			char motivo[64];
			snprintf(motivo, sizeof(motivo), "the phone stopped answering at 0x%08X", chunk_addr);
			flash_dump_abort(dump, motivo);
			return FLASH_ERROR;
		}

		int w = flash_dump_write(dump, buf, chunk);
		free(buf);
		if (w != 0) {
			flash_dump_abort(dump, "the dump could not be written to disk");
			return FLASH_ERROR;
		}

		printf("\rreading block: %zu/%zu (addr 0x%08zX size 0x%zX)", i + 1, total_blocks, (size_t)chunk_addr + chunk,
		       chunk);
		fflush(stdout);
	}

	printf("\n\n");
	char sha1_hex[41];
	if (flash_dump_commit(dump, size, sha1_hex) != FLASH_OK)
		return FLASH_ERROR;

	// --- optional babe conversion ---
	if (phone->save_as_babe) {
		char babefile[1024];
		snprintf(babefile, sizeof(babefile), "./backup/flashdump_%s_%08X_%08zX.ssw", phone->otp_imei, addr,
		         size);

		printf("converting to babe: %s\n", babefile);
		if (flash_cnv_raw_to_babe_file(rawfile, babefile, addr) != 0) {
			fprintf(stderr, "Error: failed to convert raw to babe\n");
			return FLASH_ERROR;
		}
		remove(rawfile);
	}

	return FLASH_OK;
}

static user_choice_t ask_user_choice(const char *prompt, const char *options)
{
	// Example: options = "[u]ninstall / [s]kip / [a]bort"
	char buf[16];
	printf("%s %s ? ", prompt, options);
	fflush(stdout);

	if (!fgets(buf, sizeof(buf), stdin))
		return CHOICE_ABORT;

	switch (buf[0]) {
	case 'u':
	case 'U':
		return CHOICE_UNINSTALL;
	case 's':
	case 'S':
		return CHOICE_SKIP;
	case 'a':
	case 'A':
		return CHOICE_ABORT;
	case 'c':
	case 'C':
	case 'y':
	case 'Y':
		return CHOICE_CONTINUE;
	default:
		return CHOICE_ABORT;
	}
}

static int cmp_u32(const void *a, const void *b)
{
	uint32_t va = *(const uint32_t *)a;
	uint32_t vb = *(const uint32_t *)b;

	if (va < vb)
		return -1;
	if (va > vb)
		return 1;
	return 0;
}

int flash_vkp(struct sp_port *port, const char *filename, vkp_patch_t *patch, int remove_flag, size_t flashblocksize)
{
	if (!patch || patch->patch.count == 0) {
		fprintf(stderr, "empty patch\n");
		return FLASH_VKP_ERR;
	}
	if (patch->errorline) {
		fprintf(stderr, "%s: error in line %d\n", filename, patch->errorline);
		return FLASH_VKP_ERR;
	}
	if (flashblocksize == 0) {
		fprintf(stderr, "unknown flash chip\n");
		return FLASH_VKP_ERR;
	}

	// 1. Collect blocks
	uint32_t blocks[MAX_BLOCKS];
	size_t nblocks = vkp_collect_unique_blocks(patch, flashblocksize, blocks, MAX_BLOCKS);
	if (nblocks == 0) {
		fprintf(stderr, "no blocks to patch\n");
		return FLASH_VKP_ERR;
	}
	qsort(blocks, nblocks, sizeof(uint32_t), cmp_u32);

	size_t chunks_per_flashblock = flashblocksize / BLOCK_SIZE;
	size_t realblocks = nblocks * chunks_per_flashblock;

	// 2. Allocate babe
	size_t babe_size = sizeof(struct babehdr_t) + realblocks /*hash*/ + realblocks * (8 + BLOCK_SIZE);
	uint8_t *babe = calloc(1, babe_size);
	if (!babe) {
		fprintf(stderr, "malloc failed\n");
		return FLASH_VKP_ERR;
	}

	struct babehdr_t *hdr = (struct babehdr_t *)babe;
	hdr->sig = 0xBEBA;
	hdr->ver = 3;
	hdr->payloadsize1 = (uint32_t)realblocks;

	size_t pos = sizeof(struct babehdr_t) + realblocks; // skip header + hash
	int block_index = 0;
	int total_blocks = (int)realblocks;

	for (size_t b = 0; b < nblocks; b++) {
		uint32_t base = blocks[b];
		for (size_t off = 0; off < flashblocksize; off += BLOCK_SIZE) {
			uint32_t chunk_addr = base + (uint32_t)off;

			((uint32_t *)(babe + pos))[0] = chunk_addr;
			((uint32_t *)(babe + pos))[1] = BLOCK_SIZE;
			pos += 8;

			uint8_t *dst = babe + pos;
			uint8_t *raw = flash_read_raw(port, chunk_addr, BLOCK_SIZE);
			if (!raw) {
				fprintf(stderr, "\nread failed at 0x%08X\n", chunk_addr);
				free(babe);
				return FLASH_VKP_ERR;
			}
			memcpy(dst, raw, BLOCK_SIZE);
			free(raw);

			pos += BLOCK_SIZE;

			block_index++;
			printf("\rReading block %d/%d (addr %08X size %08X) ", block_index, total_blocks, chunk_addr,
			       BLOCK_SIZE);
			fflush(stdout);
		}
	}
	printf("\n");

	// 3. Scan patches for mismatch
	int unmatched = 0, contrmatched = 0;
	for (size_t pi = 0; pi < patch->patch.count; pi++) {
		vkp_line_t *ln = &patch->patch.lines[pi];
		uint32_t addr = ln->addr;

		size_t p = sizeof(struct babehdr_t) + realblocks;
		while (1) {
			uint32_t curaddr = ((uint32_t *)(babe + p))[0];
			uint32_t cursize = ((uint32_t *)(babe + p))[1];
			if (addr < curaddr + cursize) {
				uint8_t actual = babe[p + 8 + (addr - curaddr)];
				if (actual != ln->data[remove_flag])
					unmatched++;
				if (actual == ln->data[remove_flag ^ 1])
					contrmatched++;
				break;
			}
			p += 8 + BLOCK_SIZE;
		}
	}

	// patch already installed
	if (unmatched && contrmatched == (int)patch->patch.count) {
		user_choice_t choice = ask_user_choice("patch installed, ", "[u]ninstall / [s]kip / [a]bort");

		if (choice == CHOICE_UNINSTALL) {
			remove_flag ^= 1;
			unmatched = 0;
		} else if (choice == CHOICE_SKIP) {
			free(babe);
			printf("skipping %s\n", filename);
			return FLASH_VKP_SKIP;
		} else // abort
		{
			free(babe);
			return FLASH_VKP_ERR;
		}
	}

	// patch mismatch
	if (unmatched) {
		char prompt[128];
		snprintf(prompt, sizeof(prompt), "%d/%zu mismatch", unmatched, patch->patch.count);

		user_choice_t choice = ask_user_choice(prompt, "[c]ontinue / [s]kip / [a]bort");

		if (choice == CHOICE_SKIP) {
			free(babe);
			return FLASH_VKP_SKIP; // skip this patch
		} else if (choice == CHOICE_ABORT) {
			free(babe);
			return FLASH_VKP_ERR;
		}
		// CHOICE_CONTINUE → proceed
	}

	printf("making a patched babe\n");

	// 4. Apply patches
	for (size_t pi = 0; pi < patch->patch.count; pi++) {
		vkp_line_t *ln = &patch->patch.lines[pi];
		uint32_t addr = ln->addr;

		size_t p = sizeof(struct babehdr_t) + realblocks;
		while (1) {
			uint32_t curaddr = ((uint32_t *)(babe + p))[0];
			uint32_t cursize = ((uint32_t *)(babe + p))[1];
			if (addr < curaddr + cursize) {
				babe[p + 8 + (addr - curaddr)] = ln->data[remove_flag ^ 1];
				break;
			}
			p += 8 + BLOCK_SIZE;
		}
	}

	// 5. Flash
	int rc = flash_babe(port, babe, pos, 1);
	free(babe);
	return rc == 0 ? FLASH_VKP_OK : FLASH_VKP_ERR;
}
