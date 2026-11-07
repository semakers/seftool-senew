#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <libserialport.h>

#include <core/common.h>
#include <core/serial.h>
#include <emp/common/cmd.h>

#include "gdfs.h"
#include "loader.h"

/* Copias acotadas de textos que manda el teléfono: nunca más de cap-1
 * bytes, siempre terminadas en '\0'. Los campos de gdfs_data_t miden
 * GDFS_TEXTO pero el teléfono puede mandar más. */
void gdfs_copy_text(char *dst, size_t cap, const uint8_t *src, size_t n)
{
	size_t j = 0;
	for (; j < n && j + 1 < cap && src[j]; j++)
		dst[j] = (char)src[j];
	dst[j] = '\0';
}

/* UTF-16LE a ASCII; lo que no es ASCII se vuelve '?'. (wcstombs esperaba
 * wchar_t de 4 bytes en Linux/macOS y devolvía un nombre vacío.) */
void gdfs_copy_ucs2(char *dst, size_t cap, const uint8_t *src, size_t n)
{
	size_t j = 0;
	for (size_t i = 0; i + 1 < n && j + 1 < cap; i += 2) {
		uint16_t c = src[i] | (src[i + 1] << 8);
		if (c == 0)
			break;
		/* el nombre acaba en nombres de archivo: nada de separadores de ruta */
		dst[j++] = (c < 0x20 || c >= 0x80) ? '?' : (c == '/' || c == '\\' || c == ':') ? '_' : (char)c;
	}
	dst[j] = '\0';
}

int pnx_get_rest_name(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs)
{
	// CXC article
	uint8_t resp[64];
	int len = loader_send_packet_pnx(port, 0x02, 0x0E, 0x15, resp, sizeof(resp));
	if (len <= 0)
		return -1;
	gdfs_copy_text(gdfs->cxc_article, sizeof(gdfs->cxc_article), resp, (size_t)len);

	parse_cxc_article_to_rest_name(phone, gdfs->cxc_article);

	return 0;
}

