#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <libserialport.h>

#include <core/common.h>
#include <core/serial.h>
#include <emp/common/cmd.h>
#include <utils/sha1.h>

#include "babe.h"
#include "connection.h"
#include "csloader.h"
#include "flash.h"
#include "loader.h"
#include "gdfs.h"
#include "payload.h"
#include "break.h"

int bflash_repair_boot(struct sp_port *port, struct phone_info *phone)
{
	if (phone->chip_id == DB2000) {
		connection_close(port);
		printf("\n");

		sleep_ms(200);

		/* reopen & handshake */
		if (connection_restart(port, phone) != 0) {
			fprintf(stderr, "reconnect failed\n");
			return -1;
		}

		if (loader_send_qhtry_setool(port, phone, DB2000_CRIPPLED_PRELOADER_SETOOL2_R2B) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2000_FLLOADER_R2B_DEN_PO) != 0)
			return -1;
	}

	if (phone->need_rest == 0)
		goto skip_rest;

	if (flash_restore_boot_area(port, phone) != 0)
		return -1;

skip_rest:
	printf("\n");
	return 0;
}

int loader_send_bflash_ldr_db2000(struct sp_port *port, struct phone_info *phone)
{
	// TODO CID16
	if (phone->erom_cid == 29) {
		if (loader_send_qhldr(port, phone, DB2000_CERTLOADER_RED_CID00_R3L) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2000_FLLOADER_RED_CID29_R3S) != 0)
			return -1;
		return 0;
	} else if (phone->erom_cid == 36) {
		if (loader_send_qhldr(port, phone, DB2000_PILOADER_RED_CID00_R1F) != 0)
			return -1;
		if (break_cid36(port, phone) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2000_FLLOADER_R2B_DEN_PO) != 0)
			return -1;
		return 0;
	} else if (phone->erom_cid == 49 && phone->erom_color == BROWN) {
		if (loader_send_qhldr(port, phone, DB2000_PILOADER_RED_CID00_R1F) != 0)
			return -1;
		if (break_cid36(port, phone) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2000_FLLOADER_R2B_DEN_PO) != 0)
			return -1;
		return 0;
	} else if (phone->erom_cid == 49 && phone->erom_color == RED) {
		if (loader_send_qhldr(port, phone, DB2000_PILOADER_RED_CID03_P3B) != 0)
			return -1;

		struct gdfs_data_t gdfs = {0};
		if (break_build_bootname(port, phone, &gdfs) != 0)
			return -1;

		if (loader_shutdown(port) != 0)
			return -1;

		connection_close(port);
		printf("\n");

		sleep_ms(20);

		/* reopen & handshake */
		if (connection_restart(port, phone) != 0) {
			fprintf(stderr, "reconnect failed\n");
			return -1;
		}

		if (loader_send_qhldr(port, phone, DB2000_PILOADER_RED_CID00_R2B) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2000_FLLOADER_RED_CID49_R2B) != 0)
			return -1;
		if (break_cid49(port, phone) != 0)
			return -1;

		sleep_ms(20);

		if (loader_send_qhtry_jdflasher(port, phone, DB2000_HEADER_R2B_DEN_PO) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2000_FLLOADER_R2B_DEN_PO) != 0)
			return -1;

		printf("Security disabled =)\n");

		if (flash_restore_boot_area(port, phone) != 0)
			return -1;

		printf("\n");

		return 0;
	}

	fprintf(stderr, "[BFLASH] This cid & cert is not supported\nconvert to brown first or using "
	                "'--break-rsa'(cid49 only) or '--anycid' exploit\n");
	return -1;
}

