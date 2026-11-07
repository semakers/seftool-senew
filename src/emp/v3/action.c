#include <libserialport.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define access _access
#else
#include <unistd.h>
#endif

#include <core/common.h>
#include <core/connection.h>
#include <core/payload.h>
#include <senew/senew.h>
#include <utils/zipper.h>
#include <core/serial.h>
#include <emp/common/cmd.h>
#include <utils/vkp.h>

#include "babe.h"
#include "csloader.h"
#include "flash.h"
#include "gdfs.h"
#include "loader.h"

int unlock_usercode_db2020_pnx5230(struct sp_port *port, struct phone_info *phone)
{
	if (loader_send_ofs_ldr(port, phone) != 0)
		return -1;

	if (gdfs_unlock_usercode(port) != 0)
		return -1;

	if (gdfs_terminate_access(port) != 0)
		return -1;

	return 0;
}

int unlock_usercode_db2000_db2010(struct sp_port *port, struct phone_info *phone)
{
	if (loader_enter_flashmode(port, phone) != 0)
		return -1;

	if (loader_activate_gdfs(port) != 0)
		return -1;

	struct gdfs_data_t gdfs = {0};
	gdfs_get_userlock(port, &gdfs);
	printf("\nUser code: %s\n\n", gdfs.user_lock);

	return 0;
}

int action_unlock_usercode(struct sp_port *port, struct phone_info *phone)
{
	switch (phone->chip_id) {
	case DB2000:
	case DB2010_1:
	case DB2010_2:
		return unlock_usercode_db2000_db2010(port, phone);

	case DB2020:
	case PNX5230:
		return unlock_usercode_db2020_pnx5230(port, phone);

	default:
		return -1;
	}
}

int dump_sec_units_pnx(struct sp_port *port, const char *backup_name)
{
	FILE *f = fopen(backup_name, "a");
	if (!f) {
		fprintf(stderr, "Cannot create %s\n", backup_name);
		return -1;
	}

	uint8_t resp[0x800];
	int len;

	/* list of blocks to dump (block, msb, lsb, skip_first) */
	struct {
		uint8_t block;
		uint8_t msb;
		uint8_t lsb;
	} blocks[] = {
	        {0x00, 0x00, 0x06}, /* GD_COPS_Dynamic1Variable */
	        {0x00, 0x00, 0x0E}, /* GD_COPS_Dynamic2Variable */
	        {0x00, 0x00, 0x13}, /* GD_COPS_StaticVariable */
	        {0x00, 0x00, 0x18}, /* GD_COPS_ProtectedCustomerSettings */
	        {0x00, 0x00, 0xAA}, /* GD_Protected_PlatformSettings */
	        {0x01, 0x08, 0x51} /* block 0x01 unit 0x0851 */
	};

	for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i) {
		len = loader_send_packet_pnx(port, blocks[i].block, blocks[i].msb, blocks[i].lsb, resp, sizeof(resp));
		if (len < 0) {
			fclose(f);
			return -1;
		}

		fprintf(f, "gdfswrite:%04X%02X%02X", blocks[i].block, blocks[i].msb, blocks[i].lsb);

		for (int j = 0; j < len; ++j)
			fprintf(f, "%02X", resp[j]);

		fprintf(f, "\n");
	}

	fclose(f);
	printf("SECURITY UNITS BACKUP CREATED. %s\n", backup_name);
	return 0;
}

int action_identify_pnx(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs)
{
	printf("Phone Info (from GDFS):\n");

	// Phone name
	uint8_t resp[0x800];
	int len = loader_send_packet_pnx(port, 0x02, 0x0D, 0xBB, resp, sizeof(resp));
	if (len < 0)
		return -1;
	gdfs_copy_ucs2(gdfs->phone_name, sizeof(gdfs->phone_name), resp, (size_t)len);
	printf("Model: %s\n", gdfs->phone_name);
	senew_print_identify_hint(gdfs->phone_name, phone->erom_cid);

	// Brand
	len = loader_send_packet_pnx(port, 0x02, 0x0D, 0xE5, resp, sizeof(resp));
	if (len < 0)
		return -1;
	gdfs_copy_text(gdfs->brand, sizeof(gdfs->brand), resp, (size_t)len);
	printf("Brand: %s\n", gdfs->brand);

	// CXC article
	len = loader_send_packet_pnx(port, 0x02, 0x0E, 0x15, resp, sizeof(resp));
	if (len < 0)
		return -1;
	gdfs_copy_text(gdfs->cxc_article, sizeof(gdfs->cxc_article), resp, (size_t)len);
	printf("MAPP CXC article: %s\n", gdfs->cxc_article);
	parse_cxc_article_to_anycid_pkg(phone, gdfs->cxc_article);
	parse_cxc_article_to_rest_name(phone, gdfs->cxc_article);

	// CXC version
	len = loader_send_packet_pnx(port, 0x02, 0x0E, 0x16, resp, sizeof(resp));
	if (len < 0)
		return -1;
	gdfs_copy_text(gdfs->cxc_version, sizeof(gdfs->cxc_version), resp, (size_t)len);
	printf("MAPP CXC version: %s\n", gdfs->cxc_version);

	// Language package
	len = loader_send_packet_pnx(port, 0x02, 0x0D, 0xE7, resp, sizeof(resp));
	if (len < 0)
		return -1;
	gdfs_copy_text(gdfs->langpack, sizeof(gdfs->langpack), resp, (size_t)len);
	printf("Language package: %s\n", gdfs->langpack);

	// CDA article
	len = loader_send_packet_pnx(port, 0x02, 0x0D, 0xE8, resp, sizeof(resp));
	if (len < 0)
		return -1;
	gdfs_copy_text(gdfs->cda_article, sizeof(gdfs->cda_article), resp, (size_t)len);
	printf("CDA article: %s\n", gdfs->cda_article);

	// CDA revision
	len = loader_send_packet_pnx(port, 0x02, 0x0D, 0xE9, resp, sizeof(resp));
	if (len < 0)
		return -1;
	gdfs_copy_text(gdfs->cda_revision, sizeof(gdfs->cda_revision), resp, (size_t)len);
	printf("CDA revision: %s\n", gdfs->cda_revision);

	// Default article
	len = loader_send_packet_pnx(port, 0x02, 0x0D, 0xEA, resp, sizeof(resp));
	if (len < 0)
		return -1;
	gdfs_copy_text(gdfs->default_article, sizeof(gdfs->default_article), resp, (size_t)len);
	printf("Default article: %s\n", gdfs->default_article);

	// Default version
	len = loader_send_packet_pnx(port, 0x02, 0x0D, 0xEB, resp, sizeof(resp));
	if (len < 0)
		return -1;
	gdfs_copy_text(gdfs->default_version, sizeof(gdfs->default_version), resp, (size_t)len);
	printf("Default version: %s\n", gdfs->default_version);

	// SIMLOCK (binary data)
	len = loader_send_packet_pnx(port, 0x00, 0x00, 0x06, resp, sizeof(resp));
	if (len < 0)
		return -1;
	gdfs_parse_simlockdata(gdfs, resp);
	printf("%s\n", gdfs->locked ? "LOCKED" : "SIMLOCKS NOT DETECTED");
	printf("Provider: %s-%s\n\n", gdfs->mcc, gdfs->mnc);

	char backup_path[512];
	snprintf(backup_path, sizeof(backup_path), "./backup/secunits_%s_%s.txt", gdfs->phone_name, phone->otp_imei);
	if (access(backup_path, 0) != 0) {
		dump_sec_units_pnx(port, backup_path);
	}

	if (check_restore_file(phone))
		printf("Restore firmware: ./rest/%s.rest\n", phone->rest_name);
	else
		printf("Restore firmware not found\n");

	if (check_anycid_pkg(phone))
		printf("Anycid package: ./anycid/%s.zip\n\n", phone->anycid_target);

	return 0;
}

