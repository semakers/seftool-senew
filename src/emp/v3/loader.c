#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <libserialport.h>

#include <core/common.h>
#include <core/serial.h>
#include <core/payload.h>
#include <emp/common/cmd.h>
#include <utils/sha1.h>

#include "babe.h"
#include "connection.h"
#include "csloader.h"
#include "loader.h"
#include "gdfs.h"
#include "break.h"

static int loader_type = 0;

/* La cabecera BABE de un loader dice cuánto mandar (prólogo y cuerpo). Si
 * promete más de lo que tiene el archivo, antes se mandaba memoria del
 * montón al teléfono. Hay loaders reales a los que les faltan 1 y 49 bytes:
 * si falta poco se rellena con ceros (determinista); si falta mucho
 * (db2000_viola_csloader_r1e.bin declara 3 GB) se rechaza. Devuelve el
 * búfer (posiblemente realocado) o NULL, liberándolo, si no sirve. */
static uint8_t *loader_fit(uint8_t *buffer, size_t *fsize, const char *loader_name)
{
	size_t qh = sizeof(struct babehdr_t);
	if (*fsize < qh) {
		fprintf(stderr, "%s: shorter than a BABE header (%zu bytes)\n", loader_name, *fsize);
		free(buffer);
		return NULL;
	}
	const struct babehdr_t *h = (const struct babehdr_t *)buffer;
	size_t qa = h->prologuesize1, qd = h->payloadsize1;
	size_t need = qh + qa + qd;
	if (qa > 0x1000000 || qd > 0x1000000 || need < qh) {
		fprintf(stderr, "%s: header sizes are absurd (prologue %zu, body %zu)\n", loader_name, qa, qd);
		free(buffer);
		return NULL;
	}
	if (need <= *fsize)
		return buffer;
	if (need - *fsize > 4096) {
		fprintf(stderr, "%s: header promises %zu bytes but the file has %zu\n", loader_name, need, *fsize);
		free(buffer);
		return NULL;
	}
	uint8_t *nb = realloc(buffer, need);
	if (!nb) {
		free(buffer);
		return NULL;
	}
	memset(nb + *fsize, 0, need - *fsize);
	fprintf(stderr, "%s: file is %zu bytes shorter than its header says, padded with zeros\n", loader_name,
	        need - *fsize);
	*fsize = need;
	return nb;
}

int loader_get_hello(struct packetdata_t *packet)
{
	char loader_hello[256];
	if (packet->length >= sizeof(loader_hello))
		return -1;

	memcpy(loader_hello, packet->data, packet->length);
	loader_hello[packet->length] = '\0';

	printf("LDR: %s\n", loader_hello);

	loader_type = LDR_UNKNOWN;

	if (strstr(loader_hello, "CS_LOADER") || strstr(loader_hello, "CSLOADER")) {
		// printf("This is a CHIPSELECT loader\n");
		loader_type = LDR_CHIPSELECT;
	} else if (strstr(loader_hello, "FILESYSTEMLOADER") || strstr(loader_hello, "FILE_SYSTEM_LOADER") ||
	           strstr(loader_hello, "FS_LOADER")) {
		// printf("This is a FILESYSTEM loader\n");
		loader_type = LDR_CHIPSELECT;
	} else if (strstr(loader_hello, "PRODUCTION_ID") || strstr(loader_hello, "PRODUCTIONID") ||
	           strstr(loader_hello, "PRODUCTION_LOADER")) {
		// printf("This is a PRODUCTION_ID loader\n");
		loader_type = LDR_PRODUCT_ID;
	} else if (strstr(loader_hello, "CERTLOADER")) {
		// printf("This is a CERTIFICATE loader\n");
		loader_type = LDR_CERT;
	} else if (strstr(loader_hello, "FLASHLOADER")) {
		// printf("This is a FLASH loader\n");
		loader_type = LDR_FLASH;
	} else if (strstr(loader_hello, "MEM_PATCHER")) {
		// printf("This is a MEM_PATCHER loader\n");
		loader_type = LDR_FLASH;
	} else if (strstr(loader_hello, "patched")) {
		// printf("This is a patched loader\n");
		loader_type = LDR_FLASH;
	} else {
		// printf("%s\nThis is an unknown loader type\n", loader_hello);
		loader_type = LDR_UNKNOWN;
	}

	if (strstr(loader_hello, "SETOOL"))
		printf("Let's say thanks to the_laser =)\n");

	if (strstr(loader_hello, "den_po"))
		printf("Let's say thanks to den_po =)\n");

	return 0;
}

