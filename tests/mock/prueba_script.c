/* csloader_parse_gdfs_script con líneas hostiles, bajo ASan/UBSan, contra el
 * mock. Las líneas malas se rechazan antes de escribir nada; la última, buena,
 * llega a escribir y ahí el mock (sin guion) termina con 77. */
#include <stdio.h>
#include <libserialport.h>
#include <core/common.h>
#include <emp/v3/csloader.h>

int main(int argc, char **argv)
{
	struct sp_port *port;
	if (argc < 2 || sp_get_port_by_name("mock", &port) != SP_OK)
		return 2;
	sp_open(port, SP_MODE_READ_WRITE);
	int rc = csloader_parse_gdfs_script(port, argv[1], "salida_script.txt");
	printf("parse_gdfs_script rc=%d\n", rc);
	return rc == 0 ? 0 : 1;
}