int action_identify(struct sp_port *port, struct phone_info *phone)
{
	struct gdfs_data_t gdfs = {0};

	if (phone->chip_id == PNX5230)
		return action_identify_pnx(port, phone, &gdfs);

	if (loader_enter_flashmode(port, phone) != 0)
		return -1;

	if (loader_activate_gdfs(port) != 0)
		return -1;

	printf("\nPhone Info (from GDFS):\n");

	gdfs_get_phonename(port, phone, &gdfs);
	printf("Model: %s\n", gdfs.phone_name);
	senew_print_identify_hint(gdfs.phone_name, phone->erom_cid);

	gdfs_get_brand(port, phone, &gdfs);
	printf("Brand: %s\n", gdfs.brand);

	if (phone->chip_id > DB2010_1) {
		gdfs_get_cxc_article(port, phone, &gdfs);
		printf("MAPP CXC article: %s\n", gdfs.cxc_article);
		parse_cxc_article_to_anycid_pkg(phone, gdfs.cxc_article);
		parse_cxc_article_to_rest_name(phone, gdfs.cxc_article);

		gdfs_get_cxc_version(port, phone, &gdfs);
		printf("MAPP CXC version: %s\n", gdfs.cxc_version);
	}

	gdfs_get_language(port, phone, &gdfs);
	printf("Language Package: %s\n", gdfs.langpack);

	gdfs_get_cda_article(port, phone, &gdfs);
	printf("CDA article: %s\n", gdfs.cda_article);

	gdfs_get_cda_revision(port, phone, &gdfs);
	printf("CDA revision: %s\n", gdfs.cda_revision);

	gdfs_get_default_article(port, phone, &gdfs);
	printf("Default article: %s\n", gdfs.default_article);

	gdfs_get_default_version(port, phone, &gdfs);
	printf("Default version: %s\n", gdfs.default_version);

	gdfs_get_simlock(port, &gdfs);
	printf("%s\n", gdfs.locked ? "LOCKED" : "SIMLOCKS NOT DETECTED");
	printf("Provider: %s-%s\n\n", gdfs.mcc, gdfs.mnc);

	if (phone->chip_id != DB2020) {
		gdfs_get_userlock(port, &gdfs);
		printf("User code: %s\n\n", gdfs.user_lock);
	}

	char backup_path[512];
	snprintf(backup_path, sizeof(backup_path), "./backup/secunits_%s.txt", phone->otp_imei);
	if (access(backup_path, 0) != 0) {
		if (gdfs_dump_sec_units(port, phone, backup_path) != 0)
			fprintf(stderr, "WARNING: the security-units backup could not be completed.\n");
	}

	if (phone->chip_id > DB2010_1 && phone->erom_cid >= 49) {
		if (check_restore_file(phone))
			printf("Restore firmware: ./rest/%s.rest\n", phone->rest_name);
		else
			printf("Restore firmware not found\n");

		if (check_anycid_pkg(phone))
			printf("Anycid package: ./anycid/%s.zip\n\n", phone->anycid_target);
	}

	return 0;
}

int action_upload_anycid(struct sp_port *port, struct phone_info *phone)
{
	if (phone->chip_id == DB2000) {
		printf("No anycid exploit for DB2000 phone\n");
		return 0;
	}
	if (phone->erom_color == BROWN) {
		printf("No need to use anycid exploit for BROWN phone\n");
		return 0;
	}
	if ((phone->chip_id == DB2010_1 || phone->chip_id == DB2010_2) && phone->erom_cid <= 49) {
		printf("No need to use anycid exploit for DB2010 CID49 RED and "
		       "lower phone\n");
		return 0;
	}
	/* seftool-senew NOTE [cid53-pending]:
	 * Para DB2020 red (CID49/51/52/53) se cae por aquí y se sube el
	 * paquete anycid de plataforma (anycid/DB2020.zip) más el paquete
	 * por firmware (anycid/R8BE001_*.zip para W580). Para la primera
	 * verificación en W580 CID53 (docs/CID53_RUTA.md paso 3):
	 *   - Confirmar que phone->anycid_target quedó bien resuelto por
	 *     parse_cxc_article_to_anycid_pkg a partir del CXC leído de la
	 *     GDFS. Si viene vacío o no mapea a un .zip de ./anycid, parar.
	 *   - El servidor FSX arranca con csloader_start_fsx_server; si
	 *     devuelve 29 en CID53, es el mismo síntoma documentado por la
	 *     escena ("CSloader refused to start GDFS services, error:29")
	 *     y hay que medir, no reintentar. */

	phone->gdfs_server = 1;

	if (loader_send_ofs_ldr(port, phone) != 0)
		return -1;

	/* Los dos paquetes se comprueban ANTES de abrir el sistema de archivos
	 * del teléfono: antes, sin paquete se devolvía éxito sin subir nada, y
	 * un zip dañado se descubría a mitad de la subida. */
	char anycid_pkg[256];
	snprintf(anycid_pkg, sizeof(anycid_pkg), "./anycid/%s.zip", phone->anycid_target);
	if (!file_exists(anycid_pkg) || zip_test(anycid_pkg) != 0) {
		fprintf(stderr, "Anycid package missing or damaged: %s\n", anycid_pkg);
		return -1;
	}
	if (phone->chip_id == DB2020 || phone->chip_id == PNX5230) {
		char pkg[256];
		snprintf(pkg, sizeof(pkg), "./anycid/%s.zip", get_chipset_name(phone->chip_id));
		if (!file_exists(pkg) || zip_test(pkg) != 0) {
			fprintf(stderr, "Platform package missing or damaged: %s\n", pkg);
			return -1;
		}
	}

	if (csloader_start_fsx_server(port) != 0)
		return -1;
	if (csloader_change_directory(port, "/") != 0)
		return -1;

	{
		printf("-> Uploading package: %s\n", anycid_pkg);

		char platform_pkg[256];
		if (phone->chip_id == DB2020 || phone->chip_id == PNX5230) {
			snprintf(platform_pkg, sizeof(platform_pkg), "./anycid/%s.zip",
			         get_chipset_name(phone->chip_id));

			if (csloader_upload_zip(port, platform_pkg) != 0) {
				fprintf(stderr, "Error uploading pkg: %s\n", platform_pkg);
				csloader_shutdown_fsx_server(port);
				return -1;
			}
		}

		if (csloader_upload_zip(port, anycid_pkg) != 0) {
			fprintf(stderr, "Error uploading pkg: %s\n", anycid_pkg);
			csloader_shutdown_fsx_server(port);
			return -1;
		}

		printf("Turn on the phone with simcard installed\n"
		       "executer will be installed on Games folder\n\n");
	}

	if (csloader_shutdown_fsx_server(port) != 0)
		return -1;

	return 0;
}

