/*
 * libserialport sobre libusb para el «SEMC USB Flash» en macOS y Windows.
 * Ver libserialport.h. SENEW_USB_TRAZA=1 vuelca el tráfico a stderr.
 *
 * Windows (MSYS2/MinGW): el aparato necesita el driver WinUSB asignado (con
 * Zadig o con el instalador de SENUS); libusb solo puede abrir lo que lleva
 * WinUSB, libusbK o libusb0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <libusb.h>

#include "libserialport.h"

#define RX_CAP 65536

struct sp_port {
	uint16_t vid, pid;
	libusb_context *ctx;
	libusb_device_handle *h;
	int iface;
	uint8_t ep_in, ep_out;
	int mps_in; /* wMaxPacketSize del bulk IN */
	uint8_t rx[RX_CAP];
	size_t rx_pos, rx_len;
	int traza;
};

static char ultimo_error[256] = "sin error";

static void error(const char *que, int r)
{
	snprintf(ultimo_error, sizeof(ultimo_error), "%s: %s", que, libusb_error_name(r));
	/* Diagnóstico: tras el prologue, un `rcv_len=-2` instantáneo (20 ms) con
	 * timeout largo apunta a un error USB real (PIPE/IO/NO_DEVICE), no a un
	 * silencio del teléfono. Imprimimos siempre el nombre para no tener que
	 * encender traza completa. */
	fprintf(stderr, "[sp_usb] %s falló: %s (%d)\n", que, libusb_error_name(r), r);
}

#ifdef _WIN32
static long ahora_ms(void) { return (long)GetTickCount64(); }
static void dormir_ms(long ms) { Sleep((DWORD)ms); }
#else
static long ahora_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}
static void dormir_ms(long ms)
{
	struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
	nanosleep(&t, NULL);
}
#endif

static void volcar(const char *dir, const uint8_t *b, size_t n)
{
	fprintf(stderr, "[usb %s %zu]", dir, n);
	for (size_t i = 0; i < n && i < 64; i++)
		fprintf(stderr, " %02x", b[i]);
	fprintf(stderr, n > 64 ? " …\n" : "\n");
}

enum sp_return sp_get_port_by_name(const char *portname, struct sp_port **port_ptr)
{
	unsigned vid = 0x0fce, pid = 0xadde;
	if (strncmp(portname, "usb", 3) != 0) {
		snprintf(ultimo_error, sizeof(ultimo_error),
		         "con libusb el puerto es «usb» o «usb:VVVV:PPPP», no %s", portname);
		return SP_ERR_ARG;
	}
	if (portname[3] == ':' && sscanf(portname + 4, "%x:%x", &vid, &pid) != 2)
		return SP_ERR_ARG;

	struct sp_port *p = calloc(1, sizeof(*p));
	if (!p)
		return SP_ERR_MEM;
	p->vid = vid;
	p->pid = pid;
	p->iface = -1;
	const char *t = getenv("SENEW_USB_TRAZA");
	p->traza = t && *t && *t != '0';
	*port_ptr = p;
	return SP_OK;
}

void sp_free_port(struct sp_port *port) { free(port); }

/* Abre el ÚNICO aparato vid:pid. Si hay dos (p. ej. el W580 bueno y el roto
 * enchufados en modo servicio a la vez) se niega: libusb_open_device_with_vid_pid
 * abría "el primero que enumera", y upload-anycid/flash-anycid escriben en el
 * elegido. Devuelve 0 = abierto, -1 = no hay ninguno, -2 = hay más de uno. */
static int abrir_unico(struct sp_port *p)
{
	libusb_device **lista = NULL;
	ssize_t n = libusb_get_device_list(p->ctx, &lista);
	if (n < 0) {
		error("get_device_list", (int)n);
		return -1;
	}
	libusb_device *hallado = NULL;
	int cuantos = 0;
	for (ssize_t i = 0; i < n; i++) {
		struct libusb_device_descriptor d;
		if (libusb_get_device_descriptor(lista[i], &d) != 0)
			continue;
		if (d.idVendor != p->vid || d.idProduct != p->pid)
			continue;
		cuantos++;
		if (!hallado)
			hallado = lista[i];
	}
	int rc = -1;
	if (cuantos > 1) {
		snprintf(ultimo_error, sizeof(ultimo_error),
		         "hay %d aparatos %04x:%04x conectados; deja solo el que vas a tocar", cuantos, p->vid, p->pid);
		for (ssize_t i = 0; i < n; i++) {
			struct libusb_device_descriptor d;
			if (libusb_get_device_descriptor(lista[i], &d) == 0 && d.idVendor == p->vid && d.idProduct == p->pid)
				fprintf(stderr, "  [usb] bus %d dirección %d\n", libusb_get_bus_number(lista[i]),
				        libusb_get_device_address(lista[i]));
		}
		rc = -2;
	} else if (cuantos == 1) {
		int r = libusb_open(hallado, &p->h);
		if (r)
			error("libusb_open", r);
		else
			rc = 0;
	}
	libusb_free_device_list(lista, 1);
	return rc;
}

