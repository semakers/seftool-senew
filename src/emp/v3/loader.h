#ifndef loader_h
#define loader_h

enum ldr_type_e {
	LDR_CHIPSELECT,
	LDR_PRODUCT_ID,
	LDR_CERT,
	LDR_FLASH,
	LDR_MEM_PATCHER,
	LDR_UNKNOWN,
};

int loader_send_binary_noact(struct sp_port *port, const char *loader_name);
int loader_send_binary_cmd3e(struct sp_port *port, const char *loader_name);
int loader_send_binary(struct sp_port *port, struct phone_info *phone, const char *loader_name);
int loader_send_qhldr(struct sp_port *port, struct phone_info *phone, const char *loader_name);
int loader_send_unsigned_bin(struct sp_port *port, const char *loader_name, uint32_t ram_addr);
int loader_activate_payload(struct sp_port *port, struct phone_info *phone);
/* --imei: 0 si no se pidió o el IMEI del OTP es el esperado; -1 (avisando por stderr, última línea
 * «imei-check: REFUSED: …», y dejando phone->imei_refused = 1 para que nadie añada después pistas que
 * engañen) si es otro o si el aparato no lo ha dicho. Pública para probarla sin puerto. */
int loader_check_expected_imei(struct phone_info *phone);
/* Imprime «[QHTRY] Run executer first» salvo que el fallo haya sido la negativa de --imei. */
void loader_hint_run_executer(const struct phone_info *phone);
int loader_send_qhtry_jdflasher(struct sp_port *port, struct phone_info *phone, const char *loader_name);
int loader_send_prologue_jdflasher(struct sp_port *port, struct phone_info *phone, const char *loader_name);
/* Carga el flashloader DB2020 R2A005 por el formato CRUDO (QH00/QA00/QD00 ASCII + handshake
 * EsB/EhM/EaT/EbS/EdQ + byteleft + 'R' + hello 0x89 + activate_payload), que es el que acepta el
 * bootrom tras `2020_prologue` + run en CID52. NO empaqueta en cmd 0x3C: eso es lo que hace
 * loader_send_binary y rompe aquí (ver docs/CID52_JDFLASHER.md). Verifica el SHA-1 del fichero
 * (DB2020_FLASHLOADER_R2A005_SHA1_HEX) antes de mandar nada al teléfono. */
int loader_send_qhldr_jdflasher(struct sp_port *port, struct phone_info *phone, const char *loader_name);
int loader_send_qhtry_setool(struct sp_port *port, struct phone_info *phone, const char *loader_name);

int loader_get_erom_info(struct sp_port *port, struct phone_info *phone);
int loader_get_flash_info(struct sp_port *port, struct phone_info *phone);
int loader_get_otp_info(struct sp_port *port, struct phone_info *phone);
int loader_activate_gdfs(struct sp_port *port);

int loader_enter_flashmode(struct sp_port *port, struct phone_info *phone);
int loader_send_oflash_ldr(struct sp_port *port, struct phone_info *phone);
int loader_send_ofs_ldr(struct sp_port *port, struct phone_info *phone);
int loader_send_bflash_ldr(struct sp_port *port, struct phone_info *phone);
/* Solo la carga del loader de setool2 en un DB2020 (rama anycid), sin reparar el arranque. */
int loader_send_setool2_db2020(struct sp_port *port, struct phone_info *phone);
int loader_send_bfs_ldr(struct sp_port *port, struct phone_info *phone);
int bflash_repair_boot(struct sp_port *port, struct phone_info *phone);

int loader_shutdown(struct sp_port *port);

int loader_send_packet_pnx(struct sp_port *port, uint8_t block, uint8_t msb, uint8_t lsb, uint8_t *resp,
                           size_t resp_max);

#endif // loader_h