/* seftool-senew: flash DB2020 por la rama anycid. Es la pareja de
 * `upload-anycid`: éste sube los paquetes al FFX; después el usuario abre
 * Juegos -> "executer" en el teléfono (el AMS carga el MIDlet, que ceba el
 * exploit); vuelve a conectar; y entonces esta acción llama a
 * `loader_send_bflash_ldr_db2020()` con anycid=1 — sube el preloader +
 * loader de setool2 ("Security disabled =)") — y a continuación
 * `flash_babe_fw` escribe un main sin firmar como lo haría W600/W810 CID49.
 *
 * Lo que NO hace esta función: nada que pueda darse por hecho sin aparato.
 *  - No sube los paquetes anycid: eso es `upload-anycid`.
 *  - No puede abrir «executer» por el usuario: cerrojo físico. Si el MIDlet
 *    no corrió, `qhtry_setool` falla con «Run executer first» y aquí se
 *    aborta ANTES de tocar la NOR.
 *  - Solo DB2020. Si llega otro chip, se niega: W600/W810 CID49 tienen su
 *    camino en `action_flash_fw` con --break-rsa, y mezclar rutas sería la
 *    receta para un flasheo cruzado por error.
 */
int action_flash_anycid(struct sp_port *port, struct phone_info *phone,
                        const char *main_fw, const char *fs_fw)
{
	if (phone->chip_id != DB2020) {
		fprintf(stderr,
		        "flash-anycid: solo DB2020 (W580). Para DB2010/DB2000 usa "
		        "`-a flash main <f> [fs <f>] [cda <f>]` con --break-rsa si "
		        "procede.\n");
		return -1;
	}
	if (phone->erom_color == BROWN) {
		fprintf(stderr,
		        "flash-anycid: este aparato es BROWN; no necesita anycid, "
		        "usa `-a flash main <f>` sin más.\n");
		return -1;
	}
	if (!main_fw && !fs_fw) {
		fprintf(stderr, "flash-anycid: se requiere al menos 'main' o 'fs'.\n");
		return -1;
	}

	/* Compuerta de senew. En un DB2020 recién conectado el CID activo todavía
	 * NO se conoce (el EROM de DB2020 no lo informa; se lee con un loader de
	 * identificación, y identify termina apagando el teléfono), así que lo da
	 * quien llama con --cid N, el «ACTIVE CID» que imprimió identify. La
	 * tabla de senew dice para qué (W580, CID) hay evidencia de que escribir
	 * es seguro; si no es «image available» (p. ej. CID53, sin verificar en
	 * un aparato real) NO se escribe, que es lo que promete el aviso «DO NOT
	 * write» de identify. SEFTOOL_SENEW_FORCE=1 salta la compuerta a
	 * sabiendas. */
	if (senew_force_requested()) {
		fprintf(stderr, "flash-anycid: SEFTOOL_SENEW_FORCE activo, se salta la compuerta de senew.\n");
	} else if (phone->expect_cid == 0) {
		fprintf(stderr,
		        "flash-anycid: falta --cid <N>, el «ACTIVE CID» que imprimió `identify` (49, 51, 52 o 53).\n"
		        "  Sin él no se puede saber si es seguro escribir. No se escribe nada.\n");
		return -1;
	} else {
		senew_coverage_t cov = senew_coverage_for(SENEW_BOARD_W580, phone->expect_cid);
		if (cov != SENEW_COV_IMAGE_AVAILABLE) {
			fprintf(stderr,
			        "flash-anycid: la tabla de senew no da cobertura de escritura para W580/DB2020 CID%d "
			        "(estado %d). No se escribe nada.\n"
			        "  Si de verdad quieres seguir: SEFTOOL_SENEW_FORCE=1\n",
			        phone->expect_cid, (int)cov);
			return -1;
		}
	}

	/* Los dos archivos se comprueban enteros ANTES de mandar nada al teléfono
	 * (tabla de bloques completa, ventana de NOR, plataforma, CID; y el SHA-1 de
	 * cada bloque si la imagen es de Sony; una imagen propia hecha con raw2babe
	 * no lleva hashes ni firma): así un fs dañado no se descubre cuando el main
	 * ya está escrito. */
	int forzar = senew_force_requested();
	if (main_fw && flash_babe_precheck(main_fw, phone->expect_cid, forzar) != 0) {
		fprintf(stderr, "flash-anycid: %s no pasa la comprobación; no se toca el teléfono.\n", main_fw);
		return -1;
	}
	if (fs_fw && flash_babe_precheck(fs_fw, phone->expect_cid, forzar) != 0) {
		fprintf(stderr, "flash-anycid: %s no pasa la comprobación; no se toca el teléfono.\n", fs_fw);
		return -1;
	}

	/* La rama bflash_ldr_db2020 con anycid=1 es la única que, en el fork
	 * tal cual, levanta la verificación de firma en DB2020. Si el MIDlet
	 * executer no corrió antes en el teléfono, qhtry_setool devolverá
	 * «Run executer first» y la función retorna -1 SIN tocar la NOR. */
	phone->anycid = 1;

	if (loader_send_bflash_ldr(port, phone) != 0) {
		if (phone->imei_refused)
			return -1; /* la última línea de stderr es «imei-check: REFUSED…»; la lista de causas engañaría */
		fprintf(stderr,
		        "\nflash-anycid: bflash_ldr_db2020 falló. Causas probables:\n"
		        "  - El MIDlet «executer» no está corriendo en el teléfono.\n"
		        "    Soltar: desconecta, pon SIM, enciende, Juegos ->\n"
		        "    executer, y vuelve a conectar en modo servicio.\n"
		        "  - El paquete anycid que subiste no corresponde a la\n"
		        "    CXC del aparato (comprueba la línea «Anycid package»\n"
		        "    de `identify`).\n");
		return -1;
	}

	/* Con el loader de setool2 cargado la verificación es rápida (como en
	 * el camino break_rsa de DB2010): CHECKBABE_CHECKFAST, igual que hace
	 * `action_flash_fw` cuando usa BFLASH. */
	int fw_chk = CHECKBABE_CHECKFAST;

	int main_hecho = 0;
	if (main_fw) {
		if (flash_babe_fw(port, main_fw, fw_chk) != 0) {
			fprintf(stderr,
			        "\n!!! flash-anycid: la escritura del MAIN falló. Si empezó, el main del teléfono puede estar a\n"
			        "    medias: NO apagues ni desconectes; repite `-a flash-anycid main <f>` o restaura.\n");
			return -1;
		}
		main_hecho = 1;
	}
	if (fs_fw) {
		if (flash_babe_fw(port, fs_fw, fw_chk) != 0) {
			if (main_hecho)
				fprintf(stderr,
				        "\n!!! flash-anycid: el MAIN se escribió pero el FS NO: el teléfono tiene el main NUEVO con\n"
				        "    el FS VIEJO. No lo reinicies así: repite `-a flash-anycid fs <f>`.\n");
			else
				fprintf(stderr, "\n!!! flash-anycid: la escritura del FS falló; repítela antes de reiniciar.\n");
			return -1;
		}
	}

	return 0;
}