/* Tras cerrar el puerto el teléfono se reinicia y vuelve a enumerarse unos
 * segundos después (connection_restart en DB2000/DB2010). La primera apertura
 * de un proceso falla al instante si no hay aparato; las siguientes esperan. */
static int ya_se_abrio;

/* Mientras sp_open espera la reaparición del vid:pid pedido, imprime qué hay en
 * el bus que SE podría ser (vid 0x0fce Sony Ericsson Mobile Communications,
 * vid 0x0bdb Ericsson Business Mobile Networks — este último lo medimos en el
 * W580 roto el 2026-10-17 como 0bdb:100e durante un reset USB antes de
 * terminar de enumerar). Diagnóstico puro: no intenta abrir nada que no sea
 * el vid:pid pedido; el bootrom tras un reset puede re-enumerar con otra
 * identidad y queremos VERLO antes de reintentar a ciegas. */
static void listar_sonyericsson_bus(libusb_context *ctx)
{
	libusb_device **lista = NULL;
	ssize_t n = libusb_get_device_list(ctx, &lista);
	if (n < 0)
		return;
	int cuantos = 0;
	for (ssize_t i = 0; i < n; i++) {
		struct libusb_device_descriptor d;
		if (libusb_get_device_descriptor(lista[i], &d) != 0)
			continue;
		if (d.idVendor != 0x0fce && d.idVendor != 0x0bdb)
			continue;
		cuantos++;
		fprintf(stderr, "    [bus] %04x:%04x bus %d dirección %d (clase %02x)\n",
		        d.idVendor, d.idProduct, libusb_get_bus_number(lista[i]),
		        libusb_get_device_address(lista[i]), d.bDeviceClass);
	}
	if (cuantos == 0)
		fprintf(stderr, "    [bus] no hay ningún Sony Ericsson (0fce:* ni 0bdb:*) en el bus\n");
	libusb_free_device_list(lista, 1);
}

/* Intenta configurar + encontrar endpoints + reclamar sobre un p->h YA ABIERTO.
 * Devuelve SP_OK, o SP_ERR_FAIL con *transitorio=1 si el fallo es "este handle
 * ya no sirve, pero puede que el aparato vuelva a aparecer" (cualquier error de
 * libusb en configurar/describir/reclamar), o *transitorio=0 si es un problema
 * estructural del propio aparato (sin endpoints bulk IN/OUT) que repetir no
 * arregla. No cierra p->h: eso lo decide quien llama. */
static enum sp_return configurar_y_reclamar(struct sp_port *p, int *transitorio)
{
	*transitorio = 1;
	int r;

