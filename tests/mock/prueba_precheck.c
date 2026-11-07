/* flash_babe_precheck sobre archivos de verdad, bajo ASan/UBSan. */
#include <stdio.h>
#include <stdlib.h>
#include <libserialport.h>
#include <core/common.h>
#include <emp/v3/flash.h>

int main(int argc, char **argv)
{
	int rc = argc > 1 ? flash_babe_precheck(argv[1], argc > 2 ? atoi(argv[2]) : 0, argc > 3) : 2;
	printf("precheck rc=%d\n", rc);
	return rc == 0 ? 0 : 1;
}