int loader_send_bflash_ldr_db2010(struct sp_port *port, struct phone_info *phone)
{
	// TODO CID16
	if (phone->erom_cid <= 29) {
		if (loader_send_qhldr(port, phone, DB2010_CERTLOADER_RED_CID01_R2E) != 0)
			return -1;
		if (break_cid29(port, phone) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2010_FLLOADER_P5G_DEN_PO) != 0)
			return -1;
		return 0;
	} else if (phone->erom_cid == 36) {
		if (loader_send_qhldr(port, phone, DB2010_PILOADER_RED_CID00_R2F) != 0)
			return -1;
		if (break_cid36(port, phone) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2010_FLLOADER_P5G_DEN_PO) != 0)
			return -1;
		return 0;
	} else if (phone->erom_cid == 49 && phone->erom_color == BROWN) {
		if (loader_send_qhldr(port, phone, DB2010_PILOADER_BROWN_CID49_R1A002) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2010_FLLOADER_R2B_DEN_PO) != 0)
			return -1;
		return 0;
	} else if (phone->erom_cid == 51 && phone->erom_color == BROWN) {
		if (loader_send_qhldr(port, phone, DB2010_PILOADER_RED_CID00_P4D) != 0)
			return -1;

		if (loader_send_binary(port, phone, DB2010_RESPIN_PRODLOADER_SETOOL2) != 0)
			return -1;
		return 0;
	} else if (phone->erom_color == RED && phone->erom_cid == 49 && phone->break_rsa == 1) {
		if (loader_send_qhldr(port, phone, DB2010_PILOADER_RED_CID00_P3L) != 0)
			return -1;

		struct gdfs_data_t gdfs = {0};
		if (break_build_bootname(port, phone, &gdfs) != 0)
			return -1;

		if (loader_send_binary(port, phone, DB2010_FLLOADER_RED_CID49_R2A007) != 0)
			return -1;

		if (break_cid49(port, phone) != 0)
			return -1;

		if (loader_send_qhtry_jdflasher(port, phone, DB2010_HEADER_P3L_DEN_PO) != 0)
			return -1;

		if (strstr(gdfs.phone_name, "W550") != 0 || strstr(gdfs.phone_name, "W600") != 0 ||
		    strstr(gdfs.phone_name, "K750") != 0 || strstr(gdfs.phone_name, "W800") != 0 ||
		    strstr(gdfs.phone_name, "W700") != 0 || strstr(gdfs.phone_name, "Z520") != 0) {
			if (loader_send_binary(port, phone, DB2010_FLLOADER_R2B_DEN_PO) != 0)
				return -1;
		} else {
			if (loader_send_binary(port, phone, DB2010_FLLOADER_P5G_DEN_PO) != 0)
				return -1;
		}

		printf("Security disabled =)\n");

		if (bflash_repair_boot(port, phone) != 0)
			return -1;

		return 0;
	} else if (phone->erom_color == RED && phone->anycid == 1) // ANYCID targets RED CID49+
	{
		if (loader_send_qhtry_setool(port, phone, DB2010_RESPIN_ID_LOADER_SETOOL2) != 0) {
			loader_hint_run_executer(phone);
			return -1;
		}
		if (loader_send_binary(port, phone, DB2010_RESPIN_PRODLOADER_SETOOL2) != 0)
			return -1;

		printf("Security disabled =)\n");

		if (bflash_repair_boot(port, phone) != 0)
			return -1;

		return 0;
	}
	fprintf(stderr, "[BFLASH] This cid & cert is not supported\nconvert to brown first or using "
	                "'--break-rsa'(cid49 only) or '--anycid' exploit\n");
	return -1;
}

/* La rama anycid del DB2020: sube el preloader y el loader de setool2 («Security disabled =)»).
 * NO repara el área de arranque: lo decide quien la llama. read-flash --anycid primero LEE y solo
 * después repara (si se pidió --restore-boot), para que el volcado sea la NOR tal como estaba. */
int loader_send_setool2_db2020(struct sp_port *port, struct phone_info *phone)
{
	if (loader_send_qhtry_setool(port, phone, DB2020_PRELOADER_FOR_SETOOL2) != 0) {
		loader_hint_run_executer(phone);
		return -1;
	}
	if (loader_send_binary(port, phone, DB2020_LOADER_FOR_SETOOL2) != 0)
		return -1;

	printf("Security disabled =)\n");
	return 0;
}