/* seftool-senew: flash DB2020 CID52 RED por la rama jdflasher. Es la pareja
 * de ESCRITURA de la rama de LECTURA cableada en el commit anterior: una vez
 * que `read-flash --jdflasher --cid 52 --imei <S>` ha demostrado que el
 * prologue y el flashloader R2A005 enganchan, esta acción abre el mismo
 * camino y escribe main / fs sin firmar como lo haría la rama anycid, pero
 * SIN executer (ni paquetes anycid, ni MIDlet).
 *
 * A diferencia de flash-anycid, aquí hay DOS compuertas de entorno
 * obligatorias: SEFTOOL_JDFLASHER_CONFIRM=1 (ya validada en main.c para
 * cualquier --jdflasher) y SEFTOOL_JDFLASHER_WRITE=1 (específica de la
 * ESCRITURA; sin ella, es DRY-RUN forzoso). El DRY-RUN sí carga el
 * flashloader para probar que el prologue engancha, pero no envía BFLASH.
 * Así «flash-jdflasher» se puede ensayar en el aparato con la red de
 * seguridad puesta, antes de echar bytes a la NOR.
 *
 * Compuertas duras:
 *   - chip_id == DB2020 (si no, se rechaza con mensaje jdflasher-only).
 *   - erom_color != BROWN (BROWN no necesita jdflasher).
 *   - erom_cid == 52 (49/51 van por --anycid; 53 queda pendiente).
 *   - SEFTOOL_JDFLASHER_WRITE=="1" exacto, ADEMÁS de los _CONFIRM=1 que
 *     main.c ya exigió.
 *   - al menos uno de main_fw / fs_fw.
 *   - flash_babe_precheck de cada fichero dado.
 *   - compuerta de senew (SENEW_COV_IMAGE_AVAILABLE), con el escape
 *     SEFTOOL_SENEW_FORCE=1 ya documentado.
 *
 * Lo que esta función NO hace, intencionalmente:
 *   - No «repara el arranque» de forma condicional: la rama jdflasher de
 *     loader_send_bflash_ldr_db2020 ya invoca bflash_repair_boot al final,
 *     igual que la rama anycid, para no duplicar lógica aquí.
 *   - No acepta CDA (como flash-anycid): aceptarlo y callar daba la
 *     impresión de que se había escrito.
 */
/* ACT_JDFLASHER_PRIME — VÍA DE RESPALDO, manual, de la secuencia prologue+R2A005.
 * El camino PRINCIPAL (read-flash/flash-jdflasher SIN --skip-prologue) ya encadena
 * el prologue con connection_restart() y reconecta solo, sin tocar el teléfono.
 * Esta acción es para cuando esa reconexión automática no alcanza (p. ej. el enlace
 * USB de la Mac es inestable, ver seftool-en-mac-por-usb.md): carga 2020_prologue +
 * "R" y sale, dejando que el humano reconecte físicamente antes de repetir la orden
 * con --skip-prologue. El aparato se reinicia tras R (sale del modo flash), de modo
 * que el loader_shutdown posterior fallaría; action.c:case ACT_JDFLASHER_PRIME marca
 * phone->shutdown_skipped=1 para que main() lo salte. */
int action_jdflasher_prime(struct sp_port *port, struct phone_info *phone)
{
	if (phone->chip_id != DB2020) {
		fprintf(stderr, "jdflasher-prime: solo DB2020 CID52 RED.\n");
		return -1;
	}
	if (phone->expect_cid != 52) {
		fprintf(stderr, "jdflasher-prime: solo CID52 (--cid=%d).\n", phone->expect_cid);
		return -1;
	}
	printf("jdflasher-prime: cargando 2020_prologue (SOLO LECTURA del fichero local;\n"
	       "  escribirá/modificará lo que el prologue haga internamente al teléfono).\n");

	if (loader_send_prologue_jdflasher(port, phone, DB2020_PROLOGUE) != 0) {
		fprintf(stderr,
		        "jdflasher-prime: el prologue (2020_prologue) no respondió.\n"
		        "  Síntomas posibles: 'header not accepted' (EsB), 'prologue not accepted' (EhM),\n"
		        "  'prologue execution failed' (EaT).\n");
		return -1;
	}

	printf("\n");
	printf("====================================================================\n");
	printf("  JDFLASHER-PRIME COMPLETADO (vía de respaldo MANUAL).\n");
	printf("  El aparato arrancará por su cuenta en los próximos segundos.\n");
	printf("  (La vía normal —read-flash/flash-jdflasher sin --skip-prologue— ya\n");
	printf("   intenta reconectar sola con connection_restart() antes de llegar aquí.)\n");
	printf("\n");
	printf("  SIGUIENTE PASO (humano):\n");
	printf("  1. Espera a que el teléfono se encienda (modo USB de datos del Walkman).\n");
	printf("  2. Desconecta el cable, saca la batería 5 s, vuelve a ponerla.\n");
	printf("  3. Mantén la tecla C pulsada y conecta el cable (modo flash).\n");
	printf("  4. Lanza una acción --jdflasher --skip-prologue, por ejemplo:\n");
	printf("       seftool -p usb -b 921600 -a read-flash start 0x44000000 size 0x20000 \\\n");
	printf("           --jdflasher --skip-prologue --cid %d --imei %s\n",
	       phone->expect_cid, phone->expect_imei[0] ? phone->expect_imei : "<IMEI>");
	printf("====================================================================\n");

	return 0;
}

