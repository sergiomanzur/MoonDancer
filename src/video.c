/*
 * Moonwalker video: Sega System 18 tilemaps, System 16B sprites and the
 * 315-5313 VDP layer, composited with the board's priority scheme.
 */
#include "video.h"
#include "s18.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *tile_rom;
static size_t tile_rom_size;
static uint16_t *sprite_rom;
static size_t sprite_rom_words;

static uint32_t g_frame[S18_MAX_SCREEN_W * S18_SCREEN_H];
static uint8_t g_pri[S18_MAX_SCREEN_W * S18_SCREEN_H];

static int load_file(const char *dir, const char *name, uint8_t **out, size_t *size)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "video: cannot open %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return -1;
    }
    fclose(f);
    *out = buf;
    *size = (size_t)len;
    return 0;
}

int s18_video_init(s18_t *m, const char *romdir)
{
    (void)m;
    uint8_t *b1 = NULL, *b2 = NULL, *b3 = NULL;
    size_t s1 = 0, s2 = 0, s3 = 0;
    if (load_file(romdir, "mpr-13216.b1", &b1, &s1) ||
        load_file(romdir, "mpr-13217.b2", &b2, &s2) ||
        load_file(romdir, "mpr-13218.b3", &b3, &s3)) {
        free(b1); free(b2); free(b3);
        return -1;
    }
    tile_rom_size = s1 + s2 + s3;
    tile_rom = (uint8_t *)malloc(tile_rom_size);
    memcpy(tile_rom, b1, s1);
    memcpy(tile_rom + s1, b2, s2);
    memcpy(tile_rom + s1 + s2, b3, s3);
    free(b1); free(b2); free(b3);

    /* System 16B sprite ROMs: four 512 KiB banks, each split into two byte
     * halves, combined into big-endian 16-bit words. */
    static const char *high[4] = { "mpr-13231.a11", "mpr-13230.a10", "mpr-13229.a9", "epr-13228.a8" };
    static const char *low[4]  = { "mpr-13224.b11", "mpr-13223.b10", "mpr-13222.b9",  "epr-13221.b8" };
    sprite_rom_words = 0x200000 / 2;
    sprite_rom = (uint16_t *)calloc(sprite_rom_words, sizeof(uint16_t));
    for (int bank = 0; bank < 4; bank++) {
        uint8_t *h = NULL, *l = NULL;
        size_t hs = 0, ls = 0;
        if (load_file(romdir, high[bank], &h, &hs) || load_file(romdir, low[bank], &l, &ls)) {
            free(h); free(l);
            return -1;
        }
        size_t count = hs < ls ? hs : ls;
        for (size_t i = 0; i < count; i++)
            sprite_rom[bank * 0x40000 + i] = (uint16_t)((h[i] << 8) | l[i]);
        free(h); free(l);
    }
    return 0;
}