int loader_send_bflash_ldr_db2020(struct sp_port *port, struct phone_info *phone)
{
	if (phone->anycid == 1) {
		if (loader_send_setool2_db2020(port, phone) != 0)
			return -1;

		if (bflash_repair_boot(port, phone) != 0)
			return -1;

		return 0;
	}

	/* --jdflasher: DB2020 CID52 RED SIN executer. El XML oficial jdflasher.dll <bflash break="2020_52">
	 * manda qhtry(2020_prologue) + run + ldr(2020_f_R2A005) + activateloader, SIN P3M antes. La
	 * reparación del arranque se delega en bflash_repair_boot (mismo molde que la rama anycid).
	 * Confiamos en expect_cid porque erom_cid/color NO se han leído: nadie cargó P3M. Si el
	 * aparato no es CID52 el prologue firmado lo rechaza el bootrom en EsB/EhM/EaT. */
	if (phone->jdflasher == 1 && phone->expect_cid == 52) {
		/* La verificación del SHA-1 del prologue (contra DB2020_PROLOGUE_SHA1_HEX) vive dentro
		 * de loader_send_prologue_jdflasher: así TODAS las rutas que llaman al prologue (bflash,
		 * action_read_flash y las que vengan) están cubiertas sin duplicar. Si el fichero cambia
		 * se niega antes de enviar un solo byte al teléfono.
		 *
		 * El prologue 2020_prologue NO es un flashloader; es un payload que modifica algo en el
		 * bootrom y devuelve el control sin hello. Luego el flashloader R2A005 (que sí es
		 * flashloader) trae el hello y pide loader_activate_payload.
		 *
		 * Tras el R del prologue el aparato se reinicia DE VERDAD (medido 2026-10-18: sale
		 * del modo flash y arranca el sistema operativo, no un simple hipo del bus USB).
		 * jdflasher.dll (import table: SetupDiGetClassDevs/SetupDiEnumDeviceInterfaces de
		 * SETUPAPI.dll, sin RegisterDeviceNotification ni mensajes de ventana) no usa un
		 * `<reconnect/>` del XML para este paso porque no hace falta: reabre el dispositivo
		 * por software en un bucle de sondeo, igual que ya hace esta base para break_cid49
		 * en DB2010 (emp/v3/break.c) a través de connection_restart(). Aquí aplicamos el
		 * mismo molde en vez de pedirle al humano que desconecte cable y batería: eso tarda
		 * demasiado y además DESCONECTA LA ALIMENTACIÓN, un reset distinto del que hace el
		 * propio prologue. connection_restart()→serial_open()→sp_open() ya sabe esperar
		 * hasta 15 s (mac/sp_usb.c, variable ya_se_abrio) a que el mismo vid:pid reaparezca
		 * sin intervención humana; rehace wait_for_Z+'?'+connection_get_type+connection_set_speed
		 * (erom_get_info es no-op para DB2020). Con phone->skip_prologue=1 esta rama asume
		 * que una sesión anterior de jdflasher-prime ya corrió y el humano ya reconectó a
		 * mano (vía de respaldo si la reconexión automática no alcanza). */
		if (!phone->skip_prologue) {
			if (loader_send_prologue_jdflasher(port, phone, DB2020_PROLOGUE) != 0)
				return -1;

			if (connection_restart(port, phone) != 0) {
				fprintf(stderr,
				        "jdflasher: el aparato no volvió tras el prologue (reconexión automática\n"
				        "  de hasta 15 s agotada). Vía de respaldo: usa `-a jdflasher-prime` y repite\n"
				        "  esta orden con --skip-prologue tras reconectar a mano en modo C.\n");
				return -1;
			}
		}
		/* ATENCIÓN: tras el prologue + run, el R2A005 va por qhldr CRUDO (QH00/QA00/QD00),
		 * NO por loader_send_binary (cmd 0x3C). El plugin oficial lo despacha así cuando
		 * el flag "already loaded" está a 0 — y qhtry/qhldr no marcan ese flag. Si cambias
		 * esto a loader_send_binary volverás a ver el `-2 of 902 bytes` del 2026-10-18
		 * (ver docs/CID52_JDFLASHER.md «Causa del -2 of 902»). */
		if (loader_send_qhldr_jdflasher(port, phone, DB2020_FLLOADER_R2A005_DEN_PO) != 0)
			return -1;

		printf("Security disabled =)\n");

		if (bflash_repair_boot(port, phone) != 0)
			return -1;

		return 0;
	}

	if (loader_send_qhldr(port, phone, DB2020_PILOADER_RED_CID01_P3M) != 0)
		return -1;
	if (phone->erom_color == BROWN) {
		if (loader_send_binary(port, phone, DB2020_PILOADER_BROWN_CID49_SETOOL) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2020_FLLOADER_R2A005_DEN_PO) != 0)
			return -1;
		return 0;
	}

	fprintf(stderr, "[BFLASH] This cid & cert is not supported, convert to brown first or "
	                "break using '--anycid' "
	                "exploit\n");
	if (phone->erom_color == RED)
		fprintf(stderr,
		        "[BFLASH] Este DB2020 es RED (CID activo %d): para esta acción hace falta el exploit anycid.\n"
		        "         `upload-anycid`, abre Juegos -> «executer» en el teléfono, reconecta en modo C y\n"
		        "         repite la orden con `--anycid` (read-flash además lleva `--cid %d`).\n",
		        phone->erom_cid, phone->erom_cid);
	return -1;
}