int loader_send_binary_cmd3e(struct sp_port *port, const char *fname)
{
	char loader_name[512];
	snprintf(loader_name, sizeof(loader_name), "./loader/%s", fname);

	size_t fsize;
	uint8_t *buffer = load_file(loader_name, &fsize);
	if (!buffer) {
		fprintf(stderr, "can't read %s\n", loader_name);
		return -1;
	}

	/* El paquete mide fsize + 5 y el campo de longitud es de 16 bits. Con un
	 * búfer fijo de 0x800, db2010_breakinR2E.bin (3652 bytes) lo desbordaba
	 * siempre. */
	if (fsize > 0xFFFF) {
		fprintf(stderr, "%s is too big for a cmd3e packet (%zu bytes)\n", loader_name, fsize);
		free(buffer);
		return -1;
	}
	uint8_t *cmd_buf = malloc(fsize + 8);
	if (!cmd_buf) {
		free(buffer);
		return -1;
	}
	int cmd_len = cmd_encode_binary_packet(0x3E, buffer, fsize, cmd_buf);
	free(buffer);
	if (cmd_len <= 0) {
		free(cmd_buf);
		return -1;
	}

	// --- send cmd3e command
	int enviado = serial_send_packetdata_ack(port, cmd_buf, cmd_len);
	free(cmd_buf);
	if (enviado < 0)
		return -1;

	// --- Read hello response from loader =)
	uint8_t hello_buf[128];
	int rcv_len = serial_wait_packet(port, hello_buf, sizeof(hello_buf), 3 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	if (hello_buf[1] == 0xFC && hello_buf[2] == 0xFF) // CMD3E DB2000 DB2010 CID29 from SETOOL
	{
		printf("Break CMD3E\n");
		return 0;
	}

	struct packetdata_t repl;
	if (cmd_decode_packet(hello_buf, rcv_len, &repl) != 0)
		return -1;

	loader_get_hello(&repl);

	return 0;
}

int loader_send_unsigned_bin(struct sp_port *port, const char *fname, uint32_t ram_addr)
{
	char loader_name[512];
	snprintf(loader_name, sizeof(loader_name), "./loader/%s", fname);

	size_t fsize;
	uint8_t *buffer = load_file(loader_name, &fsize);
	if (!buffer) {
		fprintf(stderr, "can't read %s\n", loader_name);
		return -1;
	}

	// --- Build ram_addr packet (little endian)
	uint8_t addr_packet[4];
	addr_packet[0] = (uint8_t)(ram_addr & 0xFF);
	addr_packet[1] = (uint8_t)((ram_addr >> 8) & 0xFF);
	addr_packet[2] = (uint8_t)((ram_addr >> 16) & 0xFF);
	addr_packet[3] = (uint8_t)((ram_addr >> 24) & 0xFF);

	if (serial_write(port, addr_packet, sizeof(addr_packet)) < 0)
		goto exit_error;

	// --- Build payload size packet (little endian)
	uint8_t size_packet[4];
	size_packet[0] = (uint8_t)(fsize & 0xFF);
	size_packet[1] = (uint8_t)((fsize >> 8) & 0xFF);
	size_packet[2] = (uint8_t)((fsize >> 16) & 0xFF);
	size_packet[3] = (uint8_t)((fsize >> 24) & 0xFF);

	if (serial_write(port, size_packet, sizeof(size_packet)) < 0)
		goto exit_error;

	// --- Send loader body
	if (serial_write_chunks(port, buffer, fsize, 0x4000) < 0)
		goto exit_error;

	// --- Read hello response from loader =)
	uint8_t hello_buf[128];
	int rcv_len = serial_wait_packet(port, hello_buf, sizeof(hello_buf), 3 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(hello_buf, rcv_len, &repl) != 0)
		return -1;

	loader_get_hello(&repl);

	free(buffer);
	return 0;

exit_error:
	free(buffer);
	return -1;
}

/* seftool-senew: identidad entre sesiones. Cada orden de seftool es una sesión nueva (el teléfono se apaga al
 * terminar) y entre una y otra la persona vuelve a enchufarlo: otro W580 recibiría la imagen y el GDFS del
 * respaldo (su IMEI, su simlock, su calibración). Con --imei S, en cuanto el primer loader está activo y el OTP
 * leído —antes de cargar el CS loader, el flashloader o un loader de exploit, y por tanto antes de escribir
 * nada— se compara. Si el aparato no dice su IMEI no se puede comprobar, y también se niega. */
int loader_check_expected_imei(struct phone_info *phone)
{
	if (phone->expect_imei[0] == '\0')
		return 0;
	if (phone->otp_imei[0] != '\0' && strncmp(phone->otp_imei, phone->expect_imei, sizeof(phone->otp_imei)) == 0)
		return 0;

	if (phone->otp_imei[0] == '\0')
		fprintf(stderr, "  El aparato no ha dicho su IMEI: no se puede comprobar que sea el esperado.\n");
	else
		fprintf(stderr, "  El aparato conectado dice IMEI «%s» y se esperaba «%s».\n", phone->otp_imei,
		        phone->expect_imei);
	fprintf(stderr, "imei-check: REFUSED: no es el teléfono esperado; no se manda nada más.\n");
	phone->imei_refused = 1;
	return -1;
}

/* «[QHTRY] Run executer first» solo cuando de verdad es lo que falta. Si qhtry_setool falló porque --imei se negó,
 * el executer no tiene nada que ver y la pista engañaría a quien la lea (SENUS la toma por «executer ausente» y
 * repite el ritual de abrir el MIDlet tres veces con el teléfono equivocado). */
void loader_hint_run_executer(const struct phone_info *phone)
{
	if (!phone->imei_refused)
		printf("[QHTRY] Run executer first\n");
}

// --- Loader activate
int loader_activate_payload(struct sp_port *port, struct phone_info *phone)
{
	if (loader_type == LDR_CHIPSELECT) {
		uint8_t cmd_buf[64];
		uint8_t resp[128];
		struct packetdata_t repl = {0};

		int cmd_len, rcv_len;

		// --- Activate CS loader
		printf("Activating CHIPSELECT loader... ");
		cmd_len = cmd_encode_csloader_packet(CMD_CSLOADER, 0x09, NULL, 0, cmd_buf);
		if (cmd_len <= 0)
			return -1;
		if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
			return -1;

		rcv_len = serial_wait_packet(port, resp, 8, 20 * TIMEOUT);
		if (rcv_len <= 0)
			return -1;

		if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
			return -1;

		if (repl.data[1] != 0x00) {
			fprintf(stderr, "failed activating loader\n");
			return -1;
		}
		printf("activated\n");

		if (phone->gdfs_server == 0)
			return 0;

		// --- Activate GDFS server
		printf("Activating GDFS server... ");
		cmd_len = cmd_encode_csloader_packet(CMD_CS_GDFS, CMD_GDFS_ACTIVATION, NULL, 0, cmd_buf);
		if (cmd_len <= 0)
			return -1;

		if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
			return -1;

		rcv_len = serial_wait_packet(port, resp, 8, 500 * TIMEOUT);
		if (rcv_len <= 0)
			return -1;

		if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
			return -1;

		if (repl.data[1] != 0x00) {
			fprintf(stderr, "failed\n");
			return -1;
		}
		printf("activated\n");

		// --- Test unlock command should print phone name if unlocked
		printf("Test GDFS Server... ");
		if (csloader_gdfs_get_phonename(port, phone) < 0) {
			printf("locked\n");
			return -1;
		}
		printf("unlocked\n");
		printf("%s ", phone->phone_name);

		// No cxc_article for DB2000 & DB2010(0x8000) =)
		if (phone->chip_id == DB2000 || phone->chip_id == DB2010_1) {
			printf("\n\n");
			return 0;
		}

		if (csloader_gdfs_get_cxc_article(port, phone) < 0)
			return -1;

		printf("%s\n\n", phone->cxc_article);
		strncpy(phone->anycid_target, phone->cxc_article, sizeof(phone->cxc_article));

		return 0;
	}

	// ------------------- Non-CS Loader -------------------

	// Get Flash data
	if (loader_get_flash_info(port, phone) != 0)
		return -1;

	// Get OTP data
	if (loader_get_otp_info(port, phone) != 0)
		return -1;

	return loader_check_expected_imei(phone);
}

int loader_send_qhtry_jdflasher(struct sp_port *port, struct phone_info *phone, const char *fname)
{
	char loader_name[512];
	snprintf(loader_name, sizeof(loader_name), "./loader/%s", fname);

	size_t fsize;
	uint8_t *buffer = load_file(loader_name, &fsize);
	if (!buffer) {
		fprintf(stderr, "can't read %s\n", loader_name);
		return -1;
	}

	buffer = loader_fit(buffer, &fsize, loader_name);
	if (!buffer)
		return -1;

	struct babehdr_t *babehdr = (struct babehdr_t *)buffer;

	size_t qh_size = sizeof(struct babehdr_t);
	size_t qa_size = babehdr->prologuesize1;

	uint8_t *qh00 = buffer;
	uint8_t *qa00 = buffer + qh_size;

	// --- Send QH00
	// printf("Send header ...\n");
	if (serial_write(port, (uint8_t *)"QH00", 4) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EsB", 5 * TIMEOUT, 1) < 0) {
		fprintf(stderr, "jdflasher: header not accepted (EsB timeout tras QH00) con %s\n", fname);
		goto exit_error;
	}
	if (serial_write(port, qh00, qh_size) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EhM", 5 * TIMEOUT, 1) < 0) {
		fprintf(stderr, "jdflasher: prologue not accepted (EhM timeout tras el header de %s)\n", fname);
		goto exit_error;
	}

	// --- Send QA00
	// printf("Send prologue ...\n");
	if (serial_write(port, (uint8_t *)"QA00", 4) < 0)
		goto exit_error;
	if (serial_write_chunks(port, qa00, qa_size, 0x400) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EaT", 5 * TIMEOUT, 1) < 0) {
		fprintf(stderr, "jdflasher: prologue execution failed (EaT timeout tras el cuerpo de %s)\n", fname);
		goto exit_error;
	}

	// break-rsa
	uint8_t byteleft;
	int rcv_len = serial_read(port, &byteleft, 1, 5 * TIMEOUT);
	if (rcv_len <= 0) {
		fprintf(stderr, "jdflasher: prologue execution failed (sin byteleft tras EaT)\n");
		goto exit_error;
	}

	printf("STARTING JDFLASHER BOOTLOADER...\n");
	if (serial_write(port, (uint8_t *)"R", 1) < 0)
		goto exit_error;

	sp_flush(port, SP_BUF_BOTH);

	uint8_t hello_buf[64];
	rcv_len = serial_wait_packet(port, hello_buf, sizeof(hello_buf), 20 * TIMEOUT);
	if (rcv_len <= 0) {
		fprintf(stderr, "jdflasher: bad payload (no hubo hello tras R)\n");
		goto exit_error;
	}

	struct packetdata_t repl;
	if (cmd_decode_packet(hello_buf, rcv_len, &repl) != 0)
		goto exit_error;

	loader_get_hello(&repl);

	free(buffer);

	// Activate loader
	if (loader_activate_payload(port, phone) != 0)
		return -1;

	phone->qhldr_sent = 1;

	return 0;

exit_error:
	free(buffer);
	return -1;
}

/* Variante de loader_send_qhtry_jdflasher para el prologue DB2020 CID52 (jdflasher.dll
 * <qhtry>2020_prologue</qhtry><run/>): hace QH00+QA00 y envía "R", pero NO espera hello
 * ni llama a loader_activate_payload. El prologue no es un flashloader; es un payload que
 * modifica algo en el bootrom y devuelve el control. El siguiente binario del script
 * (R2A005) sí es un flashloader y es él quien trae el hello y pide activate.
 *
 * Lo demuestra el fallo observado contra el W580 roto 2026-10-18 09:29:
 *   STARTING JDFLASHER BOOTLOADER...
 *   jdflasher: bad payload (no hubo hello tras R)
 * Todo el handshake previo (EsB/EhM/EaT/byteleft/R) pasa; solo falla el hello después,
 * que el prologue no manda. */
int loader_send_prologue_jdflasher(struct sp_port *port, struct phone_info *phone, const char *fname)
{
	(void)phone; /* no se toca; el prologue no deja estado leíble aquí */
	char loader_name[512];
	snprintf(loader_name, sizeof(loader_name), "./loader/%s", fname);

	size_t fsize;
	uint8_t *buffer = load_file(loader_name, &fsize);
	if (!buffer) {
		fprintf(stderr, "can't read %s\n", loader_name);
		return -1;
	}

	/* Verificación en runtime del SHA-1: la cadena de confianza del exploit se apoya en que
	 * loader/2020_prologue sea el oficial de den_po (bajado de web.archive.org del snapshot
	 * de 2017 de justdanpo.ru). Si alguien lo cambia por otro .bin, nos negamos antes de
	 * enviar nada al teléfono. Centralizado aquí para que TODAS las rutas que llaman al
	 * prologue (bflash.c, action_read_flash y las que vengan) estén cubiertas sin duplicar. */
	{
		SHA1_CTX _sha;
		unsigned char _digest[SHA1_BLOCK_SIZE];
		sha1_init(&_sha);
		sha1_update(&_sha, buffer, fsize);
		sha1_final(&_sha, _digest);
		char got_hex[SHA1_BLOCK_SIZE * 2 + 1];
		for (int _i = 0; _i < SHA1_BLOCK_SIZE; _i++)
			snprintf(got_hex + _i * 2, 3, "%02x", _digest[_i]);
		if (strcmp(got_hex, DB2020_PROLOGUE_SHA1_HEX) != 0) {
			fprintf(stderr,
			        "prologue jdflasher: REFUSED: SHA-1 de %s no es el esperado.\n"
			        "  Esperado: %s\n"
			        "  Leído:    %s\n"
			        "  No se manda nada al teléfono. Reemplaza el fichero por el oficial\n"
			        "  de jdflasher.dll de den_po (docs/CID52_JDFLASHER.md).\n",
			        loader_name, DB2020_PROLOGUE_SHA1_HEX, got_hex);
			free(buffer);
			return -1;
		}
	}

	buffer = loader_fit(buffer, &fsize, loader_name);
	if (!buffer)
		return -1;

	struct babehdr_t *babehdr = (struct babehdr_t *)buffer;
	size_t qh_size = sizeof(struct babehdr_t);
	size_t qa_size = babehdr->prologuesize1;
	uint8_t *qh00 = buffer;
	uint8_t *qa00 = buffer + qh_size;

	if (serial_write(port, (uint8_t *)"QH00", 4) < 0) goto exit_error;
	if (serial_wait_answer(port, "EsB", 5 * TIMEOUT, 1) < 0) {
		fprintf(stderr, "prologue jdflasher: header not accepted (EsB timeout tras QH00) con %s\n", fname);
		goto exit_error;
	}
	if (serial_write(port, qh00, qh_size) < 0) goto exit_error;
	if (serial_wait_answer(port, "EhM", 5 * TIMEOUT, 1) < 0) {
		fprintf(stderr, "prologue jdflasher: prologue not accepted (EhM timeout tras el header)\n");
		goto exit_error;
	}

	if (serial_write(port, (uint8_t *)"QA00", 4) < 0) goto exit_error;
	if (serial_write_chunks(port, qa00, qa_size, 0x400) < 0) goto exit_error;
	if (serial_wait_answer(port, "EaT", 5 * TIMEOUT, 1) < 0) {
		fprintf(stderr, "prologue jdflasher: prologue execution failed (EaT timeout tras el cuerpo)\n");
		goto exit_error;
	}

	uint8_t byteleft;
	int rcv_len = serial_read(port, &byteleft, 1, 5 * TIMEOUT);
	if (rcv_len <= 0) {
		fprintf(stderr, "prologue jdflasher: sin byteleft tras EaT\n");
		goto exit_error;
	}

	printf("STARTING JDFLASHER PROLOGUE (DB2020 CID52)...\n");
	if (serial_write(port, (uint8_t *)"R", 1) < 0) goto exit_error;

	sp_flush(port, SP_BUF_BOTH);
	/* El prologue ejecuta y reinicia el aparato DE VERDAD (medido 2026-10-18: el
	 * siguiente serial_write caía con -2 bytes incluso tras una reconexión manual
	 * completa — el aparato había arrancado el sistema operativo, no solo hecho un
	 * hipo de bus). Esta función solo manda el prologue; quien llama (bflash.c,
	 * action_read_flash) es quien hace `connection_restart()` para esperar a que
	 * el aparato vuelva y rehacer el protocolo de arranque — no hay nada que
	 * esperar aquí dentro. Esta pausa corta es solo para no lanzar el primer
	 * sondeo de USB en el mismo instante en que el aparato empieza a desconectarse. */
	sleep_ms(300);

	free(buffer);
	return 0;

exit_error:
	free(buffer);
	return -1;
}

/* Carga el flashloader DB2020 R2A005 por el formato CRUDO tras el prologue CID52
 * (<qhtry>2020_prologue</qhtry><run/><ldr>2020_f_R2A005</ldr>). El plugin oficial
 * jdflasher.dll de den_po, al despachar su `<ldr>` del XML, consulta un flag de sesión
 * ("already loaded", offset 0x1016c de la estructura de sesión) y bifurca:
 *   - flag == 0 → qhldr (QH00/QA00/QD00 ASCII + handshake EsB/EhM/EaT/EbS/EdQ)
 *   - flag != 0 → cmd3c (empaquetado con header 0x89 + checksum)
 * Solo cmd3c marca el flag al terminar; qhldr/qhtry NO lo marcan. Tras <qhtry>+<run/>
 * el flag está a 0, así que el R2A005 va por qhldr crudo. Nuestro loader_send_binary
 * mandaba el R2A005 por cmd 0x3C — rama equivocada: el bootrom rechazaba el header
 * 0x89 ("-2 of 902 bytes" medido 2026-10-18 10:08-10:10 contra el W580 roto).
 * Comprobación cruzada: scripts/qa2020.xml usa <ldr> para CID52 (lo deja decidir al
 * flag → crudo) y <cmd3c> explícito para CID53 (empaquetado) — el autor del plugin
 * marcó la asimetría CID52↔CID53 en el XML.
 *
 * Esta función es la MISMA mecánica que loader_send_qhtry_setool pero (a) con
 * verificación de SHA-1 del R2A005, (b) sin las ramas DB2010 break_rsa/anycid que no
 * aplican al CID52 jdflasher, y (c) con texto propio para que lo que imprime no
 * parezca el flujo setool.
 *
 * Precondiciones: phone->jdflasher==1, phone->expect_cid==52. Llama a quien llama
 * no supone chip_id leído (la rama jdflasher no corre P3M antes); aquí se trata
 * DB2020 fijo. Al terminar deja el flashloader activo (loader_activate_payload) y
 * marca phone->qhldr_sent como hace loader_send_qhtry_setool. */
/* Da una ventana corta al bootrom crudo CID52 para procesar lo que acaba de
 * recibir. Intenta leer hasta 3 bytes con timeout muy breve; si viene algo lo
 * descarta (el DLL Windows chequearía 'F' aquí, pero el bootrom crudo no
 * suele mandar nada en estos puntos); si STALL'ea, hace clear_halt y sigue.
 * Nunca falla: no es un wait_answer, es un «respiro». */
static void loader_respira_jdflasher(struct sp_port *port)
{
	uint8_t descartar[3];
	int rcv_len = serial_wait_packet(port, descartar, sizeof(descartar), 2 * TIMEOUT);
	(void)rcv_len;
	/* Si el read vino con STALL, serial_wait_packet ya hizo clear_halt
	 * dentro de sp_usb.c y retornó SP_ERR_FAIL — pero el pipe ya está
	 * limpio. Da igual: seguimos. */
}

int loader_send_qhldr_jdflasher(struct sp_port *port, struct phone_info *phone, const char *fname)
{
	char loader_name[512];
	snprintf(loader_name, sizeof(loader_name), "./loader/%s", fname);

	size_t fsize;
	uint8_t *buffer = load_file(loader_name, &fsize);
	if (!buffer) {
		fprintf(stderr, "can't read %s\n", loader_name);
		return -1;
	}

	/* Verificación del SHA-1 ANTES de nada: la cadena de confianza de la rama jdflasher
	 * requiere que el R2A005 sea el oficial del plugin de den_po. Si alguien cambia el
	 * fichero por otro .bin, nos negamos antes de enviar un solo byte al teléfono (misma
	 * política que loader_send_prologue_jdflasher con DB2020_PROLOGUE_SHA1_HEX). */
	{
		SHA1_CTX _sha;
		unsigned char _digest[SHA1_BLOCK_SIZE];
		sha1_init(&_sha);
		sha1_update(&_sha, buffer, fsize);
		sha1_final(&_sha, _digest);
		char got_hex[SHA1_BLOCK_SIZE * 2 + 1];
		for (int _i = 0; _i < SHA1_BLOCK_SIZE; _i++)
			snprintf(got_hex + _i * 2, 3, "%02x", _digest[_i]);
		if (strcmp(got_hex, DB2020_FLASHLOADER_R2A005_SHA1_HEX) != 0) {
			fprintf(stderr,
			        "qhldr jdflasher: REFUSED: SHA-1 de %s no es el esperado.\n"
			        "  Esperado: %s\n"
			        "  Leído:    %s\n"
			        "  No se manda nada al teléfono. Reemplaza el fichero por el oficial\n"
			        "  (jdflasher.dll de den_po, bin/2020_f_R2A005).\n",
			        loader_name, DB2020_FLASHLOADER_R2A005_SHA1_HEX, got_hex);
			free(buffer);
			return -1;
		}
	}

	buffer = loader_fit(buffer, &fsize, loader_name);
	if (!buffer)
		return -1;

	struct babehdr_t *babehdr = (struct babehdr_t *)buffer;
	size_t qh_size = sizeof(struct babehdr_t);
	size_t qa_size = babehdr->prologuesize1;
	size_t qd_size = babehdr->payloadsize1;

	uint8_t *qh00 = buffer;
	uint8_t *qa00 = buffer + qh_size;
	uint8_t *qd00 = buffer + qh_size + qa_size;

	/* --- QH00 + header + QA00 + prologue_data + QD00 + payload_data + 'R'
	 *
	 * TRAZA USB 2026-10-19 contra W580 roto CID52 (SENEW_USB_TRAZA=1), dos
	 * iteraciones:
	 *   it 1: tras header, STALL del endpoint IN (bootrom corta la lectura).
	 *   it 2 (sin reads intermedios): tras QA00 + primer chunk 1024 B, STALL
	 *          del endpoint OUT (bootrom «no quiero más por ahora»).
	 *
	 * Interpretación: el bootrom crudo tras el prologue CID52 necesita UNA
	 * PAUSA entre cada fase para procesar lo recibido. En el prologue mismo
	 * esa pausa la aportaba el `wait_answer "EhM"` (que leía 3 bytes del
	 * bootrom virgen). Aquí, sin bytes que recibir, hace falta meter una
	 * pausa explícita entre header↔QA00, QA00+data↔QD00, QD00+data↔'R'.
	 *
	 * `respira()` abajo intenta leer hasta 3 bytes con timeout corto; si el
	 * endpoint STALL'ea, hace clear_halt y sigue sin fallar. Así damos al
	 * bootrom su ventana sin romper si no manda nada. */
	if (serial_write(port, (uint8_t *)"QH00", 4) < 0) goto exit_error;
	if (serial_wait_answer(port, "EsB", 100 * TIMEOUT, 1) < 0) {
		fprintf(stderr, "qhldr jdflasher: header not accepted (EsB timeout tras QH00) con %s\n", fname);
		goto exit_error;
	}

	if (serial_write(port, qh00, qh_size) < 0) goto exit_error;
	loader_respira_jdflasher(port);   /* ventana al bootrom tras el header */

	if (serial_write(port, (uint8_t *)"QA00", 4) < 0) goto exit_error;
	if (serial_write_chunks(port, qa00, qa_size, 0x400) < 0) goto exit_error;
	loader_respira_jdflasher(port);   /* ventana tras QA00+prologue_data */

	if (serial_write(port, (uint8_t *)"QD00", 4) < 0) goto exit_error;
	if (serial_write_chunks(port, qd00, qd_size, 0x400) < 0) goto exit_error;
	loader_respira_jdflasher(port);   /* ventana tras QD00+payload_data */

	free(buffer);
	buffer = NULL;

	printf("STARTING JDFLASHER R2A005 (qhldr crudo, sin reads intermedios)...\n");
	if (serial_write(port, (uint8_t *)"R", 1) < 0)
		return -1;
	/* El DLL hace writebyte('R') y llama directo a activate_payload — sin
	 * byteleft previo ni three3E tras. Esperamos el hello del R2A005 (ya
	 * arrancado) y lo decodificamos; si no llega, se lo decimos al humano. */

	uint8_t hello_buf[64];
	int rcv_len = serial_wait_packet(port, hello_buf, sizeof(hello_buf), 100 * TIMEOUT);
	if (rcv_len <= 0) {
		fprintf(stderr, "qhldr jdflasher: no llegó el hello del R2A005 tras 'R'\n");
		return -1;
	}

	struct packetdata_t repl;
	if (cmd_decode_packet(hello_buf, rcv_len, &repl) != 0) {
		fprintf(stderr, "qhldr jdflasher: hello del R2A005 corrupto\n");
		return -1;
	}

	loader_get_hello(&repl);

	if (loader_activate_payload(port, phone) != 0)
		return -1;

	phone->qhldr_sent = 1;

	return 0;

exit_error:
	if (buffer)
		free(buffer);
	return -1;
}

int loader_send_qhtry_setool(struct sp_port *port, struct phone_info *phone, const char *fname)
{
	char loader_name[512];
	snprintf(loader_name, sizeof(loader_name), "./loader/%s", fname);

	size_t fsize;
	uint8_t *buffer = load_file(loader_name, &fsize);
	if (!buffer) {
		fprintf(stderr, "can't read %s\n", loader_name);
		return -1;
	}

	buffer = loader_fit(buffer, &fsize, loader_name);
	if (!buffer)
		return -1;

	struct babehdr_t *babehdr = (struct babehdr_t *)buffer;

	size_t qh_size = sizeof(struct babehdr_t);
	size_t qa_size = babehdr->prologuesize1;
	size_t qd_size = babehdr->payloadsize1;

	uint8_t *qh00 = buffer;
	uint8_t *qa00 = buffer + qh_size;
	uint8_t *qd00 = buffer + qh_size + qa_size;

	// --- Send QH00
	// printf("Send header ...\n");
	if (serial_write(port, (uint8_t *)"QH00", 4) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EsB", 5 * TIMEOUT, 1) < 0)
		goto exit_error;
	if (serial_write(port, qh00, qh_size) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EhM", 5 * TIMEOUT, 1) < 0)
		goto exit_error;

	// --- Send QA00
	// printf("Send prologue ...\n");
	if (serial_write(port, (uint8_t *)"QA00", 4) < 0)
		goto exit_error;
	if (serial_write_chunks(port, qa00, qa_size, 0x400) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EaT", 5 * TIMEOUT, 1) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EbS", 5 * TIMEOUT, 1) < 0)
		goto exit_error;

	// --- Send QD00
	// printf("Send body ...\n");
	if (serial_write(port, (uint8_t *)"QD00", 4) < 0)
		goto exit_error;
	if (serial_write_chunks(port, qd00, qd_size, 0x400) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EdQ", 5 * TIMEOUT, 1) < 0)
		goto exit_error;

	free(buffer);

	// break-rsa or anycid exploit
	uint8_t byteleft;
	int rcv_len = serial_read(port, &byteleft, 1, 3 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	printf("STARTING SETOOL BOOTLOADER...\n");
	if (serial_write(port, (uint8_t *)"R", 1) < 0)
		return -1;

	if (phone->chip_id == DB2010_2) {
		if (phone->break_rsa == 1) {
			uint8_t resp;
			int rcv_len = serial_wait_packet(port, &resp, 1, 10 * TIMEOUT);
			if (rcv_len <= 0)
				return -1;
		} else if (phone->anycid == 1) {
			uint8_t two3E[2];
			int rcv_len = serial_wait_packet(port, two3E, sizeof(two3E), 10 * TIMEOUT);
			if (rcv_len <= 0)
				return -1;

			if (serial_write(port, (uint8_t *)"R", 1) < 0)
				return -1;
		}
	}

	if (phone->chip_id == DB2020) {
		uint8_t three3E[3];
		int rcv_len = serial_wait_packet(port, three3E, sizeof(three3E), 10 * TIMEOUT);
		if (rcv_len <= 0)
			return -1;
	}

	uint8_t hello_buf[64];
	rcv_len = serial_wait_packet(port, hello_buf, sizeof(hello_buf), 10 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(hello_buf, rcv_len, &repl) != 0)
		return -1;

	loader_get_hello(&repl);

	// Activate loader
	if (loader_activate_payload(port, phone) != 0)
		return -1;

	phone->qhldr_sent = 1;

	return 0;

exit_error:
	free(buffer);
	return -1;
}

int loader_send_qhldr_noact(struct sp_port *port, const char *fname)
{
	char loader_name[512];
	snprintf(loader_name, sizeof(loader_name), "./loader/%s", fname);

	size_t fsize;
	uint8_t *buffer = load_file(loader_name, &fsize);
	if (!buffer) {
		fprintf(stderr, "can't read %s\n", loader_name);
		return -1;
	}

	buffer = loader_fit(buffer, &fsize, loader_name);
	if (!buffer)
		return -1;

	struct babehdr_t *babehdr = (struct babehdr_t *)buffer;

	size_t qh_size = sizeof(struct babehdr_t);
	size_t qa_size = babehdr->prologuesize1;
	size_t qd_size = babehdr->payloadsize1;

	uint8_t *qh00 = buffer;
	uint8_t *qa00 = buffer + qh_size;
	uint8_t *qd00 = buffer + qh_size + qa_size;

	// --- Send QH00
	// printf("Send header ...\n");
	if (serial_write(port, (uint8_t *)"QH00", 4) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EsB", 5 * TIMEOUT, 0) < 0)
		goto exit_error;
	if (serial_write(port, qh00, qh_size) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EhM", 5 * TIMEOUT, 0) < 0)
		goto exit_error;

	// --- Send QA00
	// printf("Send prologue ...\n");
	if (serial_write(port, (uint8_t *)"QA00", 4) < 0)
		goto exit_error;
	if (serial_write_chunks(port, qa00, qa_size, 0x400) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EaT", 5 * TIMEOUT, 0) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EbS", 5 * TIMEOUT, 0) < 0)
		goto exit_error;

	// --- Send QD00
	// printf("Send body ...\n");
	if (serial_write(port, (uint8_t *)"QD00", 4) < 0)
		goto exit_error;
	if (serial_write_chunks(port, qd00, qd_size, 0x400) < 0)
		goto exit_error;
	if (serial_wait_answer(port, "EdQ", 5 * TIMEOUT, 0) < 0)
		goto exit_error;

	// --- Read hello response from loader =)
	uint8_t hello_buf[64];
	int rcv_len = serial_wait_packet(port, hello_buf, sizeof(hello_buf), 5 * TIMEOUT);
	if (rcv_len <= 0)
		goto exit_error;

	struct packetdata_t repl;
	if (cmd_decode_packet(hello_buf, rcv_len, &repl) != 0)
		goto exit_error;

	loader_get_hello(&repl);

	free(buffer);
	return 0;

exit_error:
	free(buffer);
	return -1;
}

int loader_send_qhldr(struct sp_port *port, struct phone_info *phone, const char *loader_name)
{
	if (phone->qhldr_sent == 1)
		return 0;

	if (loader_send_qhldr_noact(port, loader_name) != 0)
		return -1;

	if (loader_activate_payload(port, phone) != 0)
		return -1;

	phone->qhldr_sent = 1;

	printf("FLASH ID: 0x%x (%s)\n", phone->flash_id, get_flash_vendor(phone->flash_id));

	printf("OTP: LOCKED:%d CID:%d PAF:%d IMEI:%s\n", phone->otp_locked, phone->otp_cid, phone->otp_paf,
	       phone->otp_imei);

	if (phone->chip_id == DB2020 && phone->anycid == 0) {
		if (loader_get_erom_info(port, phone) != 0)
			return -1;

		printf("ACTIVE CID:%02d COLOR:%s\n", phone->erom_cid, color_get_name(phone->erom_color));
	}

	return 0;
}

int loader_send_encoded_cmd_and_data(struct sp_port *port, uint8_t cmd, uint8_t *data, size_t total_bytes)
{
	/* Cabecera (5) + hasta 0x7FF de datos + checksum (1) = 0x805: con
	 * 0x800 el último paquete lleno se salía de la pila. */
	uint8_t buf[5 + 0x7FF + 1];
	size_t sent_bytes = 0;
	int do_once = 1;

	serial_send_ack(port);

	while (sent_bytes < total_bytes || do_once) {
		int num = 0;
		uint8_t checksum = 0;
		do_once = 0;

		buf[num++] = SERIAL_HDR89; // hdr
		buf[num++] = cmd; // command

		int bytes_to_send = total_bytes - sent_bytes;
		int max_bytes = 0x7FF;
		if (bytes_to_send > max_bytes)
			bytes_to_send = max_bytes;

		// length = payload + continuebit
		buf[num++] = (bytes_to_send + 1) & 0xFF;
		buf[num++] = ((bytes_to_send + 1) >> 8) & 0xFF;

		// continuebit: 01 = more data coming, 00 = no more data coming
		uint8_t continuebit = (sent_bytes + bytes_to_send < total_bytes) ? 0x01 : 0x00;
		buf[num++] = continuebit;

		memcpy(&buf[num], &data[sent_bytes], bytes_to_send);
		num += bytes_to_send;

		// checksum
		for (int i = 0; i < num; i++)
			checksum ^= buf[i];
		checksum = (checksum + 7) & 0xFF;
		buf[num++] = checksum;

		// send data
		if (serial_write_chunks(port, buf, num, 0x400) < 0)
			return -1;

		// --- Read response
		if (continuebit) {
			if (serial_wait_ack(port, 10 * TIMEOUT) != 0)
				return -1;
		}

		sent_bytes += bytes_to_send;
	}

	return 0;
}

int loader_send_binary_noact(struct sp_port *port, const char *fname)
{
	char loader_name[512];
	snprintf(loader_name, sizeof(loader_name), "./loader/%s", fname);

	size_t fsize;
	uint8_t *buffer = load_file(loader_name, &fsize);
	if (!buffer) {
		fprintf(stderr, "can't read %s\n", loader_name);
		return -1;
	}

	buffer = loader_fit(buffer, &fsize, loader_name);
	if (!buffer)
		return -1;

	struct babehdr_t *babehdr = (struct babehdr_t *)buffer;

	size_t qh_size = sizeof(struct babehdr_t);
	size_t qa_size = babehdr->prologuesize1;
	size_t qd_size = babehdr->payloadsize1;

	uint8_t *qh00 = buffer;
	uint8_t *qa00 = buffer + qh_size;
	uint8_t *qd00 = buffer + qh_size + qa_size;

	uint8_t resp[7] = {0};
	int rcv_len;

	struct packetdata_t repl;

	// printf("Send header ...\n");
	if (loader_send_encoded_cmd_and_data(port, 0x3C, qh00, qh_size) < 0)
		goto exit_error;
	rcv_len = serial_wait_packet(port, resp, sizeof(resp), 3 * TIMEOUT);
	if (rcv_len <= 0)
		goto exit_error;

	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.cmd != 0x3D) {
		fprintf(stderr, "Bad answer %02X\n", repl.cmd);
		return -1;
	}

	// printf("Send prologue ...\n");
	if (loader_send_encoded_cmd_and_data(port, 0x3C, qa00, qa_size) < 0)
		goto exit_error;

	rcv_len = serial_wait_packet(port, resp, sizeof(resp), 3 * TIMEOUT);
	if (rcv_len <= 0)
		goto exit_error;

	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.cmd != 0x3D) {
		fprintf(stderr, "Bad answer %02X\n", repl.cmd);
		return -1;
	}

	// printf("Send body ...\n");
	if (loader_send_encoded_cmd_and_data(port, 0x3C, qd00, qd_size) < 0)
		goto exit_error;
	rcv_len = serial_wait_packet(port, resp, sizeof(resp),
	                             20 * TIMEOUT); // PNX5230 need extra longer ;(
	if (rcv_len <= 0)
		goto exit_error;

	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.cmd != 0x3D) {
		fprintf(stderr, "Bad answer %02X\n", repl.cmd);
		return -1;
	}

	// Send ACK to start binary loader
	serial_send_ack(port);

	// --- Read hello response from loader
	uint8_t hello_buf[128];
	rcv_len = serial_read(port, hello_buf, sizeof(hello_buf), 10 * TIMEOUT);
	if (rcv_len <= 0)
		goto exit_error;

	if (cmd_decode_packet(hello_buf, rcv_len, &repl) != 0)
		return -1;

	loader_get_hello(&repl);

	free(buffer);
	return 0;

exit_error:
	free(buffer);
	return -1;
}

int loader_send_binary(struct sp_port *port, struct phone_info *phone, const char *loader_name)
{
	if (loader_send_binary_noact(port, loader_name) != 0)
		return -1;

	if (loader_activate_payload(port, phone) != 0)
		return -1;

	return 0;
}

int loader_get_erom_info(struct sp_port *port, struct phone_info *phone)
{
	uint8_t cmd_buf[32];
	int cmd_len = cmd_encode_binary_packet(CMD_READ_EROM_INFO, NULL, 0, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	// --- send command
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	uint8_t resp[0x13];
	int rcv_len = serial_read(port, resp, sizeof(resp), 5 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.data[1] & 1)
		phone->erom_color = BLUE;
	else if (repl.data[1] & 2)
		phone->erom_color = BROWN;
	else if (repl.data[1] & 4)
		phone->erom_color = RED;
	else if (repl.data[1] & 8)
		phone->erom_color = BLACK;
	else {
		phone->erom_color = BLACK;
		// return -1;
	}

	phone->erom_cid = repl.data[9];

	return 0;
}

int loader_get_otp_info(struct sp_port *port, struct phone_info *phone)
{
	uint8_t cmd_buf[32];
	int cmd_len = cmd_encode_binary_packet(CMD_READ_OTP_IMEI, NULL, 0, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	// --- send command
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	uint8_t resp[0x20];
	int rcv_len = serial_read(port, resp, sizeof(resp), 5 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.length < 5 + 14) {
		fprintf(stderr, "OTP reply too short (%d)\n", repl.length);
		return -1;
	}

	phone->otp_status = repl.data[0];
	phone->otp_locked = repl.data[1];
	phone->otp_cid = (repl.data[3] << 8) | repl.data[2];
	phone->otp_paf = repl.data[4];
	memcpy(phone->otp_imei, repl.data + 5, 14);
	phone->otp_imei[14] = '\0';
	/* El IMEI entra en nombres de archivo (secunits_, GDFS_, flashdump_):
	 * lo que no sea alfanumérico no debe poder traer un '/'. */
	for (int i = 0; i < 14; i++) {
		char c = phone->otp_imei[i];
		if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
			phone->otp_imei[i] = '_';
	}

	if (strncmp(phone->otp_imei, "353456", 6) == 0 || strncmp(phone->otp_imei, "353457", 6) == 0) {
		phone->is_z1010 = 1;
	}

	return 0;
}

int loader_get_flash_info(struct sp_port *port, struct phone_info *phone)
{
	uint8_t cmd_buf[32];
	int cmd_len = cmd_encode_binary_packet(CMD_GET_FLASHID, NULL, 0, cmd_buf);
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

	if (repl.cmd != 0x0A && repl.length != 2)
		return -1;

	phone->flash_id = (repl.data[0] << 8) | repl.data[1];

	switch (phone->flash_id) {
	case 0x8964:
		phone->flashblocksize = 0x10000;
		break;
	case 0x200D:
	case 0x890D:
		phone->flashblocksize = 0x20000;
		break;
	case 0x2019:
	case 0x897E:
		phone->flashblocksize = 0x40000;
		break;
	default:
		printf("unknown flash chip 0x%X\n", phone->flash_id);
		phone->flashblocksize = 0;
		break;
	}

	return 0;
}

int loader_activate_gdfs(struct sp_port *port)
{
	printf("Activating GDFS.. ");

	uint8_t cmd_buf[8];
	int cmd_len = cmd_encode_binary_packet(CMD_ACTIVATE_GDFS, NULL, 0, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	// --- send command
	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	// Wait for reply
	uint8_t resp[7];
	int rcv_len = serial_wait_packet(port, resp, sizeof(resp), 20 * TIMEOUT);
	if (rcv_len <= 0)
		return -1;

	struct packetdata_t repl;
	if (cmd_decode_packet(resp, rcv_len, &repl) != 0)
		return -1;

	if (repl.cmd != 0x1D && repl.data[0] != 0x00) {
		printf("failed\n");
		return -1;
	}

	printf("activated\n");
	return 0;
}

int loader_shutdown(struct sp_port *port)
{
	printf("Shutdown phone\n");

	if (loader_type == LDR_CHIPSELECT)
		goto exit_done;

	uint8_t cmd_buf[8];
	int cmd_len = cmd_encode_binary_packet(CMD_SHUTDOWN, NULL, 0, cmd_buf);
	if (cmd_len <= 0)
		return -1;

	if (serial_send_packetdata_ack(port, cmd_buf, cmd_len) < 0)
		return -1;

	if (serial_wait_ack(port, 10 * TIMEOUT) != 0)
		return -1;

exit_done:
	printf("Done");
	return 0;
}

// should activate flash_mode when success, only for identify action.
int loader_enter_flashmode(struct sp_port *port, struct phone_info *phone)
{
	switch (phone->chip_id) {
	case DB2000:
		if (phone->erom_cid == 49)
			return loader_send_qhldr(port, phone, DB2000_PILOADER_RED_CID03_P3B);
		else if (phone->erom_cid == 36)
			return loader_send_qhldr(port, phone, DB2000_PILOADER_RED_CID00_R3A);
		return loader_send_qhldr(port, phone, DB2000_PILOADER_RED_CID03_P3B);
	case DB2010_1:
	case DB2010_2:
		if (phone->erom_cid <= 36)
			return loader_send_qhldr(port, phone, DB2010_PILOADER_RED_CID00_P3L);
		return loader_send_qhldr(port, phone, DB2010_PILOADER_RED_CID00_P4D);
	case DB2020:
		return loader_send_qhldr(port, phone, DB2020_PILOADER_RED_CID01_P3M);
	default:
		fprintf(stderr, "[QHLDR] Unknown CHIP ID %X\n", phone->chip_id);
		return -1;
	}
}

// Return number of bytes received, or -1 on exit_error
int loader_send_packet_pnx(struct sp_port *port, uint8_t block, uint8_t msb, uint8_t lsb, uint8_t *resp,
                           size_t resp_max)
{
	uint8_t cmd_buf[7] = {'I', 'C', 'G', '1', block, lsb, msb};

	// send data
	if (serial_write(port, cmd_buf, sizeof(cmd_buf)) < 0)
		return -1;

	uint8_t hdr[3];
	int rcv_len = serial_read(port, hdr, sizeof(hdr), 10 * TIMEOUT);
	if (rcv_len != (int)sizeof(hdr))
		return -1;

	if (hdr[0] != block || hdr[1] != lsb || hdr[2] != msb)
		return -1;

	uint8_t len[4];
	rcv_len = serial_read(port, len, sizeof(len), 10 * TIMEOUT);
	if (rcv_len != (int)sizeof(len))
		return -1;

	/* Sin signo: un tamaño >= 2^31 era negativo como int, pasaba la
	 * comparación y llegaba a serial_read como un tamaño enorme. */
	uint32_t datasize = get_word(len);
	if (datasize > resp_max || datasize > 0x7FFFFFFF)
		return -1;

	rcv_len = serial_read(port, resp, datasize, 10 * TIMEOUT);
	if (rcv_len != (int)datasize)
		return -1;

	return datasize;
}
