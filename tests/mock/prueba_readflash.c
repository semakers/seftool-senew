/* read-flash --anycid: pruebas PURAS (sin teléfono, sin simular ningún loader), bajo ASan/UBSan.
 *
 *   ventana      flash_read_window_check: qué ventanas caben en la NOR y cuáles no
 *   plan         flash_read_plan_*: la NOR entera son 1024 trozos de 64 KiB, del 0x44000000 al 0x47FF0000
 *   dump         flash_dump_*: el escritor atómico (.INCOMPLETO / .anterior), con búferes sintéticos
 *
 * Se compila con todo src/ menos main.c y el puerto de mentira (sp_mock.c): nunca se abre un puerto.
 * Se corre dentro de un directorio de trabajo con ./backup (lo prepara prueba_readflash.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <libserialport.h>
#include <core/common.h>
#include <emp/v3/action.h>
#include <emp/v3/flash.h>
#include <emp/v3/loader.h>
#include <senew/senew.h>

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

static int existe(const char *p) { return access(p, 0) == 0; }

static long tam(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

/* Contenido sintético y reproducible: n bytes de la "semilla". */
static void rellena(uint8_t *b, size_t n, int semilla)
{
	for (size_t i = 0; i < n; i++)
		b[i] = (uint8_t)(semilla * 31 + i * 7 + (i >> 8));
}

