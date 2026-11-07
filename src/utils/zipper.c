#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <minizip/unzip.h>

#include <core/common.h>

static int mkdir_recursive(const char *path)
{
	char tmp[4096];
	strncpy(tmp, path, sizeof(tmp));
	tmp[sizeof(tmp) - 1] = '\0';

	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/' || *p == '\\') {
			*p = '\0';
			MKDIR(tmp);
			*p = '/';
		}
	}

	MKDIR(tmp);
	return 0;
}

int zip_extract(const char *zip_filename, const char *tmp_dir)
{
	unzFile uf = unzOpen(zip_filename);
	if (!uf) {
		fprintf(stderr, "Failed to open ZIP: %s\n", zip_filename);
		return -1;
	}

	if (unzGoToFirstFile(uf) != UNZ_OK) {
		fprintf(stderr, "No entries in ZIP: %s\n", zip_filename);
		unzClose(uf);
		return -1;
	}

	do {
		char filename_inzip[1024];
		unz_file_info fi;

		if (unzGetCurrentFileInfo(uf, &fi, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0) != UNZ_OK)
			continue;

		filename_inzip[sizeof(filename_inzip) - 1] = '\0';
		if (filename_inzip[0] == '\0' || strstr(filename_inzip, "..")) {
			fprintf(stderr, "Bad name in ZIP: '%s'\n", filename_inzip);
			unzClose(uf);
			return -1;
		}

		/* Un zip reempaquetado en macOS trae __MACOSX/, ._* y .DS_Store: no son
		 * del paquete y no deben acabar en la memoria del teléfono. */
		{
			const char *base = strrchr(filename_inzip, '/');
			base = base ? base + 1 : filename_inzip;
			if (strncmp(filename_inzip, "__MACOSX/", 9) == 0 || strncmp(base, "._", 2) == 0 ||
			    strcmp(base, ".DS_Store") == 0)
				continue;
		}

		char outpath[4096];
		snprintf(outpath, sizeof(outpath), "%s/%s", tmp_dir, filename_inzip);

		// Directory entry
		if (filename_inzip[strlen(filename_inzip) - 1] == '/' ||
		    filename_inzip[strlen(filename_inzip) - 1] == '\\') {
			mkdir_recursive(outpath);
			continue;
		}

		// Ensure directory exists
		char *last_slash = strrchr(outpath, '/');
		if (last_slash) {
			*last_slash = '\0';
			mkdir_recursive(outpath);
			*last_slash = '/';
		}

		if (unzOpenCurrentFile(uf) != UNZ_OK) {
			fprintf(stderr, "Failed to open file in ZIP: %s\n", filename_inzip);
			unzClose(uf);
			return -1;
		}

		FILE *out = fopen(outpath, "wb");
		if (!out) {
			fprintf(stderr, "Failed to create file: %s\n", outpath);
			unzCloseCurrentFile(uf);
			unzClose(uf);
			return -1;
		}

		/* Un zip dañado no puede subirse a medias al teléfono (y un
		 * archivo vacío acaba BORRANDO el del teléfono): error de
		 * lectura, de CRC o de escritura = abortar. */
		uint8_t buf[4096];
		int nread;
		int mal = 0;
		while ((nread = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0)
			if (fwrite(buf, 1, nread, out) != (size_t)nread)
				mal = 1;
		if (nread < 0)
			mal = 1;
		if (fclose(out) != 0)
			mal = 1;
		if (unzCloseCurrentFile(uf) != UNZ_OK)
			mal = 1;
		if (mal) {
			fprintf(stderr, "ZIP entry damaged or not written: %s\n", filename_inzip);
			unzClose(uf);
			return -1;
		}

	} while (unzGoToNextFile(uf) == UNZ_OK);

	unzClose(uf);
	return 0;
}

int zip_test(const char *zip_filename)
{
	unzFile uf = unzOpen(zip_filename);
	if (!uf)
		return -1;
	int r = unzGoToFirstFile(uf);
	while (r == UNZ_OK) {
		{
			/* los mismos nombres que zip_extract rechaza */
			char nombre[1024];
			unz_file_info fi;
			if (unzGetCurrentFileInfo(uf, &fi, nombre, sizeof(nombre), NULL, 0, NULL, 0) != UNZ_OK) {
				unzClose(uf);
				return -1;
			}
			nombre[sizeof(nombre) - 1] = '\0';
			if (nombre[0] == '\0' || strstr(nombre, "..")) {
				fprintf(stderr, "Bad name in ZIP: '%s'\n", nombre);
				unzClose(uf);
				return -1;
			}
		}
		if (unzOpenCurrentFile(uf) != UNZ_OK) {
			unzClose(uf);
			return -1;
		}
		uint8_t buf[4096];
		int n;
		while ((n = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0)
			;
		if (n < 0 || unzCloseCurrentFile(uf) != UNZ_OK) {
			unzClose(uf);
			return -1;
		}
		r = unzGoToNextFile(uf);
	}
	unzClose(uf);
	return r == UNZ_END_OF_LIST_OF_FILE ? 0 : -1;
}
