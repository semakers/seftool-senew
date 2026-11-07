/* Lo que prueba_readflash.c no ve y una prueba de MUTACIÓN delató (2026-10-17): el escritor atómico con el
 * disco y rename/fsync bajo control. Sin teléfono y sin simular ningún loader.
 * Se compila con -Wl,--wrap=rename -Wl,--wrap=fsync (ld de GNU: solo Linux). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libserialport.h>
#include <core/common.h>
#include <emp/v3/action.h>
#include <emp/v3/flash.h>

static int fallos;
#define CHECK(cond, msg)                                                                                \
	do {                                                                                            \
		if (cond)                                                                               \
			printf("  ok    %s\n", msg);                                                       \
		else {                                                                                  \
			printf("  MAL   %s\n", msg);                                                       \
			fallos++;                                                                       \
		}                                                                                       \
	} while (0)

#define TROZO 0x10000u

/* ---- envoltorios: registran el ORDEN de fsync/rename y dejan fallar un rename a voluntad ---- */
extern int __real_rename(const char *, const char *);
extern int __real_fsync(int);
static char secuencia[64];
static int rename_n, rename_falla_en; /* 0 = nunca */
static void anota(char c)
{
	size_t n = strlen(secuencia);
	if (n + 1 < sizeof(secuencia)) {
		secuencia[n] = c;
		secuencia[n + 1] = 0;
	}
}
int __wrap_rename(const char *a, const char *b)
{
	rename_n++;
	if (rename_falla_en && rename_n == rename_falla_en) {
		anota('x');
		errno = EACCES;
		return -1;
	}
	anota('R');
	return __real_rename(a, b);
}
int __wrap_fsync(int fd)
{
	anota('F');
	return __real_fsync(fd);
}
static void arma(int falla_en)
{
	secuencia[0] = 0;
	rename_n = 0;
	rename_falla_en = falla_en;
}

static int existe(const char *p) { return access(p, 0) == 0; }
static long tam(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 ? (long)st.st_size : -1;
}
static void rellena(uint8_t *b, size_t n, int semilla)
{
	for (size_t i = 0; i < n; i++)
		b[i] = (uint8_t)(semilla * 31 + i * 7 + (i >> 8));
}
static int es_patron(const char *ruta, int trozos, int semilla)
{
	size_t n = (size_t)trozos * TROZO;
	if (tam(ruta) != (long)n)
		return 0;
	uint8_t *e = malloc(TROZO), *l = malloc(TROZO);
	FILE *f = fopen(ruta, "rb");
	int ok = e && l && f;
	for (int t = 0; ok && t < trozos; t++) {
		rellena(e, TROZO, semilla);
		ok = fread(l, 1, TROZO, f) == TROZO && memcmp(e, l, TROZO) == 0;
	}
	if (f)
		fclose(f);
	free(e);
	free(l);
	return ok;
}
static int es_texto(const char *ruta, const char *txt)
{
	char buf[64] = {0};
	FILE *f = fopen(ruta, "rb");
	if (!f)
		return 0;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	return n == strlen(txt) && memcmp(buf, txt, n) == 0;
}
static void escribe_texto(const char *ruta, const char *txt)
{
	FILE *f = fopen(ruta, "wb");
	if (f) {
		fputs(txt, f);
		fclose(f);
	}
}
static int vuelca(struct flash_dump *d, int trozos, int semilla)
{
	uint8_t *b = malloc(TROZO);
	int rc = b ? 0 : -1;
	for (int t = 0; rc == 0 && t < trozos; t++) {
		rellena(b, TROZO, semilla);
		rc = flash_dump_write(d, b, TROZO);
	}
	free(b);
	return rc;
}