void s18_video_shutdown(void)
{
    free(tile_rom);
    free(sprite_rom);
    tile_rom = NULL;
    sprite_rom = NULL;
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static inline uint16_t be16p(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint16_t work16(s18_t *m, unsigned addr)
{
    addr &= 0x3FFF;
    return be16p(&m->workram[addr & 0xFFFE]);
}

static inline uint16_t tile16(s18_t *m, unsigned addr)
{
    addr &= 0xFFFF;
    return be16p(&m->tileram[addr & 0xFFFE]);
}

static inline uint16_t text16(s18_t *m, unsigned addr)
{
    addr &= 0xFFF;
    return be16p(&m->textram[addr & 0xFFFE]);
}

static inline uint16_t vram16(s18_t *m, unsigned addr)
{
    addr &= 0xFFFF;
    return be16p(&m->vdp_vram[addr & 0xFFFE]);
}

static inline int tile_pen(unsigned code, int x, int y)
{
    /* gfx_8x8x3_planar: the three bitplanes are three 256 KiB regions;
     * each tile is 8 bytes in each plane. */
    size_t base = (size_t)(code & 0x7FFF) * 8;
    if (!tile_rom || base + 8 > 0x40000)
        return 0;
    unsigned bit = 7 - (unsigned)x;
    unsigned p0 = (tile_rom[base + (unsigned)y] >> bit) & 1;
    unsigned p1 = (tile_rom[0x40000 + base + (unsigned)y] >> bit) & 1;
    unsigned p2 = (tile_rom[0x80000 + base + (unsigned)y] >> bit) & 1;
    return (int)((p2 << 2) | (p1 << 1) | p0);
}

static inline int vdp_tile_pen(s18_t *m, unsigned code, int x, int y)
{
    unsigned addr = (code * 32 + (unsigned)y * 4 + ((unsigned)x >> 1)) & 0xFFFF;
    uint8_t byte = m->vdp_vram[addr];
    return (x & 1) ? (byte & 0x0F) : (byte >> 4);
}

/* latched text-map scroll values, as the System 16B hardware latches them
 * at the start of vblank */
typedef struct {
    uint16_t pages[4];
    uint16_t xscroll[4];
    uint16_t yscroll[4];
} latch_t;

static void read_latches(s18_t *m, latch_t *l)
{
    for (int i = 0; i < 4; i++) {
        l->pages[i] = text16(m, 0xE80u + (unsigned)i * 2);
        l->yscroll[i] = text16(m, 0xE90u + (unsigned)i * 2);
        l->xscroll[i] = text16(m, 0xE98u + (unsigned)i * 2);
    }
}

/* ------------------------------------------------------------------ */
/* Tilemaps                                                            */
/* ------------------------------------------------------------------ */

static void draw_tile_line(s18_t *m, int scroll_index, int category, int opaque,
                           uint8_t mark, const latch_t *l, int y)
{
    unsigned xscroll = l->xscroll[scroll_index] & 0x3FF;
    unsigned yscroll = l->yscroll[scroll_index] & 0x1FF;
    unsigned pages = l->pages[scroll_index];
    unsigned effx = (0xC0u - xscroll) & 0x3FF;
    unsigned effy = yscroll & 0x1FF;
    unsigned wy = (unsigned)(y + (int)effy) & 0x1FF;
    unsigned qy = wy >> 8;
    unsigned ty = (wy & 0xFF) >> 3;

    /* A 512x256 page quadrant only holds the level chunk the game is
     * currently drawing when the original 320-column window overlaps it.
     * The loader leaves the other quadrant slots pointing at unrelated
     * chunks, so sampling them to fill the widescreen margin pastes a
     * foreign block over the scene with a hard vertical seam exactly at
     * the page boundary (measured on the cave stage: one column changes
     * by up to ~130 pixels against ~80 for ordinary terrain edges).
     * Only trust quadrants the visible window covers; reflect into the
     * covered one for the rest of the margin so it continues the scene. */
    unsigned wx_last = (effx + (S18_SCREEN_W - 1)) & 0x3FF;
    int qx0_used, qx1_used;
    if (wx_last < effx) {
        /* The window wraps past the 1024-column edge: it covers both. */
        qx0_used = qx1_used = 1;
    } else {
        qx0_used = effx < 512;
        qx1_used = wx_last >= 512;
    }

    for (int x = 0; x < m->view_w; x++) {
        unsigned wx = (unsigned)(x - m->view_left + (int)effx) & 0x3FF;
        unsigned qx = wx >> 9;
        unsigned page = (pages >> (qx * 4 + qy * 8)) & 0xF;
        unsigned tx = (wx & 0x1FF) >> 3;
        unsigned entry = tile16(m, page * 0x1000u + (ty * 64 + tx) * 2);
        int ox = x - m->view_left;
        if (m->view_left > 0 && (ox < 0 || ox >= S18_SCREEN_W) &&
            !(qx ? qx1_used : qx0_used)) {
            /* Reflect the world coordinate about the page boundary into
             * the quadrant the game is actually showing.  An empty
             * reflected entry means the level ends there; leave the
             * original (empty) entry so it clears to black below. */
            unsigned mwx = (ox >= S18_SCREEN_W) ? ((1023u - wx) & 0x3FFu)
                                                : ((0u - wx) & 0x3FFu);
            unsigned mqx = mwx >> 9;
            unsigned mpage = (pages >> (mqx * 4 + qy * 8)) & 0xF;
            unsigned mtx = (mwx & 0x1FF) >> 3;
            unsigned mentry = tile16(m, mpage * 0x1000u + (ty * 64 + mtx) * 2);
            if (mentry != 0) {
                entry = mentry;
                wx = mwx;
            }
        }
        if (((entry >> 15) & 1) != (unsigned)category)
            continue;
        unsigned code = entry & 0x1FFF;
        unsigned bank = m->tilebank[code >> 10];  /* 8 banks of 1024 tiles */
        code = bank * 1024u + (code & 0x3FF);
        unsigned color = (entry >> 6) & 0x7F;
        /* With wx replaced by the reflected world column, sampling the pen
         * at that column is already the mirror image; flipping the column
         * again would tear the margin every 8 pixels. */
        int penx = (int)(wx & 7);
        int pen = tile_pen(code, penx, (int)(wy & 7));
        if (pen == 0 && !opaque)
            continue;
        unsigned index = color * 8u + (unsigned)pen;
        int idx = y * m->view_w + x;
        if (opaque && entry == 0 && (ox < 0 || ox >= S18_SCREEN_W)) {
            /* Inside the original window an entry of 0 can still select a
             * real banked tile, so only clear the unstreamed outer area. */
            g_frame[idx] = 0xFF000000u;
        } else {
            g_frame[idx] = 0xFF000000u | m->palette_rgb[index & 0xFFF];
        }
        g_pri[idx] |= mark;
    }
}

static void draw_text_line(s18_t *m, int category, int opaque, uint8_t mark, int y)
{
    int ty = y >> 3;
    int py = y & 7;
    for (int x = 0; x < m->view_w; x++) {
        int ox = x - m->view_left;
        if (ox < 0 || ox >= S18_SCREEN_W)
            continue; /* never expose off-screen text/tile residue */
        int tx = (ox + 192) >> 3;
        int px = (ox + 192) & 7;
        if (tx >= 64) continue;
        unsigned entry = text16(m, (unsigned)(ty * 64 + tx) * 2);
        if (entry == 0) continue;
        if (((entry >> 15) & 1) != (unsigned)category)
            continue;
        unsigned color = (entry >> 9) & 7;
        unsigned code = (entry & 0x1FF) + m->tilebank[0] * 1024u;
        int pen = tile_pen(code, px, py);
        if (pen == 0 && !opaque)
            continue;
        unsigned index = color * 8u + (unsigned)pen;
        int idx = y * m->view_w + x;
        g_frame[idx] = 0xFF000000u | m->palette_rgb[index & 0xFFF];
        g_pri[idx] |= mark;
    }
}

/* ------------------------------------------------------------------ */
/* VDP                                                                 */
/* ------------------------------------------------------------------ */

static uint16_t vdp_line[S18_MAX_SCREEN_W];
static uint8_t vdp_pri[S18_MAX_SCREEN_W];

static void vdp_draw_plane_line(s18_t *m, int plane, int y)
{
    unsigned hsize_bits = m->vdp_regs[0x10] & 0x03;
    unsigned vsize_bits = (m->vdp_regs[0x10] >> 4) & 0x03;
    unsigned hsize = (hsize_bits == 0) ? 32 : (hsize_bits == 1) ? 64 : (hsize_bits == 3) ? 128 : 64;
    unsigned vsize = (vsize_bits == 0) ? 32 : (vsize_bits == 1) ? 64 : (vsize_bits == 3) ? 128 : 64;
    unsigned base = (plane == 0) ? (((m->vdp_regs[0x02] & 0x38) >> 3) << 13)
                                 : ((m->vdp_regs[0x04] & 0x07) << 13);
    unsigned hscroll_base = (unsigned)(m->vdp_regs[0x0D] & 0x3F) << 10;
    unsigned hscroll = 0;
    unsigned vscroll = m->vdp_vsram[plane == 0 ? 0 : 1] & 0x1FF;

    switch (m->vdp_regs[0x0B] & 0x03) {
    case 0: hscroll = vram16(m, hscroll_base + (plane == 0 ? 0u : 2u)); break;
    case 1: hscroll = vram16(m, hscroll_base + (plane == 0 ? 0u : 2u) + (unsigned)(y & 7) * 4); break;
    case 2: hscroll = vram16(m, hscroll_base + (plane == 0 ? 0u : 2u) + (unsigned)(y & ~7) * 4); break;
    case 3: hscroll = vram16(m, hscroll_base + (plane == 0 ? 0u : 2u) + (unsigned)y * 4); break;
    }
    hscroll &= 0x3FF;

    unsigned py = (unsigned)(y + (int)vscroll);
    unsigned vcol = (py >> 3) & (vsize - 1);
    unsigned ty = py & 7;

    for (int x = 0; x < m->view_w; x++) {
        int ox = x - m->view_left;
        /* Sample the plane continuously across the expanded view: the
         * plane is up to 128 tiles (1024 px) wide, so the widescreen
         * margins scroll into real plane data instead of repeating the
         * edge column of the visible 320-pixel window. */
        unsigned px = (unsigned)(ox + (int)hscroll);
        unsigned hcol = (px >> 3) & (hsize - 1);
        unsigned tx = px & 7;
        unsigned entry = vram16(m, base + (vcol * hsize + hcol) * 2);
        unsigned code = entry & 0x7FF;
        unsigned pal = (entry >> 13) & 3;
        if (entry & 0x0800) tx = 7 - tx;
        if (entry & 0x1000) ty = 7 - ty;
        int pen = vdp_tile_pen(m, code, (int)tx, (int)ty);
        if (pen == 0)
            continue;
        unsigned index = pal * 16u + (unsigned)pen;
        vdp_line[x] = (uint16_t)(index & 0x3F);
        vdp_pri[x] = (entry & 0x8000) ? 2 : 1;
    }
}

static void vdp_render_line(s18_t *m, int y)
{
    for (int x = 0; x < m->view_w; x++) {
        vdp_line[x] = 0xFFFF;
        vdp_pri[x] = 0;
    }
    if (!m->vdp_enable) return;
    if (!(m->vdp_regs[0x01] & 0x40)) return;
    if (m->vdp_regs[0x00] & 0x01) return;
    vdp_draw_plane_line(m, 1, y);
    vdp_draw_plane_line(m, 0, y);
}

static void composite_vdp_line(s18_t *m, int y, uint8_t mark)
{
    for (int x = 0; x < m->view_w; x++) {
        uint16_t pix = vdp_line[x];
        if (pix == 0xFFFF || (pix & 0x0F) == 0)
            continue;
        int idx = y * m->view_w + x;
        g_frame[idx] = 0xFF000000u | m->palette_rgb[0x1000 + (pix & 0x3F)];
        g_pri[idx] |= mark;
    }
}

/* ------------------------------------------------------------------ */
/* Sprites                                                             */
/* ------------------------------------------------------------------ */

static inline void put_sprite_pixel(s18_t *m, int x, int y, int colpri)
{
    int priority = (colpri >> 10) & 3;
    int idx = y * m->view_w + x;
    if ((1 << priority) <= g_pri[idx])
        return;
    int color = colpri & 0x3FF;
    if ((color & 0x3F0) == 0x3F0) {
        /* shadow/highlight: use the shadow palette */
        g_frame[idx] = 0xFF000000u | m->palette_rgb[(color & 0x3FF) + 2048];
    } else {
        g_frame[idx] = 0xFF000000u | m->palette_rgb[0x400 + color];
    }
}

static void draw_sprites(s18_t *m)
{
    for (int i = 0; i < 128; i++) {
        unsigned off = (unsigned)i * 16;
        unsigned w0 = be16p(&m->spriteram[off + 0]);
        unsigned w1 = be16p(&m->spriteram[off + 2]);
        unsigned w2 = be16p(&m->spriteram[off + 4]);
        unsigned w3 = be16p(&m->spriteram[off + 6]);
        unsigned w4 = be16p(&m->spriteram[off + 8]);
        unsigned w5 = be16p(&m->spriteram[off + 10]);
        if (w2 & 0x8000) break;
        int bottom = (int)(w0 >> 8);
        int top = (int)(w0 & 0xFF);
        /* The board's X field is 9 bits.  Expanded widescreen needs one
         * more bit for sprites past the original 320 columns; the game
         * writes the raw screen X straight into w1, so bit 9 is a valid
         * X bit here and must stay unsigned. */
        int xpos = (int)(w1 & 0x7FF);
        int hide = (w2 & 0x4000) != 0;
        int flip = (w2 & 0x0100) != 0;
        int pitch = (int8_t)(w2 & 0xFF);
        unsigned addr = w3;
        unsigned bank = (w4 >> 8) & 0xF;
        int colpri = (int)(((w4 & 0xFF) << 4) | (((w1 >> 9) & 0xF) << 12));
        int vzoom = (int)((w5 >> 5) & 0x1F);
        int hzoom = (int)(w5 & 0x1F);
        if (hide || top >= bottom || bank >= 16)
            continue;
        const uint16_t *bankdata = sprite_rom + (size_t)bank * 0x10000u;
        int sx = xpos - 184 + m->view_left;
        int xaccbase = 4 * hzoom;
        uint32_t yacc = 0;
        for (int y = top; y < bottom; y++) {
            addr += (unsigned)pitch;
            yacc += (uint32_t)vzoom << 10;
            if (yacc & 0x8000) {
                addr += (unsigned)pitch;
                yacc &= ~0x8000u;
            }
            if (y < 0 || y >= S18_SCREEN_H)
                continue;
            int xacc = xaccbase;
            int x = sx;
            unsigned a = addr;
            if (!flip) {
                a -= 1;
                for (;;) {
                    uint16_t pixels = bankdata[((a + 1) & 0xFFFF)];
                    a++;
                    int pix = (pixels >> 12) & 0xF;
                    xacc = (xacc & 0x3F) + hzoom;
                    if (xacc < 0x40) {
                        if (pix != 0 && pix != 15 && x >= 0 && x < m->view_w)
                            put_sprite_pixel(m, x, y, colpri | pix);
                        x++;
                    }
                    pix = (pixels >> 8) & 0xF;
                    xacc = (xacc & 0x3F) + hzoom;
                    if (xacc < 0x40) {
                        if (pix != 0 && pix != 15 && x >= 0 && x < m->view_w)
                            put_sprite_pixel(m, x, y, colpri | pix);
                        x++;
                    }
                    pix = (pixels >> 4) & 0xF;
                    xacc = (xacc & 0x3F) + hzoom;
                    if (xacc < 0x40) {
                        if (pix != 0 && pix != 15 && x >= 0 && x < m->view_w)
                            put_sprite_pixel(m, x, y, colpri | pix);
                        x++;
                    }
                    pix = pixels & 0xF;
                    xacc = (xacc & 0x3F) + hzoom;
                    if (xacc < 0x40) {
                        if (pix != 0 && pix != 15 && x >= 0 && x < m->view_w)
                            put_sprite_pixel(m, x, y, colpri | pix);
                        x++;
                    }
                    if (pix == 15) break;
                    if (((sx - x) & 0x1FF) == 1) break;
                }
            } else {
                a += 1;
                for (;;) {
                    uint16_t pixels = bankdata[((a - 1) & 0xFFFF)];
                    a--;
                    int pix = pixels & 0xF;
                    xacc = (xacc & 0x3F) + hzoom;
                    if (xacc < 0x40) {
                        if (pix != 0 && pix != 15 && x >= 0 && x < m->view_w)
                            put_sprite_pixel(m, x, y, colpri | pix);
                        x++;
                    }
                    pix = (pixels >> 4) & 0xF;
                    xacc = (xacc & 0x3F) + hzoom;
                    if (xacc < 0x40) {
                        if (pix != 0 && pix != 15 && x >= 0 && x < m->view_w)
                            put_sprite_pixel(m, x, y, colpri | pix);
                        x++;
                    }
                    pix = (pixels >> 8) & 0xF;
                    xacc = (xacc & 0x3F) + hzoom;
                    if (xacc < 0x40) {
                        if (pix != 0 && pix != 15 && x >= 0 && x < m->view_w)
                            put_sprite_pixel(m, x, y, colpri | pix);
                        x++;
                    }
                    pix = (pixels >> 12) & 0xF;
                    xacc = (xacc & 0x3F) + hzoom;
                    if (xacc < 0x40) {
                        if (pix != 0 && pix != 15 && x >= 0 && x < m->view_w)
                            put_sprite_pixel(m, x, y, colpri | pix);
                        x++;
                    }
                    if (pix == 15) break;
                    if (((sx - x) & 0x1FF) == 1) break;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

uint32_t *s18_render_frame(s18_t *m)
{
    int layer_mask = 0x1F;
    latch_t latch;
    read_latches(m, &latch);
    memset(g_pri, 0, sizeof(g_pri));
    for (int i = 0; i < m->view_w * S18_SCREEN_H; i++)
        g_frame[i] = 0xFF000000u;
    int vdplayer = (m->vdp_mixing >> 1) & 3;
    int vdppri = (m->vdp_mixing & 1) ? (1 << vdplayer) : 0;

    if (!m->display_enable) {
        /* black */
    } else {
        for (int y = 0; y < S18_SCREEN_H; y++) {
            /* background: opaque pass, then transparent priority pass */
            if (layer_mask & 1) {
                draw_tile_line(m, 1, 0, 1, 0, &latch, y);
                draw_tile_line(m, 1, 1, 1, 0, &latch, y);
            }
            vdp_render_line(m, y);
            if ((layer_mask & 16) && m->vdp_enable && vdplayer == 0)
                composite_vdp_line(m, y, (uint8_t)vdppri);
            if (layer_mask & 1) {
                draw_tile_line(m, 1, 0, 0, 0x01, &latch, y);
                draw_tile_line(m, 1, 1, 0, 0x02, &latch, y);
            }
            if (m->vdp_enable && vdplayer == 1)
                composite_vdp_line(m, y, (uint8_t)vdppri);

            draw_tile_line(m, 0, 0, 0, 0x02, &latch, y);
            draw_tile_line(m, 0, 1, 0, 0x04, &latch, y);
            if (m->vdp_enable && vdplayer == 2)
                composite_vdp_line(m, y, (uint8_t)vdppri);

            draw_text_line(m, 0, 0, 0x04, y);
            draw_text_line(m, 1, 0, 0x08, y);
            if (m->vdp_enable && vdplayer == 3)
                composite_vdp_line(m, y, (uint8_t)vdppri);
        }
        draw_sprites(m);
    }
    return g_frame;
}

uint8_t *s18_priority_bitmap(void)
{
    return g_pri;
}

int s18_save_bmp_rect(const char *path, uint32_t *pixels, int stride,
                      int src_x, int src_y, int w, int h)
{
    if (!path || !pixels || stride <= 0 || w <= 0 || h <= 0)
        return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int row = (w * 3 + 3) & ~3;
    uint8_t header[54] = { 0 };
    header[0] = 'B'; header[1] = 'M';
    uint32_t size = 54 + (uint32_t)row * h;
    header[2] = size & 0xFF; header[3] = (size >> 8) & 0xFF; header[4] = (size >> 16) & 0xFF; header[5] = (size >> 24) & 0xFF;
    header[10] = 54;
    header[14] = 40;
    header[18] = w & 0xFF; header[19] = (w >> 8) & 0xFF;
    header[22] = h & 0xFF; header[23] = (h >> 8) & 0xFF;
    header[26] = 1; header[28] = 24;
    header[34] = (uint32_t)(row * h) & 0xFF; header[35] = ((uint32_t)(row * h) >> 8) & 0xFF;
    header[36] = ((uint32_t)(row * h) >> 16) & 0xFF; header[37] = ((uint32_t)(row * h) >> 24) & 0xFF;
    fwrite(header, 1, 54, f);
    uint8_t *line = (uint8_t *)calloc(1, (size_t)row);
    if (!line) {
        fclose(f);
        return -1;
    }
    for (int y = h - 1; y >= 0; y--) {
        const uint32_t *src = pixels + (size_t)(src_y + y) * (size_t)stride + (size_t)src_x;
        for (int x = 0; x < w; x++) {
            uint32_t p = src[x];
            line[x * 3 + 0] = (uint8_t)(p & 0xFF);
            line[x * 3 + 1] = (uint8_t)((p >> 8) & 0xFF);
            line[x * 3 + 2] = (uint8_t)((p >> 16) & 0xFF);
        }
        fwrite(line, 1, (size_t)row, f);
    }
    free(line);
    fclose(f);
    return 0;
}

int s18_save_bmp(const char *path, uint32_t *pixels)
{
    return s18_save_bmp_rect(path, pixels, S18_SCREEN_W, 0, 0,
                             S18_SCREEN_W, S18_SCREEN_H);
}
