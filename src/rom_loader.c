#include "rom_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char *dir, const char *name, uint8_t *out, size_t expected)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len != (long)expected) {
        fprintf(stderr, "%s has %ld bytes, expected %zu\n", path, len, expected);
        fclose(f);
        return -1;
    }
    if (fread(out, 1, expected, f) != expected) {
        fprintf(stderr, "short read on %s\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int mw_load_roms(const char *directory, uint8_t rom[0x80000], uint8_t key[0x2000])
{
    /* Heap, not stack: the two 256 KiB bank buffers plus the caller's 512 KiB
     * ROM buffer overflow the ~1 MiB SDL thread stack on Android. */
    uint8_t *a6 = (uint8_t *)malloc(0x40000);
    uint8_t *a5 = (uint8_t *)malloc(0x40000);
    int rc = -1;

    if (!a6 || !a5) {
        fprintf(stderr, "out of memory loading the ROM banks\n");
        free(a6);
        free(a5);
        return -1;
    }
    if (read_file(directory, "epr-13235.a6", a6, 0x40000))
        goto done;
    if (read_file(directory, "epr-13234.a5", a5, 0x40000))
        goto done;
    if (read_file(directory, "317-0159.key", key, 0x2000))
        goto done;
    for (size_t i = 0; i < 0x40000; i++) {
        rom[i * 2] = a6[i];
        rom[i * 2 + 1] = a5[i];
    }
    rc = 0;
done:
    free(a6);
    free(a5);
    return rc;
}
