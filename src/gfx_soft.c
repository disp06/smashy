// Software reference renderer (desktop test build only). Same semantics as gfx_gxm.c.
#include "gfx.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct { int w, h, p8; uint8_t *pal; uint8_t *data; int repeat; } SoftTex;
static uint32_t *fb;      // RGBA
static float *zb;
static uint8_t *sb;
static GfxTex white;
static Vtx *temp; static int temp_used;
uint32_t *soft_framebuffer(void) { return fb; }

int gfx_init(void) {
    fb = malloc(SCREEN_W * SCREEN_H * 4);
    zb = malloc(SCREEN_W * SCREEN_H * sizeof(float));
    sb = malloc(SCREEN_W * SCREEN_H);
    temp = malloc(sizeof(Vtx) * 200000);
    uint8_t px[64 * 4]; memset(px, 255, sizeof px);
    gfx_tex_create_rgba(&white, 8, 8, px, 0);
    return 0;
}
void gfx_shutdown(void) {}
Vtx *gfx_static_vertices(int count) { return malloc(sizeof(Vtx) * (size_t)count); }
int gfx_tex_create_p8(GfxTex *t, int w, int h, const uint8_t *pal, const uint8_t *idx, int repeat) {
    SoftTex *s = calloc(1, sizeof *s);
    s->w = w; s->h = h; s->p8 = 1; s->repeat = repeat;
    s->pal = malloc(1024); memcpy(s->pal, pal, 1024);
    s->data = malloc((size_t)w * h); memcpy(s->data, idx, (size_t)w * h);
    t->w = w; t->h = h; t->handle = s; return 0;
}
int gfx_tex_create_rgba(GfxTex *t, int w, int h, const uint8_t *rgba, int lin) {
    (void)lin;
    SoftTex *s = calloc(1, sizeof *s);
    s->w = w; s->h = h; s->p8 = 0;
    s->data = malloc((size_t)w * h * 4); memcpy(s->data, rgba, (size_t)w * h * 4);
    t->w = w; t->h = h; t->handle = s; return 0;
}
void gfx_begin_frame(float r, float g, float b) {
    uint32_t c = (uint32_t)(r * 255) | ((uint32_t)(g * 255) << 8) | ((uint32_t)(b * 255) << 16) | 0xFF000000u;
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) { fb[i] = c; zb[i] = 1.0f; sb[i] = 0; }
    temp_used = 0;
}
Vtx *gfx_temp_vertices(int count) {
    if (temp_used + count > 200000) return NULL;
    Vtx *p = temp + temp_used; temp_used += count; return p;
}
static inline void sample(const SoftTex *t, float u, float v, uint8_t out[4]) {
    int x = (int)floorf(u * t->w), y = (int)floorf(v * t->h);
    if (t->repeat) { x %= t->w; if (x < 0) x += t->w; y %= t->h; if (y < 0) y += t->h; }
    else { x = x < 0 ? 0 : (x >= t->w ? t->w - 1 : x); y = y < 0 ? 0 : (y >= t->h ? t->h - 1 : y); }
    if (t->p8) memcpy(out, t->pal + t->data[y * t->w + x] * 4, 4);
    else memcpy(out, t->data + (size_t)(y * t->w + x) * 4, 4);
}
typedef struct { float x, y, z, w, u, v; } CV;
static void raster(CV a, CV b, CV c, const SoftTex *t, const float tint[4], int mode) {
    // perspective divide (ortho: w=1)
    CV p[3] = {a, b, c};
    float sx[3], sy[3], sz[3];
    for (int i = 0; i < 3; i++) {
        float iw = 1.0f / p[i].w;
        sx[i] = (p[i].x * iw * 0.5f + 0.5f) * SCREEN_W;
        sy[i] = (-p[i].y * iw * 0.5f + 0.5f) * SCREEN_H;
        sz[i] = p[i].z * iw * 0.5f + 0.5f;
    }
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (fabsf(area) < 1e-6f) return;
    int x0 = (int)floorf(fminf(sx[0], fminf(sx[1], sx[2]))), x1 = (int)ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2])));
    int y0 = (int)floorf(fminf(sy[0], fminf(sy[1], sy[2]))), y1 = (int)ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2])));
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > SCREEN_W) x1 = SCREEN_W; if (y1 > SCREEN_H) y1 = SCREEN_H;
    float ia = 1.0f / area;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * ia;
            float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * ia;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
            int i = y * SCREEN_W + x;
            if (mode != GFX_UI && z > zb[i]) continue;
            if (mode == GFX_SHADOW) { if (sb[i]) continue; sb[i] = 1; }
            float u = w0 * p[0].u + w1 * p[1].u + w2 * p[2].u, v = w0 * p[0].v + w1 * p[1].v + w2 * p[2].v;
            uint8_t s[4]; sample(t, u, v, s);
            float r = s[0] * tint[0], g = s[1] * tint[1], bb = s[2] * tint[2], al = s[3] / 255.0f * tint[3];
            if (mode == GFX_OPAQUE) {
                fb[i] = (uint32_t)fminf(r, 255) | ((uint32_t)fminf(g, 255) << 8) | ((uint32_t)fminf(bb, 255) << 16) | 0xFF000000u;
                zb[i] = z;
            } else {
                uint32_t d = fb[i];
                float dr = d & 255, dg = (d >> 8) & 255, db = (d >> 16) & 255;
                r = r * al + dr * (1 - al); g = g * al + dg * (1 - al); bb = bb * al + db * (1 - al);
                fb[i] = (uint32_t)fminf(r, 255) | ((uint32_t)fminf(g, 255) << 8) | ((uint32_t)fminf(bb, 255) << 16) | 0xFF000000u;
            }
        }
}
long g_stat_draws, g_stat_verts;
void gfx_draw(const Vtx *v, int count, const GfxTex *tex, const M4 *mvp, const float tint[4], int mode) {
    g_stat_draws++; g_stat_verts += count;
    if (!tex) tex = &white;
    const SoftTex *t = tex->handle;
    const float *m = mvp->m;
    for (int i = 0; i + 2 < count; i += 3) {
        CV c[3];
        for (int k = 0; k < 3; k++) {
            const Vtx *p = &v[i + k];
            c[k].x = m[0] * p->x + m[4] * p->y + m[8] * p->z + m[12];
            c[k].y = m[1] * p->x + m[5] * p->y + m[9] * p->z + m[13];
            c[k].z = m[2] * p->x + m[6] * p->y + m[10] * p->z + m[14];
            c[k].w = m[3] * p->x + m[7] * p->y + m[11] * p->z + m[15];
            c[k].u = p->u; c[k].v = p->v;
        }
        // trivial clip reject
        int out = 1;
        for (int k = 0; k < 3; k++) if (c[k].x >= -c[k].w && c[k].x <= c[k].w && c[k].y >= -c[k].w && c[k].y <= c[k].w) out = 0;
        if (out) {
            float mnx = fminf(c[0].x, fminf(c[1].x, c[2].x)), mxx = fmaxf(c[0].x, fmaxf(c[1].x, c[2].x));
            float mny = fminf(c[0].y, fminf(c[1].y, c[2].y)), mxy = fmaxf(c[0].y, fmaxf(c[1].y, c[2].y));
            if (mxx < -1 || mnx > 1 || mxy < -1 || mny > 1) continue;
        }
        raster(c[0], c[1], c[2], t, tint, mode);
    }
}
void gfx_end_frame(void) {
    static long fr, maxd, maxv, sumd, sumv;
    fr++; sumd += g_stat_draws; sumv += g_stat_verts;
    if (g_stat_draws > maxd) maxd = g_stat_draws;
    if (g_stat_verts > maxv) maxv = g_stat_verts;
    if (fr % 300 == 0) fprintf(stderr, "frames=%ld draws avg %ld max %ld | verts avg %ld max %ld\n", fr, sumd / fr, maxd, sumv / fr, maxv);
    g_stat_draws = g_stat_verts = 0;
}