int loader_send_bflash_ldr_pnx5230(struct sp_port *port, struct phone_info *phone)
{
	struct gdfs_data_t gdfs = {0};
	pnx_get_rest_name(port, phone, &gdfs);

	phone->anycid = 1;

	if (loader_send_qhtry_setool(port, phone, PNX5230_PRELOADER_FOR_SETOOL2) != 0) {
		loader_hint_run_executer(phone);
		return -1;
	}
	if (loader_send_binary(port, phone, PNX5230_PRODUCTION_LOADER_FOR_SETOOL2) != 0)
		return -1;

	printf("Security disabled =)\n");

	if (bflash_repair_boot(port, phone) != 0)
		return -1;

	return 0;
}

int loader_send_bflash_ldr(struct sp_port *port, struct phone_info *phone)
{
	sp_flush(port, SP_BUF_BOTH);
	switch (phone->chip_id) {
	case DB2000:
		return loader_send_bflash_ldr_db2000(port, phone);
	case DB2010_1:
	case DB2010_2:
		return loader_send_bflash_ldr_db2010(port, phone);
	case DB2020:
		return loader_send_bflash_ldr_db2020(port, phone);
	case PNX5230:
		return loader_send_bflash_ldr_pnx5230(port, phone);
	}

	fprintf(stderr, "[send_bflash_ldr] This cid & cert is not supported, convert to brown first\n");
	return -1;
}

int loader_send_bfs_ldr_db2000(struct sp_port *port, struct phone_info *phone)
{
	// TODO CID16&29
	if (phone->erom_cid == 36) {
		if (loader_send_qhldr(port, phone, DB2000_PILOADER_RED_CID00_R1F) != 0)
			return -1;
		if (break_cid36(port, phone) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2000_CSLOADER_R4B_SETOOL) != 0)
			return -1;
		return 0;
	} else if (phone->erom_cid == 49 && phone->erom_color == BROWN) {
		if (loader_send_qhldr(port, phone, DB2000_PILOADER_RED_CID00_R1F) != 0)
			return -1;
		if (break_cid36(port, phone) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2000_CSLOADER_R4B_SETOOL) != 0)
			return -1;
		return 0;
	} else if (phone->erom_cid == 49 && phone->erom_color == RED) {
		if (loader_send_qhldr(port, phone, DB2000_PILOADER_RED_CID03_P3B) != 0)
			return -1;

		struct gdfs_data_t gdfs = {0};
		if (break_build_bootname(port, phone, &gdfs) != 0)
			return -1;

		if (loader_shutdown(port) != 0)
			return -1;

		connection_close(port);
		printf("\n");
		sleep_ms(20);

		/* reopen & handshake */
		if (connection_restart(port, phone) != 0) {
			fprintf(stderr, "reconnect failed\n");
			return -1;
		}

		if (loader_send_qhldr(port, phone, DB2000_PILOADER_RED_CID00_R2B) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2000_FLLOADER_RED_CID49_R2B) != 0)
			return -1;
		if (break_cid49(port, phone) != 0)
			return -1;

		sleep_ms(20);

		if (loader_send_qhtry_setool(port, phone, DB2000_CRIPPLED_PRELOADER_SETOOL2_R2B) != 0)
			return -1;

		if (loader_send_binary(port, phone, DB2000_SEMC_PRODUCTION_R2D) != 0)
			return -1;

		printf("Security disabled =)\n");

		if (loader_send_binary(port, phone, DB2000_FILE_SYSTEM_LOADER_SETOOL2_V24) != 0)
			return -1;

		return 0;
	}

	fprintf(stderr, "[BFS] This cid & cert is not supported\nconvert to brown first or using "
	                "'--break-rsa'(cid49 only)\n");
	return -1;
}