int main(void)
{
	char hex[41];
	struct flash_dump *d;
	const char *why;
	setvbuf(stdout, NULL, _IOLBF, 0);

	puts("(X1) el disco manda: lo que quedó en el .INCOMPLETO, no lo que creemos haber escrito");
	d = flash_dump_open("./backup/recorta.bin");
	CHECK(d && vuelca(d, 3, 1) == 0, "escribe 3 trozos");
	CHECK(truncate("./backup/recorta.bin.INCOMPLETO", TROZO) == 0, "alguien recorta el .INCOMPLETO a 1 trozo");
	CHECK(d && flash_dump_commit(d, 3 * (size_t)TROZO, hex) != 0, "commit: el disco no tiene lo esperado -> falla");
	CHECK(!existe("./backup/recorta.bin"), "NO se publica");

	puts("(X2) el contador del escritor también cuenta: el disco coincide pero el escritor no escribió eso");
	d = flash_dump_open("./backup/crece.bin");
	CHECK(d && vuelca(d, 3, 1) == 0, "escribe 3 trozos");
	{
		uint8_t *b = malloc(TROZO);
		FILE *x = fopen("./backup/crece.bin.INCOMPLETO", "ab");
		rellena(b, TROZO, 9);
		if (x) {
			fwrite(b, 1, TROZO, x);
			fclose(x);
		}
		free(b);
	}
	CHECK(d && flash_dump_commit(d, 4 * (size_t)TROZO, hex) != 0, "commit esperando 4 con 3 escritos (disco: 4) -> falla");
	CHECK(!existe("./backup/crece.bin"), "NO se publica");

	puts("(X3) el apartado del bueno anterior falla (.anterior es un directorio NO vacío): no se pierde nada");
	escribe_texto("./backup/apart.bin", "BUENO\n");
	mkdir("./backup/apart.bin.anterior", 0755);
	escribe_texto("./backup/apart.bin.anterior/x", "x");
	d = flash_dump_open("./backup/apart.bin");
	CHECK(d && vuelca(d, 2, 1) == 0, "escribe 2 trozos");
	CHECK(d && flash_dump_commit(d, 2 * (size_t)TROZO, hex) != 0, "commit: no puede apartar el anterior -> falla");
	CHECK(es_texto("./backup/apart.bin", "BUENO\n"), "el bueno sigue INTACTO en su nombre");
	CHECK(tam("./backup/apart.bin.INCOMPLETO") == 2 * (long)TROZO, "lo nuevo queda en .INCOMPLETO");

	puts("(X4) reintentar tras un fallo: el .INCOMPLETO viejo no se anexa");
	d = flash_dump_open("./backup/reint.bin");
	CHECK(d && vuelca(d, 3, 1) == 0, "primer intento: 3 trozos");
	if (d)
		flash_dump_abort(d, "prueba");
	d = flash_dump_open("./backup/reint.bin");
	CHECK(d && vuelca(d, 2, 2) == 0, "segundo intento: 2 trozos");
	CHECK(d && flash_dump_commit(d, 2 * (size_t)TROZO, hex) == 0, "el segundo intento SÍ se publica");
	CHECK(es_patron("./backup/reint.bin", 2, 2), "y solo tiene los 2 trozos del segundo intento");

	puts("(X5) si publicar falla, el bueno anterior vuelve a su sitio");
	escribe_texto("./backup/rollb.bin", "BUENO\n");
	arma(2); /* el 2.º rename (INCOMPLETO -> definitivo) falla */
	d = flash_dump_open("./backup/rollb.bin");
	CHECK(d && vuelca(d, 2, 1) == 0, "escribe 2 trozos");
	CHECK(d && flash_dump_commit(d, 2 * (size_t)TROZO, hex) != 0, "commit: no puede publicar -> falla");
	CHECK(es_texto("./backup/rollb.bin", "BUENO\n"), "el bueno anterior está de vuelta en su nombre");
	CHECK(existe("./backup/rollb.bin.INCOMPLETO"), "lo nuevo queda en .INCOMPLETO");
	arma(0);

	puts("(X6) durabilidad: fsync ANTES de publicar (orden F, luego los rename)");
	arma(0);
	d = flash_dump_open("./backup/fsync1.bin");
	CHECK(d && vuelca(d, 1, 1) == 0 && flash_dump_commit(d, TROZO, hex) == 0, "volcado nuevo");
	CHECK(strcmp(secuencia, "FR") == 0, "secuencia = fsync, rename (sin anterior)");
	arma(0);
	d = flash_dump_open("./backup/fsync1.bin");
	CHECK(d && vuelca(d, 1, 2) == 0 && flash_dump_commit(d, TROZO, hex) == 0, "segundo volcado sobre el mismo nombre");
	CHECK(strcmp(secuencia, "FRR") == 0, "secuencia = fsync, rename (apartar), rename (publicar)");

	puts("(X7) el MOTIVO de la ventana: un tamaño de 2^64-1 da la vuelta de 32 bits, no «más allá de la NOR»");
	why = NULL;
	CHECK(flash_read_window_check(0x44000000u, UINT64_MAX, &why) == -1 && why && strstr(why, "32 bits"),
	      "0x44000000 + UINT64_MAX: se rechaza POR la vuelta de 32 bits");

	puts("(X8) el FINAL del rango también cuenta: addr+size == 2^32 se truncaría a 0 en la orden de lectura");
	{
		struct phone_info phone = {0};
		snprintf(phone.otp_imei, sizeof(phone.otp_imei), "TEST");
		CHECK(flash_read(NULL, &phone, 0xFFFF0000u, 0x10000u) != 0, "0xFFFF0000 + 0x10000 (final = 2^32): rechazado");
		CHECK(!existe("./backup/flashdump_TEST_FFFF0000_00010000.bin.INCOMPLETO"), "y no se creó ni un .INCOMPLETO");
		why = NULL;
		CHECK(flash_read_window_check(0xFFFF0000u, 0x10000u, &why) == -1, "la ventana 0xFFFF0000 + 0x10000 tampoco cabe");
	}

	printf("fallos=%d\n", fallos);
	return fallos == 0 ? 0 : 1;
}
