/* babe_check rápido y conteo de bloques sobre el main real del W580, y
 * sobre una copia truncada (debe rechazarse). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "emp/v3/babe.h"

static int contar(uint8_t *b, size_t size)
{
	struct babehdr_t *h = (struct babehdr_t *)b;
	size_t hs = h->ver >= 4 ? 20 : 1;
	size_t cur = h->ver <= 2 ? 0x480 : h->payloadsize1 * hs + 0x380;
	int bl;
	for (bl = 0; bl < (int)h->payloadsize1; bl++) {
		if (cur + 8 >= size)
			break;
		int32_t bs = ((int32_t *)(b + cur))[1];
		cur += 8 + bs;
		if (cur > size)
			break;
	}
	return bl;
}

int main(int argc, char **argv)
{
	size_t size;
	uint8_t *b = load_file(argv[1], &size);
	if (!b)
		return 2;
	printf("cabecera: %u bloques, contados: %d\n", ((struct babehdr_t *)b)->payloadsize1, contar(b, size));
	printf("check rápido completo: %d (OK=%d)\n", babe_check(b, size, CHECKBABE_CHECKFAST), CHECKBABE_OK);
	printf("check completo: %d\n", babe_check(b, size, CHECKBABE_CHECKFULL));
	printf("check rápido truncado a la mitad: %d\n", babe_check(b, size / 2, CHECKBABE_CHECKFAST));
	return 0;
}
