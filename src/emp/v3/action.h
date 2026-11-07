#ifndef emp_v3_action_h
#define emp_v3_action_h

int action_unlock_usercode(struct sp_port *port, struct phone_info *phone);
int action_identify(struct sp_port *port, struct phone_info *phone);
int action_flash_fw(struct sp_port *port, struct phone_info *phone, const char *main_fw, const char *fs_fw,
                    const char *cda);
int action_read_flash(struct sp_port *port, struct phone_info *phone, uint32_t addr, uint32_t size);
/* seftool-senew: la compuerta de `read-flash --anycid` en un DB2020. 0 = seguir; -1 = rechazada (ya avisó
 * por stderr; la última línea es «read-flash --anycid: REFUSED: …»). Pública para probarla sin puerto. */
int action_read_flash_gate(const struct phone_info *phone, uint32_t addr, uint64_t size);
int action_backup_gdfs(struct sp_port *port, struct phone_info *phone);
int action_restore_gdfs(struct sp_port *port, struct phone_info *phone, const char *inputfname);
int action_exec_scripts(struct sp_port *port, struct phone_info *phone, int nfiles, const char **filenames);
int action_convert(const char *cnv_mode, const char *cnv_filename, uint32_t mem_addr);
int action_upload_to_fs(struct sp_port *port, struct phone_info *phone, const char **src_files, int src_count,
                        const char *dst_dir);
int action_download_from_fs(struct sp_port *port, struct phone_info *phone, const char *src_dir, const char *dest_dir);
int action_upload_anycid(struct sp_port *port, struct phone_info *phone);

/* seftool-senew: DB2020 flash through the anycid bflash path.
 * Precondition: `upload-anycid` has been run on this device, the user has
 * opened Games -> "executer" on the phone, and the phone is now reconnected
 * in service mode. The MIDlet has primed the AMS to accept
 * DB2020_PRELOADER_FOR_SETOOL2; from there bflash_ldr_db2020(anycid=1)
 * uploads it + DB2020_LOADER_FOR_SETOOL2 ("Security disabled =)") and
 * flash_babe_fw can write unsigned firmware. Only DB2020 is supported here;
 * other chips return an error. */
int action_flash_anycid(struct sp_port *port, struct phone_info *phone,
                        const char *main_fw, const char *fs_fw);

/* seftool-senew: DB2020 CID52 RED flash via the jdflasher bflash path, SIN
 * executer. Precondition: identify has run, GDFS has already been backed up
 * at least once (sin él, un error vuelve el teléfono inútil), --cid 52 and
 * --imei <S> en línea de órdenes, SEFTOOL_JDFLASHER_CONFIRM=1 y
 * SEFTOOL_JDFLASHER_WRITE=1. Sin la segunda, es DRY-RUN: carga el
 * flashloader (demostrando que el prologue engancha) y vuelve sin tocar la
 * NOR. Solo DB2020 CID52; otros chips/CID se rechazan. */
int action_flash_jdflasher(struct sp_port *port, struct phone_info *phone,
                           const char *main_fw, const char *fs_fw);

/* ACT_JDFLASHER_PRIME: sesión 1 de la vía jdflasher. Carga 2020_prologue + "R" y
 * sale sin loader_shutdown (el aparato se reinicia tras R, saliendo del modo flash).
 * Después el humano reconecta en modo C y lanza una acción --jdflasher --skip-prologue
 * (read-flash, read-gdfs, write-script, flash-jdflasher) que asume el aparato parcheado. */
int action_jdflasher_prime(struct sp_port *port, struct phone_info *phone);

#endif // emp_v3_action_h