/*
 * Sustituto mínimo de libserialport para macOS.
 *
 * En modo servicio (tecla C) el DB2020 se anuncia como «SEMC USB Flash»
 * (0fce:adde, clase 255). Linux lo vuelve /dev/ttyUSBx con usbserial
 * genérico; macOS no tiene driver y no crea ningún /dev/cu.*. Esta capa
 * implementa solo las sp_* que usa seftool, sobre dos endpoints bulk con
 * libusb. Velocidad, paridad, RTS y DTR no existen en USB: son no-op.
 *
 * Nombre de puerto: "usb" (0fce:adde) o "usb:VVVV:PPPP" en hexadecimal.
 */
#ifndef SENEW_MAC_LIBSERIALPORT_H
#define SENEW_MAC_LIBSERIALPORT_H

#include <stddef.h>

enum sp_return { SP_OK = 0, SP_ERR_ARG = -1, SP_ERR_FAIL = -2, SP_ERR_MEM = -3, SP_ERR_SUPP = -4 };
enum sp_mode { SP_MODE_READ = 1, SP_MODE_WRITE = 2, SP_MODE_READ_WRITE = 3 };
enum sp_buffer { SP_BUF_INPUT = 1, SP_BUF_OUTPUT = 2, SP_BUF_BOTH = 3 };
enum sp_parity { SP_PARITY_INVALID = -1, SP_PARITY_NONE = 0 };
enum sp_rts { SP_RTS_INVALID = -1, SP_RTS_OFF = 0, SP_RTS_ON = 1 };
enum sp_dtr { SP_DTR_INVALID = -1, SP_DTR_OFF = 0, SP_DTR_ON = 1 };
enum sp_flowcontrol { SP_FLOWCONTROL_NONE = 0 };

struct sp_port;

enum sp_return sp_get_port_by_name(const char *portname, struct sp_port **port_ptr);
void sp_free_port(struct sp_port *port);
enum sp_return sp_open(struct sp_port *port, enum sp_mode flags);
enum sp_return sp_close(struct sp_port *port);
enum sp_return sp_set_baudrate(struct sp_port *port, int baudrate);
enum sp_return sp_set_bits(struct sp_port *port, int bits);
enum sp_return sp_set_parity(struct sp_port *port, enum sp_parity parity);
enum sp_return sp_set_stopbits(struct sp_port *port, int stopbits);
enum sp_return sp_set_flowcontrol(struct sp_port *port, enum sp_flowcontrol flowcontrol);
enum sp_return sp_set_rts(struct sp_port *port, enum sp_rts rts);
enum sp_return sp_set_dtr(struct sp_port *port, enum sp_dtr dtr);
enum sp_return sp_flush(struct sp_port *port, enum sp_buffer buffers);
enum sp_return sp_blocking_read(struct sp_port *port, void *buf, size_t count, unsigned int timeout_ms);
enum sp_return sp_blocking_write(struct sp_port *port, const void *buf, size_t count, unsigned int timeout_ms);
char *sp_last_error_message(void);
void sp_free_error_message(char *message);

#endif