int action_flash_jdflasher(struct sp_port *port, struct phone_info *phone,
                           const char *main_fw, const char *fs_fw)
{
	if (phone->chip_id != DB2020) {
		fprintf(stderr,
		        "flash-jdflasher: solo DB2020 CID52 RED. Para DB2010 CID49 RED usa `-a flash` con "
		        "--break-rsa; para DB2020 CID49/51 RED usa flash-anycid.\n");
		return -1;
	}
	/* La rama jdflasher NO carga P3M antes del prologue (es su diseño: el XML oficial
	 * jdflasher.dll va directo), así que phone->erom_cid y erom_color siguen en 0 aquí:
	 * nadie leyó el OTP. BROWN y CID activo los filtra la firma del propio prologue en
	 * el bootrom; nosotros solo exigimos --cid 52 explícito (ya validado en main.c). */
	if (phone->expect_cid != 52) {
		fprintf(stderr,
		        "flash-jdflasher: solo CID52 (--cid=%d). Para CID49/51 va flash-anycid; "
		        "CID53 queda pendiente de verificación.\n",
		        phone->expect_cid);
		return -1;
	}
	if (!main_fw && !fs_fw) {
		fprintf(stderr, "flash-jdflasher: se requiere al menos 'main' o 'fs'.\n");
		return -1;
	}

	/* Compuerta de ESCRITURA, aparte de SEFTOOL_JDFLASHER_CONFIRM=1 (que main.c
	 * ya exigió para cualquier --jdflasher). Sin WRITE, DRY-RUN forzoso: la
	 * función sigue cargando el flashloader (es la demostración de que el
	 * prologue engancha), pero no manda BFLASH. */
	const char *write_env = getenv("SEFTOOL_JDFLASHER_WRITE");
	int write_ok = (write_env && strcmp(write_env, "1") == 0);

	/* Compuerta de senew: mismo molde que flash-anycid. SEFTOOL_SENEW_FORCE=1
	 * salta pero avisa ruidosamente. */
	if (senew_force_requested()) {
		fprintf(stderr, "flash-jdflasher: SEFTOOL_SENEW_FORCE activo, se salta la compuerta de senew.\n");
	} else {
		senew_coverage_t cov = senew_coverage_for(SENEW_BOARD_W580, 52);
		if (cov != SENEW_COV_IMAGE_AVAILABLE) {
			fprintf(stderr,
			        "flash-jdflasher: la tabla de senew no da cobertura de escritura para W580/DB2020 CID52 "
			        "(estado %d). No se escribe nada.\n"
			        "  Si de verdad quieres seguir: SEFTOOL_SENEW_FORCE=1\n",
			        (int)cov);
			return -1;
		}
	}

	/* Precheck de los ficheros ANTES de tocar el puerto: así una imagen de FS
	 * corrupta no se descubre con el MAIN ya escrito. */
	int forzar = senew_force_requested();
	if (main_fw && flash_babe_precheck(main_fw, 52, forzar) != 0) {
		fprintf(stderr, "flash-jdflasher: %s no pasa la comprobación; no se toca el teléfono.\n", main_fw);
		return -1;
	}
	if (fs_fw && flash_babe_precheck(fs_fw, 52, forzar) != 0) {
		fprintf(stderr, "flash-jdflasher: %s no pasa la comprobación; no se toca el teléfono.\n", fs_fw);
		return -1;
	}

	/* Cargar el flashloader por la rama jdflasher añadida en el commit 1 a
	 * loader_send_bflash_ldr_db2020: qhtry(2020_prologue) + run + ldr(R2A005) +
	 * activateloader. Esto imprime «Security disabled =)» y repara el
	 * arranque. */
	phone->jdflasher = 1;

	if (loader_send_bflash_ldr(port, phone) != 0) {
		if (phone->imei_refused)
			return -1; /* la última línea de stderr es «imei-check: REFUSED…»; otra lista engañaría */
		fprintf(stderr,
		        "\nflash-jdflasher: el flashloader no cargó. Lo que suele fallar:\n"
		        "  - 'header not accepted': el 2020_prologue no se aceptó (BA BE 00 03). Revisa\n"
		        "    que loader/2020_prologue tenga el sha256 esperado (ver docs/CID52_JDFLASHER.md).\n"
		        "  - 'prologue not accepted' / 'prologue execution failed': el teléfono no está en\n"
		        "    modo servicio o la batería flaquea; vuelve a conectar y repítelo.\n"
		        "  - 'bad payload': el flashloader R2A005 llegó mal o la línea USB se cayó.\n");
		return -1;
	}

	if (!write_ok) {
		printf("\n"
		       "DRY-RUN: SEFTOOL_JDFLASHER_WRITE no está en «1», no se mandará ningún BFLASH.\n"
		       "         El flashloader quedó cargado (prueba hecha). Para flashear de verdad:\n"
		       "         SEFTOOL_JDFLASHER_CONFIRM=1 SEFTOOL_JDFLASHER_WRITE=1 \\\n"
		       "           seftool -a flash-jdflasher main <f> [fs <f>] --cid 52 --imei <S>\n");
		return 0;
	}

	/* Con el flashloader cargado la verificación es rápida: CHECKBABE_CHECKFAST,
	 * igual que la rama anycid y que `action_flash_fw` cuando usa BFLASH. */
	int fw_chk = CHECKBABE_CHECKFAST;

	int main_hecho = 0;
	if (main_fw) {
		if (flash_babe_fw(port, main_fw, fw_chk) != 0) {
			fprintf(stderr,
			        "\n!!! flash-jdflasher: la escritura del MAIN falló. Si empezó, el main del teléfono puede\n"
			        "    estar a medias: NO apagues ni desconectes; repite `-a flash-jdflasher main <f>` o restaura.\n");
			return -1;
		}
		main_hecho = 1;
	}
	if (fs_fw) {
		if (flash_babe_fw(port, fs_fw, fw_chk) != 0) {
			if (main_hecho)
				fprintf(stderr,
				        "\n!!! flash-jdflasher: el MAIN se escribió pero el FS NO: el teléfono tiene el main NUEVO\n"
				        "    con el FS VIEJO. No lo reinicies así: repite `-a flash-jdflasher fs <f>`.\n");
			else
				fprintf(stderr,
				        "\n!!! flash-jdflasher: la escritura del FS falló; repítela antes de reiniciar.\n");
			return -1;
		}
	}

	return 0;
}

