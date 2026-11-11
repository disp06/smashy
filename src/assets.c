#include "assets.h"
#include "plat.h"
#include <stdlib.h>
#include <stdio.h>

Model *g_models; int g_nmodels;
GfxTex *g_tex; int g_ntex;
GfxTex g_white, g_palette_tex;
Image g_img[IM_COUNT];
Image g_caricon[31][2];
Glyph g_glyph[95]; int g_font_cell, g_font_h;

static uint8_t *load(const char *name, size_t *sz) {
    char path[256];
    snprintf(path, sizeof path, "%s%s", plat_data_dir(), name);
    uint8_t *d = plat_read_file(path, sz);
    if (!d) dbg_log("missing %s", path);
    return d;
}
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static int load_textures(void) {
    size_t sz; uint8_t *d = load("textures.pak", &sz);
    if (!d || memcmp(d, "PAK1", 4)) return -1;
    g_ntex = rd32(d + 4);
    g_tex = calloc(g_ntex, sizeof(GfxTex));
    for (int i = 0; i < g_ntex; i++) {
        const uint8_t *b = d + rd32(d + 8 + i * 8);
        int w = rd32(b + 4), h = rd32(b + 8);
        if (gfx_tex_create_p8(&g_tex[i], w, h, b + 16, b + 16 + 1024, 0) < 0) { dbg_log("tex alloc fail %d", i); return -2; }
    }
    free(d);
    return 0;
}

static int load_models(void) {
    size_t sz; uint8_t *d = load("models.pak", &sz);
    if (!d || memcmp(d, "PAK1", 4)) return -1;
    g_nmodels = rd32(d + 4);
    g_models = calloc(g_nmodels, sizeof(Model));
    for (int i = 0; i < g_nmodels; i++) {
        const uint8_t *b = d + rd32(d + 8 + i * 8);
        Model *m = &g_models[i];
        memcpy(m->lo, b + 4, 12); memcpy(m->hi, b + 16, 12);
        m->nsub = rd32(b + 28); m->ncol = rd32(b + 32);
        const uint8_t *p = b + 36;
        m->cols = calloc(m->ncol ? m->ncol : 1, sizeof(Box));
        for (int c = 0; c < m->ncol; c++) { memcpy(m->cols[c].lo, p, 12); memcpy(m->cols[c].hi, p + 12, 12); m->cols[c].trig = rd32(p + 24); p += 28; }
        m->subs = calloc(m->nsub ? m->nsub : 1, sizeof(Sub));
        for (int s = 0; s < m->nsub; s++) {
            Sub *sb = &m->subs[s];
            sb->tex = rd32(p); sb->dir = rd32(p + 4); sb->count = rd32(p + 8); p += 12;
            sb->v = gfx_static_vertices(sb->count);
            if (!sb->v) { dbg_log("vertex pool full"); return -2; }
            memcpy(sb->v, p, (size_t)sb->count * sizeof(Vtx));
            p += (size_t)sb->count * sizeof(Vtx);
        }
    }
    free(d);
    return 0;
}

static void img_from(Image *im, const uint8_t *b, int linear) {
    int W = rd32(b + 4), H = rd32(b + 8), w = rd32(b + 12), h = rd32(b + 16);
    gfx_tex_create_rgba(&im->tex, W, H, b + 24, linear);
    im->u1 = (float)w / W; im->v1 = (float)h / H; im->w = w; im->h = h;
}

static int load_ui(void) {
    size_t sz; uint8_t *d = load("ui.pak", &sz);
    if (!d || memcmp(d, "NPK1", 4)) return -1;
    static const char *names[IM_COUNT] = {"btnPlay", "btnPlayDruk", "btnCar", "btnCarDruk", "btnPrize", "btnPrizeDruk", "btnBack", "btnBackDruk",
                                          "cashIcon", "zwaailichtenBlue", "zwaailichtenRed", "pauseBtn", "boxBase", "boxBaseDown", "boxBaseDownGreen",
                                          "boxBaseDownGrey", "btnBase 1", "btnBaseDruk 1", "btnEmpty", "btnEmptySelect", "btnGreen", "font"};
    uint32_t n = rd32(d + 4);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = d + 8 + i * 40;
        char nm[33]; memcpy(nm, e, 32); nm[32] = 0;
        const uint8_t *b = d + rd32(e + 32);
        if (!memcmp(b, "IMG1", 4)) {
            for (int k = 0; k < IM_COUNT; k++) if (!strcmp(nm, names[k])) img_from(&g_img[k], b, k == IM_FONT);
            int cn; char bw[8] = "";
            if (sscanf(nm, "Car%d%3s", &cn, bw) >= 1 && cn >= 1 && cn <= 30)
                img_from(&g_caricon[cn][bw[0] == 'B' ? 1 : 0], b, 1);
        } else if (!memcmp(b, "FNT1", 4)) {
            g_font_cell = rd32(b + 4); g_font_h = rd32(b + 8);
            int cnt = rd32(b + 12);
            for (int k = 0; k < cnt && k < 95; k++) {
                uint16_t v[3]; memcpy(v, b + 16 + k * 6, 6);
                g_glyph[k].x = v[0]; g_glyph[k].y = v[1]; g_glyph[k].adv = v[2];
            }
        }
    }
    free(d);
    return 0;
}

int assets_load(void) {
    uint8_t px[8 * 8 * 4];
    memset(px, 255, sizeof px);
    gfx_tex_create_rgba(&g_white, 8, 8, px, 0);
    // 16x16 palette for particles: row = hue, column = brightness
    static uint8_t pal[16 * 16 * 4];
    static const uint8_t hues[16][3] = {
        {255, 255, 255}, {130, 130, 130}, {60, 60, 60}, {255, 200, 40}, {255, 120, 20}, {230, 40, 20}, {80, 190, 60}, {40, 120, 40},
        {120, 80, 40}, {60, 120, 255}, {255, 240, 120}, {200, 200, 210}, {30, 30, 30}, {160, 220, 255}, {255, 90, 200}, {240, 220, 170}};
    for (int h = 0; h < 16; h++)
        for (int c = 0; c < 16; c++) {
            float k = 0.35f + 0.65f * c / 15.0f;
            uint8_t *q = pal + (h * 16 + c) * 4;
            q[0] = (uint8_t)(hues[h][0] * k); q[1] = (uint8_t)(hues[h][1] * k); q[2] = (uint8_t)(hues[h][2] * k); q[3] = 255;
        }
    gfx_tex_create_rgba(&g_palette_tex, 16, 16, pal, 0);
    int r;
    if ((r = load_textures()) < 0) return r - 10;
    if ((r = load_models()) < 0) return r - 20;
    if ((r = load_ui()) < 0) return r - 30;
    return 0;
}
