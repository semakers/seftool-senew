#ifndef flash_h
#define flash_h

#include <stdint.h>

#include "vkp.h"

#define BLOCK_SIZE 0x10000
#define MAX_BLOCKS 480

#define FLASH_VKP_ERR -1
#define FLASH_VKP_OK 0
#define FLASH_VKP_SKIP 1

// Unified choice enum
typedef enum { CHOICE_UNINSTALL = 0, CHOICE_SKIP = 1, CHOICE_ABORT = 2, CHOICE_CONTINUE = 3 } user_choice_t;

int flash_detect_fw_version(struct sp_port *port, struct phone_info *phone);

uint8_t *flash_read_raw(struct sp_port *port, uint32_t addr, size_t size);
int flash_read(struct sp_port *port, struct phone_info *phone, uint32_t addr, size_t size);

/* Ventana de la NOR que `read-flash --anycid` puede volcar en un DB2020: los 64 MiB
 * enteros, incluido el arranque (0x44000000..0x4413FFFF) que flash-anycid NO escribe. */
#define NOR_READ_MIN 0x44000000u
#define NOR_READ_MAX 0x48000000u

/* 0 = [addr, addr+size) cabe en la NOR; si no, -1 y *why (si no es NULL) dice por qué.
 * Aritmética de 64 bits: ni un start+size que dé la vuelta pasa por bueno. */
int flash_read_window_check(uint32_t addr, uint64_t size, const char **why);

/* El plan de lectura: flash_read pide la ventana de BLOCK_SIZE en BLOCK_SIZE (el último
 * trozo es el resto). flash_read_plan_at devuelve -1 si `index` queda fuera del plan. */
size_t flash_read_plan_count(uint64_t size);
int flash_read_plan_at(uint32_t addr, uint64_t size, size_t index, uint32_t *chunk_addr, size_t *chunk_len);

/* Volcado atómico (el mismo criterio que read-gdfs): se escribe en <nombre>.INCOMPLETO y solo
 * se renombra al definitivo cuando está entero y comprobado. Una lectura que falla, un corte
 * de luz o un Ctrl-C no destruyen el volcado bueno que hubiera ni dejan un .bin truncado con
 * aspecto de bueno. El volcado bueno anterior pasa a <nombre>.anterior (UNA sola generación: el siguiente
 * volcado bueno sustituye a ese .anterior). */
struct flash_dump;
struct flash_dump *flash_dump_open(const char *path); /* NULL si no se puede abrir */
int flash_dump_write(struct flash_dump *d, const uint8_t *data, size_t len);
/* Cierra, comprueba que se escribieron `expected` bytes (y que el disco los tiene) y publica.
 * Libera `d` pase lo que pase. sha1_hex (41 bytes) recibe el SHA-1 de lo escrito. */
int flash_dump_commit(struct flash_dump *d, size_t expected, char sha1_hex[41]);
/* Abandona: deja <nombre>.INCOMPLETO tal cual, no toca el definitivo, avisa. Libera `d`. */
void flash_dump_abort(struct flash_dump *d, const char *why);

int flash_babe(struct sp_port *port, uint8_t *addr, size_t size, int flashfull);
int flash_babe_fw(struct sp_port *port, const char *filename, int fw_check);
/* Ventana de la NOR que flash-anycid puede escribir en un DB2020: del inicio del
 * main (0x44140000, donde empieza el main del W580) a los 64 MiB. Por debajo
 * están el arranque, el CID y las unidades de seguridad. */
#define NOR_WRITE_MIN 0x44140000u
#define NOR_WRITE_MAX 0x48000000u

/* expect_cid: el CID activo del aparato (0 = no dado); force: salta las guardas
 * de dirección, plataforma y CID (no la integridad del archivo). */
int flash_babe_precheck(const char *filename, int expect_cid, int force);

int flash_raw(struct sp_port *port, const char *filename, uint32_t raw_addr);
uint8_t *flash_convert_raw_to_babe(uint8_t *raw, size_t size, uint32_t raw_addr, size_t *babe_size_out);

int flash_restore_boot_area(struct sp_port *port, struct phone_info *phone);

int flash_vkp(struct sp_port *port, const char *filename, vkp_patch_t *patch, int remove_flag, size_t flashblocksize);

int flash_cnv_raw_to_babe_file(const char *raw_filename, const char *babe_filename, uint32_t raw_addr);
int flash_cnv_babe_to_raw_file(const char *babe_filename, const char *raw_filename);

#endif // flash_h