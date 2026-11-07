#ifndef zipper_h
#define zipper_h

int zip_extract(const char *zip_filename, const char *tmp_dir);
/* 0 si el zip se lee entero y todos los CRC cuadran; -1 si no. */
int zip_test(const char *zip_filename);

#endif // zipper_h