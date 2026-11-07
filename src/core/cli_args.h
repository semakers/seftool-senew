#ifndef cli_args_h
#define cli_args_h

typedef struct {
	const char *port_name;
	int baudrate;
	const char *action;
	const char *unlock_target;
	const char *gdfs_filename;
	const char *flash_mainfw;
	const char *flash_fsfw;
	const char *flash_cda;
	const char *cnv_filename;
	const char *cnv_mode;
	const char *dest_path;
	const char *src_path;
	const char **src_list;
	int upload_count;
	const char *flash_avr_fw;
	const char *flash_arm_fw;
	const char **script_filenames;
	int script_count;
	uint32_t dump_addr;
	uint32_t dump_size;
	uint32_t mem_addr;
	int anycid;
	int jdflasher; /* --jdflasher: DB2020 CID52 RED vía qhtry(2020_prologue)+flashloader R2A005. Incompatible con --anycid y con --break-rsa. */
	int skip_prologue; /* --skip-prologue: con --jdflasher, no cargar 2020_prologue (asumir que `jdflasher-prime` ya corrió en sesión anterior) */
	int restore_boot; /* --restore-boot: permitir que read-flash & co escriban el .rest */
	uint32_t gdfs_write_max; /* --gdfs-max N */
	int gdfs_skip_big; /* --skip-big */
	int expect_cid; /* --cid N: el CID ACTIVO que imprimió identify (flash-anycid) */
	const char *expect_imei; /* --imei S: el IMEI que tiene que decir el aparato (identidad entre sesiones) */
	int break_rsa;
	int save_as_babe;
	const char *eeprom_file;
	const char *avr_device_name;
} cli_args;

#endif // cli_args