int gdfs_read_var(struct sp_port *port, struct gdfs_data_t *gdfs, int gd_index, uint8_t block, uint8_t lsb, uint8_t msb)
{
	uint8_t cmd_buf[10];
	int cmd_len = cmd_encode_read_gdfs(block, lsb, msb, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	// --- send command
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	// --- wait for response packet
	uint8_t resp[128];
	int rcv_len = serial_read(port, resp, sizeof(resp), 5 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	/* El teléfono puede mandar más bytes que el campo (el operador llega
	 * con 33 para 32): se copia acotado y siempre terminado en '\0'. */
	const uint8_t *src = repl.data + 1;
	size_t n = repl.length ? repl.length - 1 : 0;
	if (n > (size_t)rcv_len)
		n = (size_t)rcv_len;

	static const size_t desplaz[GD_COUNT] = {
		offsetof(struct gdfs_data_t, phone_name),   offsetof(struct gdfs_data_t, brand),
		offsetof(struct gdfs_data_t, cxc_article),  offsetof(struct gdfs_data_t, cxc_version),
		offsetof(struct gdfs_data_t, langpack),     offsetof(struct gdfs_data_t, cda_article),
		offsetof(struct gdfs_data_t, cda_revision), offsetof(struct gdfs_data_t, default_article),
		offsetof(struct gdfs_data_t, default_version),
	};
	if (gd_index < 0 || gd_index >= GD_COUNT)
		return 0;
	char *dst = (char *)gdfs + desplaz[gd_index];
	const size_t cap = GDFS_TEXTO;

	if (gd_index == GD_PHONE_NAME)
		gdfs_copy_ucs2(dst, cap, src, n);
	else
		gdfs_copy_text(dst, cap, src, n);

	return 0;
}

int gdfs_get_phonename(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs)
{
	uint8_t block = 0;
	uint8_t msb = 0;
	uint8_t lsb = 0;

	switch (phone->chip_id) {
	case DB2000:
		block = phone->is_z1010 ? 0x04 : 0x02;
		msb = 0x8F;
		lsb = 0x0C;
		break;
	case DB2010_1:
	case DB2010_2:
		block = 0x02;
		msb = 0x8F;
		lsb = 0x0C;
		break;
	case DB2020:
		block = 0x02;
		msb = 0xBB;
		lsb = 0x0D;
		break;
	default:
		return -1;
	}

	return gdfs_read_var(port, gdfs, GD_PHONE_NAME, block, msb, lsb);
}

int gdfs_get_brand(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs)
{
	uint8_t block = 0;
	uint8_t msb = 0;
	uint8_t lsb = 0;

	switch (phone->chip_id) {
	case DB2000:
		block = phone->is_z1010 ? 0x04 : 0x02;
		msb = 0xB9;
		lsb = 0x0C;
		break;
	case DB2010_1:
	case DB2010_2:
		block = 0x02;
		msb = 0xB9;
		lsb = 0x0C;
		break;
	case DB2020:
		block = 0x02;
		msb = 0xE5;
		lsb = 0x0D;
		break;

	default:
		return -1;
	}

	return gdfs_read_var(port, gdfs, GD_BRAND, block, msb, lsb);
}

int gdfs_parse_simlockdata(struct gdfs_data_t *gdfs, uint8_t *simlock)
{
	gdfs->locked = simlock[0x34] ? 1 : 0;

	// --- Decode MCC and MNC from BCD
	char mccmnc[16];
	decode_bcd(&simlock[0x34], 3, mccmnc, sizeof(mccmnc)); // 3 bytes BCD → 5–6 digits

	// MCC is always first 3 digits
	strncpy(gdfs->mcc, mccmnc, 3);
	gdfs->mcc[3] = '\0';

	// MNC is the rest (2 or 3 digits)
	strncpy(gdfs->mnc, mccmnc + 3, sizeof(gdfs->mnc) - 1);
	gdfs->mnc[sizeof(gdfs->mnc) - 1] = '\0';

	return 0;
}

int gdfs_get_simlock(struct sp_port *port, struct gdfs_data_t *gdfs)
{
	uint8_t block = 0x00;
	uint8_t msb = 0x00;
	uint8_t lsb = 0x06;

	uint8_t cmd_buf[64];
	int cmd_len = cmd_encode_read_gdfs(block, lsb, msb, cmd_buf);
	if (cmd_len < 0)
		return -1;

	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	uint8_t resp[512];
	int rcv_len = serial_read(port, resp, sizeof(resp), 5 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) < 0)
		return -1;

	gdfs_parse_simlockdata(gdfs, repl.data + 1);

	return 0;
}

/* Lee una unidad y la añade como línea «gdfswrite:» al archivo abierto `f`.
 * data[0] es el estado (0 = bien): antes no se miraba, y una respuesta de error
 * se grababa como una unidad VACÍA que write-script habría escrito de vuelta. */
static int gdfs_dump_var_to(struct sp_port *port, FILE *f, uint16_t block, uint8_t msb, uint8_t lsb)
{
	uint8_t cmd_buf[64];
	int cmd_len = cmd_encode_read_gdfs(block, lsb, msb, cmd_buf);
	if (cmd_len < 0)
		return -1;

	// --- send command
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	// --- wait for response packet
	uint8_t resp[1024];
	int rcv_len = serial_read(port, resp, sizeof(resp), 5 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) < 0)
		return -1;

	if (repl.length < 1 || repl.data[0] != 0x00) {
		fprintf(stderr, "Security unit %04X%02X%02X: the phone answered status %02X\n", block, msb, lsb,
		        repl.length >= 1 ? repl.data[0] : 0xFF);
		return -1;
	}

	// print header (block + msb + lsb)
	fprintf(f, "gdfswrite:%04X%02X%02X", block, msb, lsb);

	// append hex data
	for (int i = 1; i < repl.length; i++) {
		fprintf(f, "%02X", repl.data[i]);
	}

	fprintf(f, "\n");
	return 0;
}

/* Las cinco unidades de seguridad. Se escriben a <backup_name>.part y solo con
 * las cinco leídas se renombra al nombre final: antes un fallo a media lectura
 * dejaba un archivo con 2 de 5 líneas que identify daba por bueno para siempre. */
