/* El artículo CXC del W580 bueno (Telcel, R8BE001) debe llevar al
 * paquete anycid que existe en ./anycid. */
#include <stdio.h>
#include <string.h>
#include "common.h"

int main(void)
{
	struct phone_info phone = {0};
	const char *art = "R8BE001     prgCXC1123362_GENERIC_LA";
	parse_cxc_article_to_anycid_pkg(&phone, art);
	parse_cxc_article_to_rest_name(&phone, art);
	printf("anycid_target=%s existe=%d\n", phone.anycid_target, check_anycid_pkg(&phone));
	printf("rest_name=%s existe=%d\n", phone.rest_name, check_restore_file(&phone));
	/* Lo que salía con la copia cortada a 31: */
	struct phone_info corto = {0};
	parse_cxc_article_to_anycid_pkg(&corto, "R8BE001     prgCXC1123362_GENER");
	printf("cortado: anycid_target=%s existe=%d\n", corto.anycid_target, check_anycid_pkg(&corto));
	return !(check_anycid_pkg(&phone) && check_restore_file(&phone));
}
