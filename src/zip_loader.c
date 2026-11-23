#include "zip_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MW_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MW_MKDIR(p) mkdir(p, 0755)
#endif

#include "miniz.h"

static void make_dir(const char *path)
{
    if (!path || !*path) return;
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = 0;
            MW_MKDIR(tmp);
            *p = saved;
        }
    }
    MW_MKDIR(tmp);
}

static void make_parent_dirs(const char *file_path)
{
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s", file_path);
    char *slash = strrchr(tmp, '/');
    if (!slash) slash = strrchr(tmp, '\\');
    if (slash) {
        *slash = 0;
        make_dir(tmp);
    }
}

static int safe_entry_name(const char *name)
{
    if (!name || !*name) return 0;
    if (name[0] == '/' || name[0] == '\\') return 0;
    if (name[1] == ':') return 0;
    if (strstr(name, "..")) return 0;
    return 1;
}

int mw_extract_zip(const char *zip_path, const char *dest_dir,
                   char *romdir_out, int romdir_cap)
{
    if (!zip_path || !dest_dir || !romdir_out || romdir_cap <= 0)
        return -1;
    romdir_out[0] = 0;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zip_path, 0))
        return -1;

    make_dir(dest_dir);
    char prefix[1024] = "";
    int found_rom = 0;
    mz_uint count = mz_zip_reader_get_num_files(&zip);

    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st))
            continue;
        if (st.m_is_directory)
            continue;
        if (!safe_entry_name(st.m_filename))
            continue;

        char rel[1024];
        snprintf(rel, sizeof(rel), "%s", st.m_filename);
        for (char *p = rel; *p; p++) {
            if (*p == '\\') *p = '/';
        }

        char out[2048];
        size_t dlen = strlen(dest_dir);
        if (dlen && (dest_dir[dlen - 1] == '/' || dest_dir[dlen - 1] == '\\'))
            snprintf(out, sizeof(out), "%s%s", dest_dir, rel);
        else
            snprintf(out, sizeof(out), "%s/%s", dest_dir, rel);

        make_parent_dirs(out);
        if (!mz_zip_reader_extract_to_file(&zip, i, out, 0)) {
            mz_zip_reader_end(&zip);
            return -1;
        }

        const char *base = strrchr(rel, '/');
        base = base ? base + 1 : rel;
        if (!strcmp(base, "epr-13235.a6") || !strcmp(base, "EPR-13235.A6")) {
            found_rom = 1;
            if (base != rel) {
                size_t plen = (size_t)(base - rel);
                if (plen >= sizeof(prefix)) plen = sizeof(prefix) - 1;
                memcpy(prefix, rel, plen);
                prefix[plen] = 0;
                while (plen && (prefix[plen - 1] == '/')) prefix[--plen] = 0;
            }
        }
    }

    mz_zip_reader_end(&zip);
    if (!found_rom)
        return -1;

    if (prefix[0])
        snprintf(romdir_out, (size_t)romdir_cap, "%s/%s", dest_dir, prefix);
    else
        snprintf(romdir_out, (size_t)romdir_cap, "%s", dest_dir);
    return 0;
}