int gdfs_dump_sec_units(struct sp_port *port, struct phone_info *phone, const char *backup_name)
{
	(void)phone;
	char parcial[560];
	snprintf(parcial, sizeof(parcial), "%s.part", backup_name);
	FILE *f = fopen(parcial, "w");
	if (!f) {
		fprintf(stderr, "Cannot create %s\n", parcial);
		return -1;
	}

	static const struct {
		uint8_t msb, lsb;
	} unidades[] = {
	        {0x00, 0x06}, /* GD_COPS_Dynamic1Variable */
	        {0x00, 0x0E}, /* GD_COPS_Dynamic2Variable */
	        {0x00, 0x13}, /* GD_COPS_StaticVariable */
	        {0x00, 0x18}, /* GD_COPS_ProtectedCustomerSettings */
	        {0x00, 0xAA}, /* GD_Protected_PlatformSettings */
	};
	for (size_t i = 0; i < sizeof(unidades) / sizeof(unidades[0]); i++) {
		if (gdfs_dump_var_to(port, f, 0x00, unidades[i].msb, unidades[i].lsb) < 0) {
			fclose(f);
			fprintf(stderr, "SECURITY UNITS BACKUP INCOMPLETE (kept as %s); it will be retried on the next identify\n",
			        parcial);
			return -1;
		}
	}
	if (fclose(f) != 0 || rename(parcial, backup_name) != 0) {
		fprintf(stderr, "Cannot finish %s\n", backup_name);
		return -1;
	}

	printf("SECURITY UNITS BACKUP CREATED. %s\n", backup_name);

	return 0;
}

int gdfs_get_cxc_article(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs)
{
	uint8_t block = 0, msb = 0, lsb = 0;

	switch (phone->chip_id) {
	case DB2010_1:
	case DB2010_2:
		block = 0x02;
		msb = 0xE9;
		lsb = 0x0C;
		break;
	case DB2020:
		block = 0x02;
		msb = 0x15;
		lsb = 0x0E;
		break;
	default:
		return -1;
	}

	return gdfs_read_var(port, gdfs, GD_CXC_ARTICLE, block, msb, lsb);
}

int gdfs_get_cxc_version(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs)
{
	uint8_t block = 0;
	uint8_t msb = 0;
	uint8_t lsb = 0;

	switch (phone->chip_id) {
	case DB2010_1:
	case DB2010_2:
		block = 0x02;
		msb = 0xEA;
		lsb = 0x0C;
		break;
	case DB2020:
		block = 0x02;
		msb = 0x16;
		lsb = 0x0E;
		break;
	default:
		return -1;
	}

	return gdfs_read_var(port, gdfs, GD_CXC_VERSION, block, msb, lsb);
}

int gdfs_get_language(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs)
{
	uint8_t block = 0;
	uint8_t msb = 0;
	uint8_t lsb = 0;

	switch (phone->chip_id) {
	case DB2000:
		block = phone->is_z1010 ? 0x04 : 0x02;
		msb = 0xBB;
		lsb = 0x0C;
		break;
	case DB2010_1:
	case DB2010_2:
		block = 0x02;
		msb = 0xBB;
		lsb = 0x0C;
		break;
	case DB2020:
		block = 0x02;
		msb = 0xE7;
		lsb = 0x0D;
		break;
	default:
		return -1;
	}

	return gdfs_read_var(port, gdfs, GD_LANGPACK, block, msb, lsb);
}

int gdfs_get_cda_article(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs)
{
	uint8_t block = 0;
	uint8_t msb = 0;
	uint8_t lsb = 0;

	switch (phone->chip_id) {
	case DB2000:
		block = phone->is_z1010 ? 0x04 : 0x02;
		msb = 0xBC;
		lsb = 0x0C;
		break;
	case DB2010_1:
	case DB2010_2:
		block = 0x02;
		msb = 0xBC;
		lsb = 0x0C;
		break;
	case DB2020:
		block = 0x02;
		msb = 0xE8;
		lsb = 0x0D;
		break;
	default:
		return -1;
	}

	return gdfs_read_var(port, gdfs, GD_CDA_ARTICLE, block, msb, lsb);
}

int gdfs_get_cda_revision(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs)
{
	uint8_t block = 0;
	uint8_t msb = 0;
	uint8_t lsb = 0;

	switch (phone->chip_id) {
	case DB2000:
		block = phone->is_z1010 ? 0x04 : 0x02;
		msb = 0xBD;
		lsb = 0x0C;
		break;
	case DB2010_1:
	case DB2010_2:
		block = 0x02;
		msb = 0xBD;
		lsb = 0x0C;
		break;
	case DB2020:
		block = 0x02;
		msb = 0xE9;
		lsb = 0x0D;
		break;
	default:
		return -1;
	}

	return gdfs_read_var(port, gdfs, GD_CDA_REVISION, block, msb, lsb);
}