	/* macOS deja SIN configurar un aparato de clase 255 al que ningún
	 * driver reclama: hay que elegir la configuración nosotros. */
	int conf = 0;
	if (libusb_get_configuration(p->h, &conf) == 0 && conf == 0) {
		r = libusb_set_configuration(p->h, 1);
		if (r) {
			error("set_configuration", r);
			return SP_ERR_FAIL;
		}
	}
	struct libusb_config_descriptor *cfg;
	r = libusb_get_config_descriptor(libusb_get_device(p->h), 0, &cfg);
	if (r) {
		error("config descriptor", r);
		return SP_ERR_FAIL;
	}
	/* La primera interfaz con un bulk IN y un bulk OUT. */
	for (int i = 0; i < cfg->bNumInterfaces && p->iface < 0; i++) {
		const struct libusb_interface_descriptor *d = &cfg->interface[i].altsetting[0];
		uint8_t in = 0, out = 0;
		for (int e = 0; e < d->bNumEndpoints; e++) {
			const struct libusb_endpoint_descriptor *ep = &d->endpoint[e];
			if ((ep->bmAttributes & 3) != LIBUSB_TRANSFER_TYPE_BULK)
				continue;
			if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN)
				in = in ? in : ep->bEndpointAddress;
			else
				out = out ? out : ep->bEndpointAddress;
		}
		if (in && out) {
			p->iface = d->bInterfaceNumber;
			p->ep_in = in;
			p->ep_out = out;
		}
	}
	libusb_free_config_descriptor(cfg);
	if (p->iface < 0) {
		snprintf(ultimo_error, sizeof(ultimo_error), "sin endpoints bulk IN/OUT");
		*transitorio = 0; /* esto no es una carrera: este aparato no sirve, no reintentar */
		return SP_ERR_FAIL;
	}

	p->mps_in = libusb_get_max_packet_size(libusb_get_device(p->h), p->ep_in);
	if (p->mps_in < 64 || p->mps_in > RX_CAP)
		p->mps_in = 64;

	/* Sin libusb_set_auto_detach_kernel_driver: en macOS no hay driver de núcleo
	 * que quitar, y con la bandera puesta y privilegios de root libusb hace un
	 * reset USB del teléfono al reclamar y otro al soltar. El identify real de
	 * 2026-10-16 no tenía driver enlazado, así que sin ella es el mismo camino.
	 *
	 * Eso vale para una entrada «fría» a modo C (el aparato llevaba rato quieto
	 * antes de que alguien lo reclamara). Reclamar justo tras una REAPARICIÓN
	 * (ya_se_abrio arriba, p. ej. la reconexión automática de connection_restart
	 * tras el prologue de jdflasher) es distinto: medido contra el W580 roto
	 * 2026-10-18, el dispositivo SÍ se encontró (abrir_unico ya tenía p->h) pero
	 * claim_interface falló, dos veces, con DOS errores diferentes:
	 *   - LIBUSB_ERROR_ACCESS: macOS aún estaba terminando de asentar el
	 *     dispositivo recién llegado (su propio intento de emparejar driver,
	 *     aunque ninguno lo reclame al final).
	 *   - LIBUSB_ERROR_NO_DEVICE: el dispositivo YA SE HABÍA IDO OTRA VEZ para
	 *     cuando llegamos a reclamar. No es un reinicio limpio de una sola vez:
	 *     0fce:adde parpadea (aparece y desaparece) durante la transición antes
	 *     de que el teléfono siga hacia su arranque normal.
	 * Reintento corto (hasta 2 s) sobre el MISMO handle para el caso ACCESS (el
	 * aparato sigue ahí, solo hace falta esperar); para NO_DEVICE no tiene
	 * caso reintentar sobre un handle muerto — se marca transitorio y quien
	 * llama descarta el handle y vuelve a escanear desde cero. */
	r = libusb_claim_interface(p->h, p->iface);
	for (int intento = 0; r == LIBUSB_ERROR_ACCESS && intento < 20; intento++) {
		dormir_ms(100);
		r = libusb_claim_interface(p->h, p->iface);
	}
	if (r) {
		error("claim_interface", r);
		return SP_ERR_FAIL;
	}
	fprintf(stderr, "[usb] %04x:%04x interfaz %d, IN 0x%02x (paquete %d), OUT 0x%02x\n", p->vid,
	        p->pid, p->iface, p->ep_in, p->mps_in, p->ep_out);
	return SP_OK;
}

enum sp_return sp_open(struct sp_port *p, enum sp_mode flags)
{
	(void)flags;
	int r = libusb_init(&p->ctx);
	if (r) {
		error("libusb_init", r);
		return SP_ERR_FAIL;
	}

	/* Presupuesto para una REAPARICIÓN (ya_se_abrio=1): por defecto 60 s (240
	 * intentos a 250 ms). Antes eran 15 s (60 intentos), que resultaron escasos
	 * contra el W580 roto el 2026-10-18 tras el `R` del prologue de jdflasher:
	 * el bootrom se cae del bus Mac y, cuando vuelve, tarda bastante — a veces
	 * nunca con la misma identidad 0fce:adde (ver listar_sonyericsson_bus).
	 * SEFTOOL_REOPEN_SECS=N permite subir o bajar el límite sin recompilar
	 * (p. ej. SEFTOOL_REOPEN_SECS=120 para dejarlo esperar dos minutos). */
	int max_intentos = 60 * 4; /* 60 s por defecto */
	const char *lim_s = getenv("SEFTOOL_REOPEN_SECS");
	if (lim_s && *lim_s) {
		int lim = atoi(lim_s);
		if (lim >= 1 && lim <= 600)
			max_intentos = lim * 4;
	}