int action_upload_to_fs(struct sp_port *port, struct phone_info *phone, const char **src_files, int src_count,
                        const char *dst_dir)
{
	phone->gdfs_server = 1;

	if (loader_send_ofs_ldr(port, phone) != 0)
		return -1;
	if (csloader_start_fsx_server(port) != 0)
		return -1;
	if (csloader_change_directory(port, dst_dir) != 0)
		return -1;

	for (int i = 0; i < src_count; i++) {
		const char *src_name = src_files[i];

		if (!file_exists(src_name)) {
			fprintf(stderr, "Warning: %s not found, skipping\n", src_name);
			continue;
		}

		printf("=== %s (%d/%d) ===\n", src_name, i + 1, src_count);

		if (is_directory(src_name)) {
			printf("-> Uploading directory:\n");
			if (csloader_write_directory(port, src_name, "", dst_dir) != 0) {
				fprintf(stderr, "Error uploading directory: %s\n", src_name);
				csloader_shutdown_fsx_server(port);
				return -1;
			}
		} else if (ends_with(src_name, ".zip")) {
			printf("-> Uploading zip:\n");
			if (csloader_upload_zip(port, src_name) != 0) {
				fprintf(stderr, "Error uploading zip: %s\n", src_name);
				csloader_shutdown_fsx_server(port);
				return -1;
			}
		} else {
			const char *basename = strrchr(src_name, '/');
			if (!basename)
				basename = strrchr(src_name, '\\');
			basename = basename ? basename + 1 : src_name;

			char target_path[512];
			snprintf(target_path, sizeof(target_path), "%s/%s", dst_dir, basename);
			if (normalize_path(target_path, sizeof(target_path)) != 0) {
				fprintf(stderr, "Path too long: %s/%s\n", dst_dir, basename);
				csloader_shutdown_fsx_server(port);
				return -1;
			}

			size_t filesize = 0;
			uint8_t *filebuff = load_file(src_name, &filesize);
			if (!filebuff) {
				fprintf(stderr, "Failed to read %s\n", src_name);
				csloader_shutdown_fsx_server(port);
				return -1;
			}

			if (filesize == 0)
				csloader_delete_file(port, target_path);
			else {
				if (csloader_put_file(port, target_path, filesize, filebuff) != 0) {
					fprintf(stderr, "Error copying file: %s\n", src_name);
					free(filebuff);
					csloader_shutdown_fsx_server(port);
					return -1;
				}
			}

			free(filebuff);
		}
	}

	if (csloader_shutdown_fsx_server(port) != 0)
		return -1;

	printf("\nAll uploads complete.\n");
	return 0;
}

int action_download_from_fs(struct sp_port *port, struct phone_info *phone, const char *src_path, const char *dest_dir)
{
	phone->gdfs_server = 1;

	ose_stat_t st;

	if (loader_send_bfs_ldr(port, phone) != 0)
		return -1;
	if (csloader_start_fsx_server(port) != 0)
		return -1;
	if (csloader_change_directory(port, "/") != 0)
		return -1;

	int result = -1;

	// --- Special case: root path is always a directory ---
	if (strcmp(src_path, "/") == 0) {
		printf("Source is root directory\n");
		result = csloader_read_directory(port, "/", dest_dir);
		goto done;
	}

	// --- Query file info ---
	if (csloader_stat(port, src_path, &st) != 0) {
		printf("Error: cannot stat '%s'\n", src_path);
		goto done;
	}

	// --- Directory or file ---
	if (st.st_mode & OSEATTR_DIRECTORY) {
		printf("'%s' is a directory\n", src_path);
		result = csloader_read_directory(port, src_path, dest_dir);
	} else {
		printf("'%s' is a file\n", src_path);
		const char *fname = strrchr(src_path, '/');
		if (!fname)
			fname = src_path;
		else
			fname++;

		char dest_path[512];
		snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, fname);

		result = csloader_get_file(port, dest_path, src_path);
	}

done:
	csloader_shutdown_fsx_server(port);

	if (result == 0)
		printf("\nDownload complete: %s\n", src_path ? src_path : "/");
	else
		printf("\nDownload failed: %s\n", src_path ? src_path : "/");

	if (phone->chip_id == DB2000 && phone->break_rsa) {
		result = bflash_repair_boot(port, phone);
	}

	return result;
}

int action_flash_fw(struct sp_port *port, struct phone_info *phone, const char *main_fw, const char *fs_fw,
                    const char *cda)
{
	int fw_flashed = 0;
	int use_bflash = 0;

	if ((phone->chip_id == DB2000 || phone->chip_id == DB2010_1 || phone->chip_id == DB2010_2) &&
	    phone->erom_cid <= 49 && phone->break_rsa == 1) {
		use_bflash = 1;
	}

	if (phone->erom_color == BROWN)
		use_bflash = 1;

	if (main_fw || fs_fw) {
		int fw_chk = CHECKBABE_CHECKFULL;
		if (use_bflash) {
			printf("BFLASH\n");
			if (loader_send_bflash_ldr(port, phone) != 0)
				return -1;
			fw_chk = CHECKBABE_CHECKFAST;
		} else {
			printf("OFLASH\n");
			if (loader_send_oflash_ldr(port, phone) != 0)
				return -1;

			if (phone->erom_cid <= 36 || phone->erom_color == BROWN)
				fw_chk = CHECKBABE_CHECKFAST;
		}

		if (main_fw) {
			if (flash_babe_fw(port, main_fw, fw_chk) != 0)
				return -1;

			fw_flashed++;
		}

		if (fs_fw) {
			if (flash_babe_fw(port, fs_fw, fw_chk) != 0)
				return -1;

			fw_flashed++;
		}
	}

	if (cda) {
		// Restart device before upload CDA package
		if (fw_flashed && (phone->chip_id != DB2020 && phone->chip_id != PNX5230)) {
			printf("\n");

			if (loader_shutdown(port) != 0)
				return -1;

			sleep_ms(20);
			connection_close(port);
			sleep_ms(20);

			printf("\n\nRestarting\n\n");

			/* reopen & handshake */
			if (connection_restart(port, phone) != 0) {
				fprintf(stderr, "reconnect failed\n");
				return -1;
			}
		}

		const char *src_list[] = {cda};
		if (action_upload_to_fs(port, phone, src_list, 1, "/") != 0)
			return -1;
	}

	return 0;
}

/* seftool-senew: compuerta de `read-flash --anycid` en un DB2020 (W580 RED). TODO lo que rechaza lo
 * rechaza ANTES de mandar un solo byte de loader: con el teléfono en modo C solo han salido los tres
 * paquetes del saludo. La última línea de stderr es siempre «read-flash --anycid: REFUSED: <motivo>»
 * (SENUS enseña esa línea), con las pistas en líneas anteriores.
 *   - --cid N obligatorio: el «ACTIVE CID» que imprimió `identify` (en un DB2020 recién conectado no se
 *     conoce hasta cargar un loader, así que lo da quien llama);
 *   - la tabla de senew tiene que dar cobertura (CID 49/51/52); CID 53 está sin verificar en un aparato
 *     real (docs/CID53_RUTA.md) y se niega;
 *   - la ventana tiene que caber en la NOR 0x44000000..0x48000000.
 * SEFTOOL_SENEW_FORCE=1 (solo el valor «1») salta la tabla y la ventana a sabiendas, igual que flash-anycid. */
