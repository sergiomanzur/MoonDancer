#ifndef MW_ZIP_LOADER_H
#define MW_ZIP_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Extracts a Moonwalker ROM zip into dest_dir.
 * On success, romdir_out receives the directory that directly contains
 * epr-13235.a6 (the zip may have a top-level folder). */
int mw_extract_zip(const char *zip_path, const char *dest_dir,
                   char *romdir_out, int romdir_cap);

#ifdef __cplusplus
}
#endif

#endif