	for (int intento = 0;; intento++) {
		int ab = abrir_unico(p);
		if (ab == -2)
			return SP_ERR_FAIL; /* el mensaje ya lo dejó abrir_unico: hay 2+ aparatos, no es carrera */

		if (ab == 0) {
			int transitorio = 1;
			if (configurar_y_reclamar(p, &transitorio) == SP_OK) {
				ya_se_abrio = 1;
				return SP_OK;
			}
			if (!transitorio)
				return SP_ERR_FAIL; /* aparato sin lo que hace falta: repetir no cambia nada */

			/* Se encontró y se abrió, pero algo entre configurar y reclamar falló
			 * de forma transitoria (ver comentario en configurar_y_reclamar). Este
			 * handle ya no sirve: se descarta y se reintenta desde el escaneo, con
			 * el mismo presupuesto que "aún no ha aparecido". */
			if (p->iface >= 0)
				libusb_release_interface(p->h, p->iface); /* no-op si no llegó a reclamarse */
			libusb_close(p->h);
			p->h = NULL;
			p->iface = -1;
			p->ep_in = p->ep_out = 0;
			p->mps_in = 0;
		}

		if (!ya_se_abrio || intento >= max_intentos) {
			snprintf(ultimo_error, sizeof(ultimo_error),
			         "no encuentro %04x:%04x (¿teléfono en modo servicio?)", p->vid, p->pid);
			return SP_ERR_FAIL;
		}
		/* Cada 2 s (8 intentos de 250 ms) deja ver qué hay en el bus que podría
		 * ser el teléfono (ver listar_sonyericsson_bus). Si el aparato re-enumera
		 * con otra identidad USB tras el reset del prologue, lo veremos aquí. */
		if (intento > 0 && intento % 8 == 0)
			fprintf(stderr, "  [sp_open] esperando %04x:%04x (%d/%d s)\n",
			        p->vid, p->pid, intento / 4, max_intentos / 4);
		if (intento > 0 && intento % 8 == 0)
			listar_sonyericsson_bus(p->ctx);
		dormir_ms(250);
	}
}

enum sp_return sp_close(struct sp_port *p)
{
	if (p->h) {
		if (p->iface >= 0)
			libusb_release_interface(p->h, p->iface);
		libusb_close(p->h);
		p->h = NULL;
	}
	if (p->ctx) {
		libusb_exit(p->ctx);
		p->ctx = NULL;
	}
	/* una reapertura no hereda interfaz, endpoints ni bytes de la sesión anterior */
	p->iface = -1;
	p->ep_in = p->ep_out = 0;
	p->mps_in = 0;
	p->rx_pos = p->rx_len = 0;
	return SP_OK;
}

/* En USB no hay línea serie: velocidad, formato y señales no existen. */
enum sp_return sp_set_baudrate(struct sp_port *p, int b) { (void)p; (void)b; return SP_OK; }
enum sp_return sp_set_bits(struct sp_port *p, int b) { (void)p; (void)b; return SP_OK; }
enum sp_return sp_set_parity(struct sp_port *p, enum sp_parity x) { (void)p; (void)x; return SP_OK; }
enum sp_return sp_set_stopbits(struct sp_port *p, int s) { (void)p; (void)s; return SP_OK; }
enum sp_return sp_set_flowcontrol(struct sp_port *p, enum sp_flowcontrol f) { (void)p; (void)f; return SP_OK; }
enum sp_return sp_set_rts(struct sp_port *p, enum sp_rts x) { (void)p; (void)x; return SP_OK; }
enum sp_return sp_set_dtr(struct sp_port *p, enum sp_dtr x) { (void)p; (void)x; return SP_OK; }

/* sp_flush solo vacía el búfer propio. Se probó vaciar también el FIFO del
 * teléfono (leer y tirar lo pendiente) y se descartó a propósito: tras mandar
 * la 'R' que arranca el loader, loader_send_qhtry_setool y loader_send_qhldr_noact
 * llaman a sp_flush ANTES de esperar el saludo del loader, y un vaciado con
 * plazo se lo podría comer si llega pronto. El identify real contra el W580
 * (2026-10-16) funcionó sin vaciado, y tcflush solo descarta lo que ya llegó
 * en ese instante, cosa que por USB no se puede imitar sin riesgo. */
enum sp_return sp_flush(struct sp_port *p, enum sp_buffer buffers)
{
	if (buffers & SP_BUF_INPUT)
		p->rx_pos = p->rx_len = 0;
	return SP_OK;
}

/* Como libserialport: espera hasta tener `count` bytes o agotar el plazo
 * (0 = sin plazo) y devuelve cuántos llegaron. */
enum sp_return sp_blocking_read(struct sp_port *p, void *buf, size_t count, unsigned int timeout_ms)
{
	uint8_t *dst = buf;
	size_t got = 0;
	long limite = ahora_ms() + timeout_ms;

