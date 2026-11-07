#ifndef gdfs_h
#define gdfs_h

/* Los textos de la GDFS pueden pasar de 32: el artículo CXC del W580 es
 * "R8BE001     prgCXC1123362_GENERIC_LA" (36). Con [32] upstream se salía
 * al campo siguiente y el nombre del paquete anycid salía bien por
 * accidente. 128 cubre también los caminos PNX (hasta 64). */
#define GDFS_TEXTO 128

struct gdfs_data_t {
	char phone_name[GDFS_TEXTO];
	char brand[GDFS_TEXTO];
	char cxc_article[GDFS_TEXTO];
	char cxc_version[GDFS_TEXTO];
	char langpack[GDFS_TEXTO];
	char cda_article[GDFS_TEXTO];
	char cda_revision[GDFS_TEXTO];
	char default_article[GDFS_TEXTO];
	char default_version[GDFS_TEXTO];
	char user_lock[16];
	char mcc[16];
	char mnc[16];
	uint8_t locked;
};

enum {
	GD_PHONE_NAME,
	GD_BRAND,
	GD_CXC_ARTICLE,
	GD_CXC_VERSION,
	GD_LANGPACK,
	GD_CDA_ARTICLE,
	GD_CDA_REVISION,
	GD_DEF_ARTICLE,
	GD_DEF_VERSION,
	GD_COUNT,
};

void gdfs_copy_text(char *dst, size_t cap, const uint8_t *src, size_t n);
void gdfs_copy_ucs2(char *dst, size_t cap, const uint8_t *src, size_t n);
int gdfs_read_var(struct sp_port *port, struct gdfs_data_t *gdfs, int gd_index, uint8_t block, uint8_t lsb,
                  uint8_t msb);
int gdfs_get_phonename(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs);
int gdfs_get_brand(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs);
int gdfs_get_cxc_article(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs);
int gdfs_get_cxc_version(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs);
int gdfs_get_language(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs);
int gdfs_get_cda_article(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs);
int gdfs_get_cda_revision(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs);
int gdfs_get_default_article(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs);
int gdfs_get_default_version(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs);
int gdfs_get_simlock(struct sp_port *port, struct gdfs_data_t *gdfs);
int gdfs_parse_simlockdata(struct gdfs_data_t *gdfs, uint8_t *simlock);
int gdfs_get_userlock(struct sp_port *port, struct gdfs_data_t *gdfs);
int gdfs_unlock_usercode(struct sp_port *port);
int gdfs_dump_sec_units(struct sp_port *port, struct phone_info *phone, const char *backup_name);
int gdfs_terminate_access(struct sp_port *port);
int pnx_get_rest_name(struct sp_port *port, struct phone_info *phone, struct gdfs_data_t *gdfs);

#endif // gdfs_h