int gdfs_get_default_article(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs)
{
	uint8_t block = 0;
	uint8_t msb = 0;
	uint8_t lsb = 0;

	switch (phone->chip_id) {
	case DB2000:
		block = phone->is_z1010 ? 0x04 : 0x02;
		msb = 0xBE;
		lsb = 0x0C;
		break;
	case DB2010_1:
	case DB2010_2:
		block = 0x02;
		msb = 0xBE;
		lsb = 0x0C;
		break;
	case DB2020:
		block = 0x02;
		msb = 0xEA;
		lsb = 0x0D;
		break;
	default:
		return -1;
	}

	return gdfs_read_var(port, gdfs, GD_DEF_ARTICLE, block, msb, lsb);
}

int gdfs_get_default_version(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs)
{
	uint8_t block = 0;
	uint8_t msb = 0;
	uint8_t lsb = 0;

	switch (phone->chip_id) {
	case DB2000:
		block = phone->is_z1010 ? 0x04 : 0x02;
		msb = 0xBF;
		lsb = 0x0C;
		break;
	case DB2010_1:
	case DB2010_2:
		block = 0x02;
		msb = 0xBF;
		lsb = 0x0C;
		break;
	case DB2020:
		block = 0x02;
		msb = 0xEB;
		lsb = 0x0D;
		break;
	default:
		return -1;
	}

	return gdfs_read_var(port, gdfs, GD_DEF_VERSION, block, msb, lsb);
}

int gdfs_get_userlock(struct sp_port *port, struct gdfs_data_t *gdfs)
{
	uint8_t block = 0x00;
	uint8_t msb = 0x00;
	uint8_t lsb = 0x0E;

	uint8_t cmd_buf[64];
	int cmd_len = cmd_encode_read_gdfs(block, lsb, msb, cmd_buf);
	if (cmd_len < 0)
		return -1;

	// --- send command
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	// --- wait for response packet
	uint8_t resp[256];
	int rcv_len = serial_read(port, resp, sizeof(resp), 5 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	/* data[0x62] = nº de dígitos y data[0x63...] los dígitos (2 por byte):
	 * la respuesta tiene que llegar hasta ahí, y un código tiene como mucho
	 * 8 dígitos (antes usercode[9] y user_lock[16] se desbordaban con una
	 * respuesta corrupta). */
	if (repl.length < 0x63)
		return -1;
	uint8_t usercode_len = repl.data[0x62];
	if (usercode_len > 8)
		usercode_len = 8;
	if (usercode_len > 0 && repl.length < 0x63 + (usercode_len + 1) / 2)
		return -1;
	if (usercode_len == 0) {
		strncpy(gdfs->user_lock, "No usercode", sizeof(gdfs->user_lock) - 1);
		gdfs->user_lock[sizeof(gdfs->user_lock) - 1] = '\0';
		return 0;
	}

	char usercode[9] = {0}; // max 8 digits
	int pos = 0;
	for (int i = 0; i < usercode_len; i++) {
		int byte_index = 0x63 + (i / 2);
		uint8_t b = repl.data[byte_index];

		uint8_t digit;
		if ((i % 2) == 0)
			digit = b & 0x0F; // low nibble first
		else
			digit = (b >> 4) & 0x0F;

		usercode[pos++] = '0' + digit;
	}
	usercode[pos] = '\0';

	gdfs_copy_text(gdfs->user_lock, sizeof(gdfs->user_lock), (const uint8_t *)usercode, (size_t)pos);

	return 0;
}

int gdfs_unlock_usercode(struct sp_port *port)
{
	printf("Reset USERCODE... ");
	uint8_t cmd_buf[64];
	uint8_t resp[64];

	int cmd_len = cmd_encode_csloader_packet(CMD_CSLOADER, CMD_CS_RESETLOCK, NULL, 0, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	int rcv_len = serial_wait_packet(port, resp, sizeof(resp), 10 * TIMEOUT);
	if (rcv_len <= 0) {
		fprintf(stderr, "failed [no answer]\n");
		return -1;
	}

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.cmd == 0x1 && repl.data[1] == 0) {
		printf("done\n\nUSERCODE reset to '0000'\n\n");
		return 0;
	}

	printf("failed\n\n");
	return -1;
}

int gdfs_terminate_access(struct sp_port *port)
{
	uint8_t cmd_buf[8];

	// shutdown
	printf("Terminating GDFS server... ");
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