int action_read_flash_gate(const struct phone_info *phone, uint32_t addr, uint64_t size)
{
	/* El gate se comparte entre --anycid y --jdflasher: la etiqueta de la última
	 * línea tiene que decir por cuál vino el refusal (SENUS parsea esto literal). */
	const char *tag = phone->jdflasher ? "read-flash --jdflasher" : "read-flash --anycid";

	if (senew_force_requested()) {
		fprintf(stderr, "%s: SEFTOOL_SENEW_FORCE activo, se saltan la tabla de senew y la "
		                "ventana de la NOR.\n", tag);
		return 0;
	}

	if (phone->expect_cid == 0) {
		fprintf(stderr, "  Sin el CID activo no se puede saber si es seguro seguir. Va con `--cid <N>`.\n"
		                "%s: REFUSED: falta --cid <N>, el «ACTIVE CID» que imprimió `identify` "
		                "(49, 51, 52 o 53); no se manda ningún loader.\n", tag);
		return -1;
	}

	senew_coverage_t cov = senew_coverage_for(SENEW_BOARD_W580, phone->expect_cid);
	if (cov != SENEW_COV_IMAGE_AVAILABLE) {
		fprintf(stderr,
		        "  W580/DB2020 CID%d: %s.\n"
		        "  Si de verdad quieres seguir: SEFTOOL_SENEW_FORCE=1\n"
		        "%s: REFUSED: la tabla de senew no da cobertura de lectura para W580/DB2020 "
		        "CID%d; no se manda ningún loader.\n",
		        phone->expect_cid,
		        cov == SENEW_COV_PENDING_VERIFICATION
		                ? "sin verificar en un aparato real (docs/CID53_RUTA.md, paso 4)"
		                : "ese CID no está entre los que conoce la tabla (49, 51, 52)",
		        tag, phone->expect_cid);
		return -1;
	}

	const char *why = NULL;
	if (flash_read_window_check(addr, size, &why) != 0) {
		fprintf(stderr,
		        "  La ventana pedida es 0x%08X + 0x%llX y tiene que caber en la NOR 0x%08X..0x%08X.\n"
		        "%s: REFUSED: %s; no se manda ningún loader.\n",
		        addr, (unsigned long long)size, NOR_READ_MIN, NOR_READ_MAX,
		        tag, why ? why : "ventana no válida");
		return -1;
	}

	return 0;
}

int action_read_flash(struct sp_port *port, struct phone_info *phone, uint32_t addr, uint32_t size)
{
	/* DB2020 CID52 RED + --jdflasher: lectura por la ruta oficial jdflasher.dll (sin executer).
	 * Es la hermana de la rama --anycid de abajo, pero NO carga P3M antes del prologue: por eso
	 * exige --imei explícito en línea de órdenes (lo cablean las compuertas de main.c). */
	if (phone->chip_id == DB2020 && phone->jdflasher == 1) {
		/* La rama jdflasher NO carga P3M antes del prologue (es su diseño: el XML
		 * oficial jdflasher.dll va directo), así que phone->erom_cid y erom_color
		 * siguen en 0: nadie leyó el OTP. Confiamos en phone->expect_cid, que main.c
		 * ya validó como 52 antes de abrir el puerto. Si el aparato NO es CID52,
		 * el prologue firmado para CID52 lo rechaza el bootrom (handshake EsB/EhM/EaT). */
		if (phone->expect_cid != 52) {
			fprintf(stderr,
			        "read-flash --jdflasher: solo DB2020 CID52 RED. (--cid=%d; para "
			        "CID49/51 va --anycid, para CID53 no hay ruta todavía)\n",
			        phone->expect_cid);
			return -1;
		}
		if (action_read_flash_gate(phone, addr, size) != 0)
			return -1;

		printf("read-flash --jdflasher: SOLO LECTURA, no se manda ninguna orden de escritura a la NOR. %s\n",
		       phone->need_rest ? "El arranque se repara DESPUÉS del volcado (--restore-boot)."
		                        : "La reparación del arranque está APAGADA.");

		sp_flush(port, SP_BUF_BOTH);

		/* Pareja jdflasher: 2020_prologue (v3) + 2020_f_R2A005. Sin P3M, sin executer.
		 * El prologue NO es flashloader (no envía hello, no se activa): va por
		 * loader_send_prologue_jdflasher. El R2A005 sí es flashloader y lo monta
		 * loader_send_binary (hello + loader_activate_payload). Molde del XML
		 * jdflasher.dll <qhtry>2020_prologue</qhtry><run/><ldr>R2A005</ldr><activateloader/>.
		 *
		 * El R del prologue reinicia el aparato DE VERDAD (medido W580 roto 2026-10-18
		 * 09:48-10:10: sale del modo flash, arranca el sistema operativo). jdflasher.dll
		 * no necesita un `<reconnect/>` en el XML para esto porque lo resuelve por software
		 * (import table: SetupDiGetClassDevs/SetupDiEnumDeviceInterfaces, sin notificación de
		 * ventana → es un sondeo). Mismo molde que break_cid49 ya usa en esta base para
		 * DB2010 tras su propio exploit: connection_restart() reabre el puerto esperando
		 * hasta 15 s a que el mismo vid:pid reaparezca (mac/sp_usb.c) y rehace el protocolo
		 * de arranque, SIN que el humano desconecte cable ni batería (eso añade un reset de
		 * alimentación distinto, y tarda más de lo que el aparato espera). Con
		 * --skip-prologue (vía de respaldo si la reconexión automática no alcanza) se asume
		 * que `-a jdflasher-prime` ya corrió en otra sesión y el humano reconectó a mano. */
		if (!phone->skip_prologue) {
			if (loader_send_prologue_jdflasher(port, phone, DB2020_PROLOGUE) != 0) {
				fprintf(stderr,
				        "read-flash --jdflasher: el prologue (2020_prologue) no respondió.\n"
				        "  Si ya corriste `-a jdflasher-prime` antes en otra sesión, añade --skip-prologue\n"
				        "  a esta orden para saltar el prologue (el aparato ya quedó parcheado).\n");
				return -1;
			}

			if (connection_restart(port, phone) != 0) {
				fprintf(stderr,
				        "read-flash --jdflasher: el aparato no volvió tras el prologue (reconexión\n"
				        "  automática de hasta 15 s agotada). Vía de respaldo: usa `-a jdflasher-prime`\n"
				        "  y repite esta orden con --skip-prologue tras reconectar a mano en modo C.\n");
				return -1;
			}
		}
		/* El R2A005 va por qhldr CRUDO tras el prologue (ver loader_send_qhldr_jdflasher y
		 * docs/CID52_JDFLASHER.md). Un cambio a loader_send_binary aquí reintroduce el
		 * `-2 of 902 bytes` que medimos el 2026-10-18. */
		if (loader_send_qhldr_jdflasher(port, phone, DB2020_FLLOADER_R2A005_DEN_PO) != 0) {
			fprintf(stderr, "read-flash --jdflasher: el flashloader R2A005 no engancha sobre el "
			                "prologue.\n");
			return -1;
		}

		if (flash_read(port, phone, addr, size) != 0)
			return -1;

		if (phone->need_rest) {
			printf("\n--restore-boot: reparando el arranque DESPUÉS del volcado\n");
			if (bflash_repair_boot(port, phone) != 0) {
				fprintf(stderr, "read-flash --jdflasher: el volcado quedó guardado, pero la "
				                "reparación del arranque (--restore-boot) falló\n");
				return -1;
			}
		}
		return 0;
	}

	/* DB2020 + --anycid (W580 RED): la lectura de la NOR entera de primera clase. Pasa por la compuerta,
	 * carga SOLO el loader de setool2, LEE, y solo después (si se pidió --restore-boot) repara el
	 * arranque: la NOR volcada es la que había, no la ya «reparada». */
	if (phone->chip_id == DB2020 && phone->anycid == 1) {
		if (action_read_flash_gate(phone, addr, size) != 0)
			return -1;

		printf("read-flash --anycid: SOLO LECTURA, no se manda ninguna orden de escritura a la NOR. %s\n",
		       phone->need_rest ? "El arranque se repara DESPUÉS del volcado (--restore-boot)."
		                        : "La reparación del arranque está APAGADA.");

		/* loader_send_bflash_ldr hace este flush al entrar; esta rama no pasa por ahí. */
		sp_flush(port, SP_BUF_BOTH);

		if (loader_send_setool2_db2020(port, phone) != 0) {
			if (phone->imei_refused)
				return -1; /* la última línea de stderr es «imei-check: REFUSED…»; la lista de causas engañaría */
			fprintf(stderr, "\nread-flash --anycid: el loader de setool2 no cargó. Causas probables:\n"
			                "  - El MIDlet «executer» no está corriendo en el teléfono (desconecta, pon SIM,\n"
			                "    enciende, Juegos -> executer, apaga y vuelve a conectar en modo C).\n"
			                "  - El paquete anycid no corresponde a la CXC del aparato (`upload-anycid`).\n"
			                "read-flash --anycid: no cargó el loader de setool2 (¿executer activo?)\n");
			return -1;
		}

		if (flash_read(port, phone, addr, size) != 0)
			return -1;

		if (phone->need_rest) {
			printf("\n--restore-boot: reparando el arranque DESPUÉS del volcado\n");
			if (bflash_repair_boot(port, phone) != 0) {
				fprintf(stderr, "read-flash --anycid: el volcado quedó guardado, pero la reparación del "
				                "arranque (--restore-boot) falló\n");
				return -1;
			}
		}
		return 0;
	}

	if (loader_send_bflash_ldr(port, phone) != 0)
		return -1;

	if (flash_read(port, phone, addr, size) != 0)
		return -1;

	return 0;
}

