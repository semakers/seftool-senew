/* gdfs_dump_sec_units (la copia de seguridad de las 5 unidades que hace
 * identify) contra el mock, bajo ASan/UBSan. */
#include <stdio.h>
#include <libserialport.h>
#include <core/common.h>
#include <emp/v3/gdfs.h>

int main(int argc, char **argv)
{
	struct sp_port *port;
	if (argc < 2 || sp_get_port_by_name("mock", &port) != SP_OK)
		return 2;
	sp_open(port, SP_MODE_READ_WRITE);
	struct phone_info phone = {0};
	int rc = gdfs_dump_sec_units(port, &phone, argv[1]);
	printf("gdfs_dump_sec_units rc=%d\n", rc);
	return rc == 0 ? 0 : 1;
}