/* ¿El archivo es exactamente `trozos` trozos del patrón de esa semilla? */
static int es_patron(const char *ruta, int trozos, int semilla)
{
	size_t n = (size_t)trozos * TROZO;
	if (tam(ruta) != (long)n)
		return 0;
	uint8_t *esperado = malloc(TROZO), *leido = malloc(TROZO);
	FILE *f = fopen(ruta, "rb");
	int ok = esperado && leido && f;
	for (int t = 0; ok && t < trozos; t++) {
		rellena(esperado, TROZO, semilla);
		ok = fread(leido, 1, TROZO, f) == TROZO && memcmp(esperado, leido, TROZO) == 0;
	}
	if (f)
		fclose(f);
	free(esperado);
	free(leido);
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

/* Escribe `trozos` trozos del patrón por el escritor. */
static int vuelca(struct flash_dump *d, int trozos, int semilla)
{
	uint8_t *b = malloc(TROZO);
	if (!b)
		return -1;
	int rc = 0;
	for (int t = 0; rc == 0 && t < trozos; t++) {
		rellena(b, TROZO, semilla);
		rc = flash_dump_write(d, b, TROZO);
	}
	free(b);
	return rc;
}

static void t_ventana(void)
{
	const char *why = NULL;
	puts("(1) ventana: qué cabe en la NOR (0x44000000..0x48000000)");

	CHECK(flash_read_window_check(0x44000000u, 0x4000000u, &why) == 0, "la NOR entera cabe");
	CHECK(flash_read_window_check(0x44000000u, 1, &why) == 0, "el primer byte cabe");
	CHECK(flash_read_window_check(0x47FFFFFFu, 1, &why) == 0, "el último byte cabe");
	CHECK(flash_read_window_check(0x44140000u, 0x3EC0000u, &why) == 0, "del main al final cabe");
	CHECK(flash_read_window_check(0x44000000u, 0x4000000u, NULL) == 0, "why puede ser NULL");

	why = NULL;
	CHECK(flash_read_window_check(0x44000000u, 0, &why) == -1 && why && strstr(why, "tamaño"), "tamaño 0: no");
	why = NULL;
	CHECK(flash_read_window_check(0x43FFFFFFu, 2, &why) == -1 && why && strstr(why, "debajo"),
	      "empieza un byte por debajo: no");
	why = NULL;
	CHECK(flash_read_window_check(0x47FF0000u, 0x20000u, &why) == -1 && why && strstr(why, "más allá"),
	      "0x47FF0000 + 0x20000 se sale por arriba: no");
	CHECK(flash_read_window_check(0x47FFFFFFu, 2, NULL) == -1, "un byte más allá del final: no");
	CHECK(flash_read_window_check(0x44000000u, 0x4000001u, NULL) == -1, "la NOR + 1 byte: no");
	CHECK(flash_read_window_check(0x48000000u, 1, NULL) == -1, "empezar en el final: no");
	CHECK(flash_read_window_check(0, 0x10000u, NULL) == -1, "dirección 0: no");
	why = NULL;
	CHECK(flash_read_window_check(0xFFFF0000u, 0x20000u, &why) == -1 && why && strstr(why, "32 bits"),
	      "0xFFFF0000 + 0x20000 da la vuelta de 32 bits: no (y lo dice)");
	CHECK(flash_read_window_check(0x44000000u, UINT64_MAX, NULL) == -1, "tamaño enorme: no");
	CHECK(flash_read_window_check(0xFFFFFFFFu, UINT64_MAX, NULL) == -1, "todo al máximo: no");
}

static void t_plan(void)
{
	uint32_t a;
	size_t l, suma = 0, n;
	puts("(2) plan: la NOR entera son 1024 trozos de 64 KiB");

	n = flash_read_plan_count(0x4000000u);
	CHECK(n == 1024, "64 MiB = 1024 trozos");
	CHECK(flash_read_plan_at(0x44000000u, 0x4000000u, 0, &a, &l) == 0 && a == 0x44000000u && l == TROZO,
	      "el trozo 0 es 0x44000000 + 64 KiB");
	CHECK(flash_read_plan_at(0x44000000u, 0x4000000u, 1023, &a, &l) == 0 && a == 0x47FF0000u && l == TROZO,
	      "el trozo 1023 es 0x47FF0000 + 64 KiB (acaba en 0x48000000)");
	CHECK(flash_read_plan_at(0x44000000u, 0x4000000u, 1024, &a, &l) == -1, "el trozo 1024 no existe");

	int contiguo = 1;
	uint32_t previo_fin = 0x44000000u;
	for (size_t i = 0; i < n; i++) {
		if (flash_read_plan_at(0x44000000u, 0x4000000u, i, &a, &l) != 0 || a != previo_fin) {
			contiguo = 0;
			break;
		}
		previo_fin = a + (uint32_t)l;
		suma += l;
	}
	CHECK(contiguo && suma == 0x4000000u, "los trozos son contiguos y suman exactamente 0x4000000");

	CHECK(flash_read_plan_count(0x18000u) == 2, "0x18000 bytes = 2 trozos");
	CHECK(flash_read_plan_at(0x44140000u, 0x18000u, 1, &a, &l) == 0 && a == 0x44150000u && l == 0x8000u,
	      "la cola es el resto (0x8000), no 64 KiB");
	CHECK(flash_read_plan_count(1) == 1 && flash_read_plan_at(0x44000000u, 1, 0, &a, &l) == 0 && l == 1, "1 byte");
	CHECK(flash_read_plan_count(0) == 0 && flash_read_plan_at(0x44000000u, 0, 0, &a, &l) == -1, "0 bytes: sin plan");
}

static void t_dump(void)
{
	char hex[41];
	struct flash_dump *d;
	puts("(3) escritor atómico: .INCOMPLETO y .anterior (búferes sintéticos)");

	/* A. abortar deja el parcial y nada definitivo */
	d = flash_dump_open("./backup/abort.bin");
	CHECK(d != NULL, "A: se abre");
	CHECK(d && vuelca(d, 3, 1) == 0, "A: escribe 3 trozos");
	if (d)
		flash_dump_abort(d, "prueba");
	CHECK(existe("./backup/abort.bin.INCOMPLETO") && tam("./backup/abort.bin.INCOMPLETO") == 3 * (long)TROZO,
	      "A: queda el .INCOMPLETO con lo escrito");
	CHECK(!existe("./backup/abort.bin"), "A: NO hay archivo definitivo");

	/* B. abortar no destruye el volcado bueno que ya había */
	escribe_texto("./backup/respeta.bin", "BUENO\n");
	d = flash_dump_open("./backup/respeta.bin");
	CHECK(d && vuelca(d, 2, 1) == 0, "B: empieza otro volcado sobre un nombre que ya existe");
	CHECK(es_texto("./backup/respeta.bin", "BUENO\n"), "B: el bueno anterior sigue INTACTO mientras se escribe");
	if (d)
		flash_dump_abort(d, "prueba");
	CHECK(es_texto("./backup/respeta.bin", "BUENO\n"), "B: y tras abortar");
	CHECK(!existe("./backup/respeta.bin.anterior"), "B: abortar no crea .anterior");

	/* C. un tamaño que no cuadra no se publica */
	d = flash_dump_open("./backup/mal.bin");
	CHECK(d && vuelca(d, 3, 1) == 0, "C: escribe 3 trozos");
	CHECK(d && flash_dump_commit(d, 4 * (size_t)TROZO, hex) != 0, "C: commit esperando 4 trozos: falla");
	CHECK(!existe("./backup/mal.bin"), "C: NO se publica");
	CHECK(existe("./backup/mal.bin.INCOMPLETO"), "C: queda el .INCOMPLETO");

	/* D. el caso bueno */
	d = flash_dump_open("./backup/bueno.bin");
	CHECK(d && vuelca(d, 3, 2) == 0, "D: escribe 3 trozos");
	hex[0] = 0;
	CHECK(d && flash_dump_commit(d, 3 * (size_t)TROZO, hex) == 0, "D: commit bueno");
	CHECK(es_patron("./backup/bueno.bin", 3, 2), "D: el definitivo tiene los bytes escritos");
	CHECK(!existe("./backup/bueno.bin.INCOMPLETO"), "D: ya no hay .INCOMPLETO");
	CHECK(!existe("./backup/bueno.bin.anterior"), "D: sin .anterior (no había nada que apartar)");
	CHECK(strlen(hex) == 40, "D: devuelve un SHA-1 de 40 caracteres");
	printf("SHA1_BUENO %s\n", hex);

	/* E. un segundo volcado aparta el primero a .anterior */
	d = flash_dump_open("./backup/dos.bin");
	CHECK(d && vuelca(d, 2, 3) == 0 && flash_dump_commit(d, 2 * (size_t)TROZO, hex) == 0, "E: primer volcado");
	d = flash_dump_open("./backup/dos.bin");
	CHECK(d && vuelca(d, 2, 4) == 0 && flash_dump_commit(d, 2 * (size_t)TROZO, hex) == 0, "E: segundo volcado");
	CHECK(es_patron("./backup/dos.bin", 2, 4), "E: el definitivo es el segundo");
	CHECK(es_patron("./backup/dos.bin.anterior", 2, 3), "E: el primero quedó en .anterior, entero");

	/* F. abortar un tercero deja los dos como están */
	d = flash_dump_open("./backup/dos.bin");
	CHECK(d && vuelca(d, 1, 5) == 0, "F: tercer volcado, a medias");
	if (d)
		flash_dump_abort(d, "prueba");
	CHECK(es_patron("./backup/dos.bin", 2, 4), "F: el definitivo no se tocó");
	CHECK(es_patron("./backup/dos.bin.anterior", 2, 3), "F: el .anterior tampoco");

	/* G/H. rutas que no valen */
	char larga[1100];
	memset(larga, 'x', sizeof(larga) - 1);
	larga[sizeof(larga) - 1] = 0;
	CHECK(flash_dump_open(larga) == NULL, "G: una ruta de más de 1023 caracteres no se acepta");
	CHECK(flash_dump_open("./no_existe_este_directorio/x.bin") == NULL, "H: un directorio que no existe: NULL");
}

static void t_force(void)
{
	puts("(5) SEFTOOL_SENEW_FORCE: solo vale exactamente «1»");
	unsetenv("SEFTOOL_SENEW_FORCE");
	CHECK(!senew_force_requested(), "sin definir: no");
	setenv("SEFTOOL_SENEW_FORCE", "1", 1);
	CHECK(senew_force_requested(), "«1»: sí");
	setenv("SEFTOOL_SENEW_FORCE", "0", 1);
	CHECK(!senew_force_requested(), "«0»: no (con getenv()!=NULL valía)");
	setenv("SEFTOOL_SENEW_FORCE", "", 1);
	CHECK(!senew_force_requested(), "vacía: no (con getenv()!=NULL valía)");
	setenv("SEFTOOL_SENEW_FORCE", "11", 1);
	CHECK(!senew_force_requested(), "«11»: no");
	setenv("SEFTOOL_SENEW_FORCE", "yes", 1);
	CHECK(!senew_force_requested(), "«yes»: no");
	unsetenv("SEFTOOL_SENEW_FORCE");

	/* La misma regla para SEFTOOL_BREAK_CONFIRM (break.c: escribir un bootloader parchado). */
	unsetenv("SEFTOOL_BREAK_CONFIRM");
	CHECK(!senew_env_one("SEFTOOL_BREAK_CONFIRM"), "BREAK_CONFIRM sin definir: no");
	setenv("SEFTOOL_BREAK_CONFIRM", "1", 1);
	CHECK(senew_env_one("SEFTOOL_BREAK_CONFIRM"), "BREAK_CONFIRM=1: sí");
	setenv("SEFTOOL_BREAK_CONFIRM", "0", 1);
	CHECK(!senew_env_one("SEFTOOL_BREAK_CONFIRM"), "BREAK_CONFIRM=0: no (con getenv()==NULL confirmaba)");
	setenv("SEFTOOL_BREAK_CONFIRM", "", 1);
	CHECK(!senew_env_one("SEFTOOL_BREAK_CONFIRM"), "BREAK_CONFIRM vacía: no (con getenv()==NULL confirmaba)");
	unsetenv("SEFTOOL_BREAK_CONFIRM");
}

/* --imei: la identidad entre sesiones (loader_check_expected_imei), sin puerto. */
static void t_imei(void)
{
	struct phone_info p = {0};
	puts("(7) --imei: loader_check_expected_imei");

	CHECK(loader_check_expected_imei(&p) == 0, "sin --imei y sin IMEI: no se comprueba nada");
	snprintf(p.otp_imei, sizeof(p.otp_imei), "35000000000000");
	CHECK(loader_check_expected_imei(&p) == 0, "sin --imei y con IMEI: pasa");

	snprintf(p.expect_imei, sizeof(p.expect_imei), "35000000000000");
	CHECK(loader_check_expected_imei(&p) == 0, "el mismo IMEI: pasa");
	snprintf(p.expect_imei, sizeof(p.expect_imei), "35000000000001");
	CHECK(loader_check_expected_imei(&p) == -1, "otro IMEI (cambia el último dígito): se niega");
	snprintf(p.expect_imei, sizeof(p.expect_imei), "3500000000000");
	CHECK(loader_check_expected_imei(&p) == -1, "un PREFIJO del IMEI no vale");
	snprintf(p.expect_imei, sizeof(p.expect_imei), "350000000000000");
	CHECK(loader_check_expected_imei(&p) == -1, "un IMEI más largo (15 dígitos) tampoco");

	snprintf(p.expect_imei, sizeof(p.expect_imei), "35000000000000");
	p.otp_imei[0] = '\0';
	CHECK(loader_check_expected_imei(&p) == -1, "el aparato no dijo su IMEI: no se puede comprobar, se niega");
}

/* Lo que sale por stdout al llamar a loader_hint_run_executer, con la negativa de --imei puesta o no. */
static void hint_a_fichero(int imei_refused, const char *ruta)
{
	struct phone_info p = {0};
	p.imei_refused = imei_refused;
	fflush(stdout);
	int guardado = dup(1);
	int fd = open(ruta, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	dup2(fd, 1);
	close(fd);
	loader_hint_run_executer(&p);
	fflush(stdout);
	dup2(guardado, 1);
	close(guardado);
}

/* La negativa de --imei NO debe parecerse a «falta el executer»: SENUS lee «Run executer first» y repite el ritual. */
static void t_imei_no_es_executer(void)
{
	struct phone_info p = {0};
	puts("(7b) la negativa de --imei deja su marca y no se disfraza de «Run executer first»");

	snprintf(p.otp_imei, sizeof(p.otp_imei), "35000000000000");
	snprintf(p.expect_imei, sizeof(p.expect_imei), "35000000000000");
	CHECK(loader_check_expected_imei(&p) == 0 && p.imei_refused == 0, "el mismo IMEI: pasa y no deja marca");
	snprintf(p.expect_imei, sizeof(p.expect_imei), "99999999999999");
	CHECK(loader_check_expected_imei(&p) == -1 && p.imei_refused == 1, "otro IMEI: se niega y deja la marca");

	struct phone_info q = {0};
	snprintf(q.expect_imei, sizeof(q.expect_imei), "35000000000000");
	CHECK(loader_check_expected_imei(&q) == -1 && q.imei_refused == 1, "el aparato no dijo su IMEI: también deja la marca");

	hint_a_fichero(0, "hint0.txt");
	hint_a_fichero(1, "hint1.txt");
	char b0[128] = {0}, b1[128] = {0};
	FILE *f = fopen("hint0.txt", "r");
	if (f) {
		size_t n = fread(b0, 1, sizeof(b0) - 1, f);
		(void)n;
		fclose(f);
	}
	f = fopen("hint1.txt", "r");
	if (f) {
		size_t n = fread(b1, 1, sizeof(b1) - 1, f);
		(void)n;
		fclose(f);
	}
	CHECK(strstr(b0, "[QHTRY] Run executer first") != NULL, "sin negativa de --imei: avisa de que falta el executer");
	CHECK(b1[0] == '\0', "con la negativa de --imei: NO dice nada del executer");
}

/* La compuerta de read-flash --anycid, sin puerto. Los mensajes se prueban por la línea de órdenes
 * (grupo A de prueba_readflash.sh); aquí, qué se acepta y qué no. */
static int gate(int cid, uint32_t addr, uint64_t size)
{
	struct phone_info phone = {0};
	phone.chip_id = DB2020;
	phone.anycid = 1;
	phone.expect_cid = cid;
	return action_read_flash_gate(&phone, addr, size);
}

static void t_gate(void)
{
	puts("(6) compuerta de read-flash --anycid (sin puerto)");
	unsetenv("SEFTOOL_SENEW_FORCE");

	CHECK(gate(52, 0x44000000u, 0x4000000u) == 0, "CID 52, NOR entera: pasa");
	CHECK(gate(49, 0x44000000u, 0x4000000u) == 0, "CID 49: pasa");
	CHECK(gate(51, 0x44000000u, 0x4000000u) == 0, "CID 51: pasa");
	CHECK(gate(52, 0x44000000u, 0x20000u) == 0, "CID 52, sonda de 2 bloques: pasa");
	CHECK(gate(0, 0x44000000u, 0x4000000u) == -1, "sin --cid: se niega");
	CHECK(gate(53, 0x44000000u, 0x4000000u) == -1, "CID 53 (sin verificar): se niega");
	CHECK(gate(99, 0x44000000u, 0x4000000u) == -1, "CID 99 (no existe): se niega");
	CHECK(gate(50, 0x44000000u, 0x4000000u) == -1, "CID 50 (no es de este W580): se niega");
	CHECK(gate(52, 0x43FF0000u, 0x20000u) == -1, "empieza por debajo de la NOR: se niega");
	CHECK(gate(52, 0x47FF0000u, 0x20000u) == -1, "se sale por arriba: se niega");
	CHECK(gate(52, 0xFFFF0000u, 0x20000u) == -1, "da la vuelta de 32 bits: se niega");
	CHECK(gate(52, 0x44000000u, 0) == -1, "tamaño 0: se niega");

	setenv("SEFTOOL_SENEW_FORCE", "1", 1);
	CHECK(gate(53, 0x44000000u, 0x4000000u) == 0, "FORCE=1: CID 53 pasa");
	CHECK(gate(0, 0x44000000u, 0x4000000u) == 0, "FORCE=1: sin --cid pasa");
	CHECK(gate(52, 0x43FF0000u, 0x20000u) == 0, "FORCE=1: la ventana también cede");
	setenv("SEFTOOL_SENEW_FORCE", "0", 1);
	CHECK(gate(53, 0x44000000u, 0x4000000u) == -1, "FORCE=0 no vale: CID 53 sigue negado");
	unsetenv("SEFTOOL_SENEW_FORCE");
}

/* bflash_repair_boot con need_rest=0 (read-flash sin --restore-boot) NO manda nada al puerto. El mock
 * apunta a SEFTOOL_TX lo que se escribe; aquí se sale con exit() normal para que stdio lo vuelque, y
 * el guion de la prueba comprueba que el archivo está vacío. `control` escribe un byte para demostrar
 * que esa forma de medir no está vacía de por sí. */
static int m_repair(int escribe_control)
{
	struct sp_port *port;
	if (sp_get_port_by_name("mock", &port) != SP_OK)
		return 2;
	sp_open(port, SP_MODE_READ_WRITE);
	if (escribe_control) {
		uint8_t b = 0xAB;
		sp_blocking_write(port, &b, 1, 100);
		return 0;
	}
	struct phone_info phone = {0};
	phone.chip_id = DB2020;
	phone.anycid = 1;
	phone.need_rest = 0;
	int rc = bflash_repair_boot(port, &phone);
	printf("bflash_repair_boot(need_rest=0) rc=%d\n", rc);
	return rc;
}

/* flash_read rechaza ANTES de tocar el puerto (aquí es NULL: si lo tocara, reventaría). */
static void t_rechazo(void)
{
	struct phone_info phone = {0};
	snprintf(phone.otp_imei, sizeof(phone.otp_imei), "TEST");
	puts("(4) flash_read rechaza ventanas imposibles antes de tocar el puerto");

	CHECK(flash_read(NULL, &phone, 0xFFFF0000u, 0x20000) != 0, "0xFFFF0000 + 0x20000 (da la vuelta): rechazada");
	CHECK(flash_read(NULL, &phone, 0x44000000u, 0) != 0, "tamaño 0: rechazada");
	CHECK(flash_read(NULL, &phone, 0xFFFFFFFFu, SIZE_MAX) != 0, "tamaño enorme: rechazada");
	CHECK(!existe("./backup/flashdump_TEST_FFFF0000_00020000.bin.INCOMPLETO"), "y no se creó ni un .INCOMPLETO");
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0); /* que lo de stderr salga entre sus líneas, no antes de todas */
	if (argc > 1 && strcmp(argv[1], "repair") == 0)
		return m_repair(0);
	if (argc > 1 && strcmp(argv[1], "control") == 0)
		return m_repair(1);
	t_ventana();
	t_plan();
	t_dump();
	t_rechazo();
	t_force();
	t_gate();
	t_imei();
	t_imei_no_es_executer();
	printf("fallos=%d\n", fallos);
	return fallos == 0 ? 0 : 1;
}
