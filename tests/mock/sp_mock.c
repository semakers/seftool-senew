/*
 * libserialport de mentira: el «teléfono» es un guion de bytes.
 *
 * SEFTOOL_GUION=<archivo>: una línea por trozo, en hexadecimal. Cada trozo
 * es lo que el teléfono manda de una vez; una lectura nunca junta dos
 * trozos (como el teléfono, que no adelanta la respuesta siguiente).
 * Lo que escribe seftool se descarta (o va a SEFTOOL_TX si está puesto).
 * Al agotarse el guion el proceso sale con 77: «fin limpio del guion».
 * Se compila junto con mac/libserialport.h, con ASan y UBSan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "libserialport.h"

struct sp_port {
	int abierto;
};

static uint8_t *trozo[200000];
static size_t largo[200000];
static size_t ntrozos, actual, pos;
static FILE *tx;

static int hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	c |= 0x20;
	return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}

static void cargar(void)
{
	const char *ruta = getenv("SEFTOOL_GUION");
	FILE *f = ruta ? fopen(ruta, "r") : NULL;
	if (!f) {
		fprintf(stderr, "[mock] sin SEFTOOL_GUION\n");
		_exit(2);
	}
	char *linea = NULL;
	size_t cap = 0;
	ssize_t n;
	while ((n = getline(&linea, &cap, f)) > 0 && ntrozos < 200000) {
		uint8_t *b = malloc(n / 2 + 1);
		size_t k = 0;
		int alto = -1;
		for (ssize_t i = 0; i < n; i++) {
			int v = hexval(linea[i]);
			if (v < 0)
				continue;
			if (alto < 0)
				alto = v;
			else {
				b[k++] = (uint8_t)(alto << 4 | v);
				alto = -1;
			}
		}
		if (k == 0) {
			free(b);
			continue;
		}
		trozo[ntrozos] = b;
		largo[ntrozos++] = k;
	}
	free(linea);
	fclose(f);
	const char *t = getenv("SEFTOOL_TX");
	if (t)
		tx = fopen(t, "w");
}

enum sp_return sp_get_port_by_name(const char *portname, struct sp_port **port_ptr)
{
	(void)portname;
	*port_ptr = calloc(1, sizeof(struct sp_port));
	cargar();
	return SP_OK;
}

void sp_free_port(struct sp_port *port) { free(port); }
enum sp_return sp_open(struct sp_port *p, enum sp_mode m) { (void)m; p->abierto = 1; return SP_OK; }
enum sp_return sp_close(struct sp_port *p) { p->abierto = 0; return SP_OK; }
enum sp_return sp_set_baudrate(struct sp_port *p, int b) { (void)p; (void)b; return SP_OK; }
enum sp_return sp_set_bits(struct sp_port *p, int b) { (void)p; (void)b; return SP_OK; }
enum sp_return sp_set_parity(struct sp_port *p, enum sp_parity x) { (void)p; (void)x; return SP_OK; }
enum sp_return sp_set_stopbits(struct sp_port *p, int s) { (void)p; (void)s; return SP_OK; }
enum sp_return sp_set_flowcontrol(struct sp_port *p, enum sp_flowcontrol f) { (void)p; (void)f; return SP_OK; }
enum sp_return sp_set_rts(struct sp_port *p, enum sp_rts x) { (void)p; (void)x; return SP_OK; }
enum sp_return sp_set_dtr(struct sp_port *p, enum sp_dtr x) { (void)p; (void)x; return SP_OK; }
enum sp_return sp_flush(struct sp_port *p, enum sp_buffer b) { (void)p; (void)b; return SP_OK; }

/* Como el teléfono: un trozo nuevo solo sale después de que seftool
 * escribió algo. Sin escritura, la lectura vuelve con lo que tenga
 * (como un timeout real). Muchos timeouts seguidos = atasco: sale con 78. */
static int escrito = 1, vacias;

enum sp_return sp_blocking_read(struct sp_port *p, void *buf, size_t count, unsigned int timeout_ms)
{
	(void)p;
	(void)timeout_ms;
	uint8_t *dst = buf;
	size_t got = 0;
	while (got < count) {
		if (pos == 0) {
			if (!escrito)
				break;
			if (actual >= ntrozos) {
				if (got)
					break;
				fflush(stdout);
				if (tx)
					fflush(tx);
				fprintf(stderr, "[mock] fin del guion\n");
				_exit(77);
			}
		}
		size_t n = largo[actual] - pos;
		if (n > count - got)
			n = count - got;
		memcpy(dst + got, trozo[actual] + pos, n);
		got += n;
		pos += n;
		if (pos == largo[actual]) {
			actual++;
			pos = 0;
			escrito = 0;
		}
	}
	if (getenv("SEFTOOL_TRAZA"))
		fprintf(stderr, "[mock lee pide=%zu da=%zu trozo=%zu]\n", count, got, actual);
	if (got == 0 && ++vacias > 200) {
		fflush(stdout);
		if (tx)
			fflush(tx);
		fprintf(stderr, "[mock] atascado: 200 lecturas vacías\n");
		_exit(78);
	}
	if (got)
		vacias = 0;
	return (enum sp_return)got;
}

enum sp_return sp_blocking_write(struct sp_port *p, const void *buf, size_t count, unsigned int timeout_ms)
{
	(void)p;
	(void)timeout_ms;
	escrito = 1;
	if (tx) {
		const uint8_t *b = buf;
		for (size_t i = 0; i < count; i++)
			fprintf(tx, "%02x", b[i]);
		fputc('\n', tx);
	}
	return (enum sp_return)count;
}

char *sp_last_error_message(void) { return "mock"; }
void sp_free_error_message(char *m) { (void)m; }