	while (got < count) {
		if (p->rx_pos < p->rx_len) {
			size_t n = p->rx_len - p->rx_pos;
			if (n > count - got)
				n = count - got;
			memcpy(dst + got, p->rx + p->rx_pos, n);
			p->rx_pos += n;
			got += n;
			continue;
		}
		unsigned int espera = 0;
		if (timeout_ms) {
			long resta = limite - ahora_ms();
			if (resta <= 0)
				break;
			espera = (unsigned int)resta;
		}
		int n = 0;
		/* Una lectura del tamaño de UN paquete termina con el primer
		 * paquete, lleno o corto. Con un búfer grande, un mensaje de
		 * 512*k bytes (sin paquete corto que lo cierre) no terminaba
		 * hasta agotar el plazo: 10 s por lectura con TIMEOUT=100 ms
		 * mientras el teléfono espera nuestro ack. Es lo que hace el
		 * driver usb-serial de Linux. */
		int r = libusb_bulk_transfer(p->h, p->ep_in, p->rx, p->mps_in, &n, espera);
		if (r == LIBUSB_ERROR_PIPE) {
			/* STALL del endpoint IN: el teléfono rechaza algo. En jdflasher
			 * CID52 tras el header del R2A005 esto pasa cuando el bootrom crudo
			 * no aceptó lo que mandamos. Hacemos clear_halt y reintentamos UNA
			 * vez: si el bootrom tenía algo que decir (p.ej. un código de error
			 * de 3 bytes empezando por 'F'), lo leeremos tras el clear. */
			fprintf(stderr, "[sp_usb] bulk IN STALL → clear_halt + reintento\n");
			libusb_clear_halt(p->h, p->ep_in);
			r = libusb_bulk_transfer(p->h, p->ep_in, p->rx, p->mps_in, &n, espera);
			if (r == 0 && n > 0)
				fprintf(stderr, "[sp_usb] tras clear_halt recibidos %d bytes\n", n);
		}
		if (r && r != LIBUSB_ERROR_TIMEOUT) {
			error("bulk IN", r);
			return got ? (enum sp_return)got : SP_ERR_FAIL;
		}
		if (p->traza && n > 0)
			volcar("<-", p->rx, n);
		p->rx_pos = 0;
		p->rx_len = n;
		if (r == LIBUSB_ERROR_TIMEOUT && n == 0)
			break;
	}
	return (enum sp_return)got;
}

enum sp_return sp_blocking_write(struct sp_port *p, const void *buf, size_t count, unsigned int timeout_ms)
{
	const uint8_t *src = buf;
	size_t sent = 0;
	if (p->traza)
		volcar("->", src, count);
	/* libserialport: 0 = sin plazo. Aquí un teléfono atascado dejaba el
	 * proceso colgado para siempre con el aparato a medio recibir: se
	 * acota a 60 s por trozo. */
	unsigned int plazo = timeout_ms ? timeout_ms : 60000;
	while (sent < count) {
		int n = 0;
		int r = libusb_bulk_transfer(p->h, p->ep_out, (uint8_t *)src + sent, (int)(count - sent), &n, plazo);
		sent += n;
		if (r == LIBUSB_ERROR_PIPE) {
			/* STALL del endpoint OUT: en jdflasher CID52 el bootrom crudo
			 * marca STALL cuando se le satura durante QA/QD. Hacemos
			 * clear_halt y reintentamos UNA vez: si el bootrom estaba
			 * procesando y ya aceptó más, continuamos; si vuelve a
			 * STALL'ear, la pista de que lo que mandamos no le gusta en
			 * absoluto. */
			fprintf(stderr, "[sp_usb] bulk OUT STALL (sent %zu/%zu) → clear_halt + reintento\n", sent, count);
			libusb_clear_halt(p->h, p->ep_out);
			r = libusb_bulk_transfer(p->h, p->ep_out, (uint8_t *)src + sent, (int)(count - sent), &n, plazo);
			sent += n;
			if (r == 0 && n > 0)
				fprintf(stderr, "[sp_usb] tras clear_halt OUT, aceptados %d bytes más\n", n);
		}
		if (r == 0 && n <= 0) {
			/* sin progreso y sin error: reintentar sin fin sería un bucle de
			 * transacciones USB */
			snprintf(ultimo_error, sizeof(ultimo_error), "bulk OUT sin progreso (%zu de %zu bytes)", sent, count);
			return SP_ERR_FAIL;
		}
		if (r) {
			/* Un envío parcial NUNCA se da por bueno: el que llama solo mira
			 * si el retorno es negativo y seguiría con el flujo roto. */
			error("bulk OUT", r);
			return SP_ERR_FAIL;
		}
	}
	return (enum sp_return)sent;
}

char *sp_last_error_message(void) { return ultimo_error; }
void sp_free_error_message(char *message) { (void)message; }