int action_restore_gdfs(struct sp_port *port, struct phone_info *phone, const char *inputfname)
{
	/* El volcado se comprueba entero antes de subir NINGÚN loader. */
	if (csloader_gdfs_dump_restorable(inputfname, phone->gdfs_write_max, phone->gdfs_skip_big) != 0)
		return -1;

	if (loader_send_ofs_ldr(port, phone) != 0)
		return -1;

	if (csloader_write_gdfs(port, inputfname, phone->gdfs_write_max, phone->gdfs_skip_big) != 0)
		return -1;

	if (gdfs_terminate_access(port) != 0)
		return -1;

	return 0;
}

int action_backup_gdfs(struct sp_port *port, struct phone_info *phone)
{
	if (loader_send_ofs_ldr(port, phone) != 0)
		return -1;

	if (csloader_read_gdfs(port, phone) != 0)
		return -1;

	if (gdfs_terminate_access(port) != 0)
		return -1;

	return 0;
}

int action_exec_scripts(struct sp_port *port, struct phone_info *phone, int nfiles, const char **filenames)
{
	int rc = 0;
	int has_vkp = 0;
	int has_txt = 0;

	// First pass: detect what we got
	for (int i = 0; i < nfiles; i++) {
		const char *ext = strrchr(filenames[i], '.');
		if (ext && strcasecmp(ext, ".vkp") == 0)
			has_vkp = 1;
		else
			has_txt = 1;
	}

	if (has_vkp && has_txt) {
		fprintf(stderr, "Error: cannot mix VKP patches and GDFS "
		                "scripts in one run.\n");
		return 0;
	}

	if (has_vkp) {
		// --- Prepare bflash loader once ---
		if (loader_send_bflash_ldr(port, phone) != 0)
			return -1;

		int patched_count = 0;
		int skipped_count = 0;

		// --- Loop over VKP patches ---
		for (int i = 0; i < nfiles; i++) {
			printf("\n=== Patching %d/%d ===\n", i + 1, nfiles);

			const char *fname = filenames[i];
			vkp_patch_t patch;
			vkp_patch_init(&patch);

			if (vkp_load_file(fname, &patch) != 0) {
				fprintf(stderr, "Failed to parse VKP file: %s\n", fname);
				vkp_patch_free(&patch);
				rc = -1;
				continue; // try next patch
			}

			printf("\n%s parsed successfully, %zu byte(s)\n", fname, patch.patch.count);

			int vkp_rc = flash_vkp(port, fname, &patch, 0, phone->flashblocksize);
			if (vkp_rc == FLASH_VKP_SKIP) {
				skipped_count++;
				vkp_patch_free(&patch);
				continue; // keep processing others
			}
			if (vkp_rc == FLASH_VKP_ERR) {
				vkp_patch_free(&patch);
				rc = -1;
				break;
			}
			patched_count++;
			vkp_patch_free(&patch);
		}

		printf("\nSummary: %d patched, %d skipped\n\n", patched_count, skipped_count);
		return rc;
	}

	// --- CSLOADER once ---
	if (loader_send_ofs_ldr(port, phone) != 0)
		return -1;

	for (int i = 0; i < nfiles; i++) {
		const char *fname = filenames[i];
		printf("\n=== %s (%d/%d) ===\n", fname, i + 1, nfiles);

		char script_name[512];
		snprintf(script_name, sizeof(script_name), "./script_%s_%s.txt", phone->phone_name, phone->otp_imei);

		if (csloader_parse_gdfs_script(port, fname, script_name) != 0) {
			rc = -1;
			break;
		}
	}

	if (rc == 0) {
		if (gdfs_terminate_access(port) != 0)
			rc = -1;
	}

	return rc;
}

int action_convert(const char *cnv_mode, const char *cnv_filename, uint32_t mem_addr)
{
	char outname[256];

	if (strcmp(cnv_mode, "raw2babe") == 0) {
		snprintf(outname, sizeof(outname), "%s.ssw", cnv_filename);
		if (flash_cnv_raw_to_babe_file(cnv_filename, outname, mem_addr) != 0) {
			fprintf(stderr, "Error: failed to convert raw to babe\n");
			return -1;
		}
	} else if (strcmp(cnv_mode, "babe2raw") == 0) {
		snprintf(outname, sizeof(outname), "%s.bin", cnv_filename);
		if (flash_cnv_babe_to_raw_file(cnv_filename, outname) != 0) {
			fprintf(stderr, "Error: failed to convert babe to raw\n");
			return -1;
		}
	}

	return 0;
}