int loader_send_bfs_ldr_db2010(struct sp_port *port, struct phone_info *phone)
{
	if (phone->erom_cid <= 29) {
		if (loader_send_qhldr(port, phone, DB2010_CERTLOADER_RED_CID01_R2E) != 0)
			return -1;
		if (break_cid29(port, phone) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2010_CSLOADER_R2C_DEN_PO) != 0)
			return -1;
		return 0;
	} else if (phone->erom_cid == 36) // Both RED and BROWN
	{
		if (loader_send_qhldr(port, phone, DB2010_PILOADER_RED_CID00_R2F) != 0)
			return -1;
		if (break_cid36(port, phone) != 0)
			return -1;
		if (phone->chip_id == DB2010_1)
			return loader_send_binary(port, phone, DB2010_CSLOADER_R2C_DEN_PO);
		return loader_send_binary(port, phone, DB2010_CSLOADER_HAK_CID00_V23);
	}

	if (phone->erom_color == BROWN) {
		switch (phone->erom_cid) {
		case 49:
			if (loader_send_qhldr(port, phone, DB2010_PILOADER_RED_CID00_R2AB) != 0)
				return -1;
			if (strstr(phone->phone_name, "K750") || strstr(phone->phone_name, "W700") ||
			    strstr(phone->phone_name, "W800")) {
				return loader_send_binary(port, phone, DB2010_FS_LOADER_SETOOL2_V23);
			}
			break;
		case 51:
			if (loader_send_qhldr(port, phone, DB2010_PILOADER_RED_CID00_P4D) != 0)
				return -1;
			if (loader_send_binary(port, phone, DB2010_RESPIN_PRODLOADER_SETOOL2) != 0)
				return -1;
			break;
		default:
			fprintf(stderr, "[OFS DB2010 BROWN] Unknown CID! %d\n", phone->erom_cid);
			return -1;
		}
		// K310/K320/K510/W200/W300/W810/Z530/Z550/Z558
		return loader_send_binary(port, phone, DB2010_FS_LOADER_SETOOL2_V26);
	} else if (phone->erom_color == RED) {
		if (loader_send_bflash_ldr_db2010(port, phone) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2010_FS_LOADER_SETOOL2_V26) != 0)
			return -1;
		return 0;
	}

	fprintf(stderr, "[BFLASH] This cid & cert is not supported, convert to brown first or "
	                "break using '--anycid' "
	                "exploit\n");
	return -1;
}

int loader_send_bfs_ldr_db2020(struct sp_port *port, struct phone_info *phone)
{
	if (phone->anycid == 1) {
		if (loader_send_bflash_ldr_db2020(port, phone) != 0)
			return -1;

		if (loader_send_binary(port, phone, DB2020_CSLOADER_R3A006_DEN_PO) != 0)
			return -1;
		return 0;
	}

	/* --jdflasher sobre la rama file-system: encadena el csloader R3A006 sobre el flashloader R2A005
	 * que ya montó loader_send_bflash_ldr_db2020. Mismo molde que la rama anycid de arriba.
	 * Confiamos en expect_cid porque P3M no corrió antes del prologue.
	 *
	 * TODO (2026-10-18): esta rama NO está probada y según el XML oficial del plugin
	 * (scripts/qa2020.xml CID52) la vía bfs CID52 debe re-ejecutar prologue + run +
	 * qhldr(2020_cs_R3A006) + activatecsloader desde CERO, no encadenar sobre R2A005. Con
	 * el fix de 2026-10-18 (R2A005 por qhldr crudo, que no marca el flag "already loaded"
	 * del plugin) el encadenamiento con loader_send_binary(R3A006) seguramente volvería a
	 * fallar — pero igual que ahora: nadie lo usa aún. Cuando se use, cablear una variante
	 * loader_send_qhldr_jdflasher para R3A006 y meter su propio prologue previo. */
	if (phone->jdflasher == 1 && phone->expect_cid == 52) {
		if (loader_send_bflash_ldr_db2020(port, phone) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2020_CSLOADER_R3A006_DEN_PO) != 0)
			return -1;
		return 0;
	}

	if (loader_send_qhldr(port, phone, DB2020_PILOADER_RED_CID01_P3M) != 0)
		return -1;
	if (phone->erom_color == BROWN) {
		if (loader_send_binary(port, phone, DB2020_PILOADER_BROWN_CID49_SETOOL) != 0)
			return -1;
		if (loader_send_binary(port, phone, DB2020_CSLOADER_R3A006_DEN_PO) != 0)
			return -1;
		return 0;
	}

	fprintf(stderr, "[BFLASH] This cid & cert is not supported, convert to brown first or "
	                "break using '--anycid' "
	                "exploit\n");
	return -1;
}

int loader_send_bfs_ldr_pnx5230(struct sp_port *port, struct phone_info *phone)
{
	if (loader_send_bflash_ldr_pnx5230(port, phone) != 0)
		return -1;

	if (loader_send_binary(port, phone, PNX5230_FILESYSTEMLOADER_FOR_SETOOL2) != 0)
		return -1;

	return 0;
}

int loader_send_bfs_ldr(struct sp_port *port, struct phone_info *phone)
{
	switch (phone->chip_id) {
	case DB2000:
		return loader_send_bfs_ldr_db2000(port, phone);
	case DB2010_1:
	case DB2010_2:
		return loader_send_bfs_ldr_db2010(port, phone);
	case DB2020:
		return loader_send_bfs_ldr_db2020(port, phone);
	case PNX5230:
		return loader_send_bfs_ldr_pnx5230(port, phone);

	default:
		fprintf(stderr, "[send_bfs_ldr] unknown CHIPID %X\n", phone->chip_id);
		return -1;
	}
}
