#include "render.h"
#include "gamedata.h"
#include <stdlib.h>

Camera g_cam;
const V3 LIGHT_DIR = {-0.554f, -0.741f, 0.379f};
static const V3 CAM_FWD = {-0.5794f, -0.5744f, -0.5785f};
static const float AMB = 0.46f, SUN = 0.68f;
static const V3 DIRN[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

void cam_update(V3 target, float zoom) {
    g_cam.target = target; g_cam.zoom = zoom;
    g_cam.fwd = v3norm(CAM_FWD);
    V3 t = target;
    if (g_cam.shake > 0) { t.x += frange(-1, 1) * g_cam.shake; t.z += frange(-1, 1) * g_cam.shake; }
    V3 eye = v3sub(t, v3mul(g_cam.fwd, 120.0f));
    g_cam.view = m4_lookdir(eye, g_cam.fwd, v3(0, 1, 0));
    float a = (float)SCREEN_W / SCREEN_H;
    g_cam.proj = m4_ortho(-zoom * a, zoom * a, -zoom, zoom, 1.0f, 300.0f);
    g_cam.vp = m4_mul(g_cam.proj, g_cam.view);
}

int cam_visible(V3 lo, V3 hi) {
    const float *m = g_cam.vp.m;
    float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
    for (int i = 0; i < 8; i++) {
        float x = (i & 1) ? hi.x : lo.x, y = (i & 2) ? hi.y : lo.y, z = (i & 4) ? hi.z : lo.z;
        float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
        float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
        if (cx < mnx) mnx = cx; if (cx > mxx) mxx = cx;
        if (cy < mny) mny = cy; if (cy > mxy) mxy = cy;
    }
    return !(mxx < -1.02f || mnx > 1.02f || mxy < -1.02f || mny > 1.02f);
}

static int swap_tex(int tex, int biome) {
    if (biome <= 0) return tex;
    for (int k = 0; k < NUM_TEXSWAP; k++) if (TEXSWAP[k][0] == tex) return TEXSWAP[k][1 + biome];
    return tex;
}

void draw_model(int mi, M4 world, float yaw, int is_static, const float color[4], int biome) {
    if (mi < 0 || mi >= g_nmodels) return;
    const Model *m = &g_models[mi];
    M4 mvp = m4_mul(g_cam.vp, world);
    float c = cosf(yaw), s = sinf(yaw);
    for (int i = 0; i < m->nsub; i++) {
        const Sub *sb = &m->subs[i];
        if (sb->dir == 3) continue; // bottom faces are never visible
        V3 n = DIRN[sb->dir];
        V3 w = v3(c * n.x + s * n.z, n.y, -s * n.x + c * n.z);
        if (v3dot(w, g_cam.fwd) > 0.05f) continue; // faces away from the fixed camera
        (void)is_static;
        float sh = AMB + SUN * fmaxf(0, -v3dot(w, LIGHT_DIR));
        float tint[4] = {color[0] * sh, color[1] * sh, color[2] * sh, color[3]};
        gfx_draw(sb->v, sb->count, &g_tex[swap_tex(sb->tex, biome)], &mvp, tint, GFX_OPAQUE);
    }
}

// arbitrary orientation (tumbling wrecks): no face culling, shading from the matrix
void draw_model_ex(int mi, M4 world, float yaw, const float color[4], int mode) {
    (void)yaw;
    if (mi < 0 || mi >= g_nmodels) return;
    const Model *m = &g_models[mi];
    M4 mvp = m4_mul(g_cam.vp, world);
    for (int i = 0; i < m->nsub; i++) {
        const Sub *sb = &m->subs[i];
        V3 n = DIRN[sb->dir];
        V3 w = v3norm(v3(world.m[0] * n.x + world.m[4] * n.y + world.m[8] * n.z,
                         world.m[1] * n.x + world.m[5] * n.y + world.m[9] * n.z,
                         world.m[2] * n.x + world.m[6] * n.y + world.m[10] * n.z));
        if (v3dot(w, g_cam.fwd) > 0.3f) continue;
        float sh = AMB + SUN * fmaxf(0, -v3dot(w, LIGHT_DIR));
        float tint[4] = {color[0] * sh, color[1] * sh, color[2] * sh, color[3]};
        gfx_draw(sb->v, sb->count, &g_tex[sb->tex], &mvp, tint, mode);
    }
}

// ---------------------------------------------------------------- shadows
#define MAX_SHADOW_V 24000
static Vtx shadow_v[MAX_SHADOW_V]; static int shadow_n;
static float shadow_alpha = 0.33f;

void shadow_box(V3 c, float hx, float hz, float h, float yaw, float alpha) {
    (void)alpha;
    if (shadow_n + 24 > MAX_SHADOW_V || h <= 0.05f) return;
    float cs = cosf(yaw), sn = sinf(yaw);
    float ox = -LIGHT_DIR.x / LIGHT_DIR.y * h, oz = -LIGHT_DIR.z / LIGHT_DIR.y * h;
    float px[8], pz[8];
    const float sx[4] = {-1, 1, 1, -1}, sz[4] = {-1, -1, 1, 1};
    for (int i = 0; i < 4; i++) {
        float lx = sx[i] * hx, lz = sz[i] * hz;
        float wx = c.x + cs * lx + sn * lz, wz = c.z - sn * lx + cs * lz;
        px[i] = wx; pz[i] = wz;
        px[i + 4] = wx + ox; pz[i + 4] = wz + oz;
    }
    // convex hull (gift wrapping, 8 points)
    int hull[9], hn = 0, start = 0;
    for (int i = 1; i < 8; i++) if (px[i] < px[start] || (px[i] == px[start] && pz[i] < pz[start])) start = i;
    int p = start;
    do {
        hull[hn++] = p;
        int q = (p + 1) % 8;
        for (int r = 0; r < 8; r++) {
            float cr = (px[q] - px[p]) * (pz[r] - pz[p]) - (pz[q] - pz[p]) * (px[r] - px[p]);
            if (cr < 0) q = r;
        }
        p = q;
    } while (p != start && hn < 8);
    float y = 0.04f;
    for (int i = 1; i + 1 < hn; i++) {
        int a = hull[0], b = hull[i], d = hull[i + 1];
        Vtx *v = &shadow_v[shadow_n];
        v[0] = (Vtx){px[a], y, pz[a], 0.5f, 0.5f};
        v[1] = (Vtx){px[b], y, pz[b], 0.5f, 0.5f};
        v[2] = (Vtx){px[d], y, pz[d], 0.5f, 0.5f};
        shadow_n += 3;
    }
}

void shadows_flush(void) {
    if (!shadow_n) return;
    Vtx *t = gfx_temp_vertices(shadow_n);
    if (t) {
        memcpy(t, shadow_v, sizeof(Vtx) * shadow_n);
        float tint[4] = {0.05f, 0.05f, 0.12f, shadow_alpha};
        gfx_draw(t, shadow_n, &g_white, &g_cam.vp, tint, GFX_SHADOW);
    }
    shadow_n = 0;
}

// ---------------------------------------------------------------- particles
typedef struct { V3 p, v; float size, life, max, grav; int hue; } Part;
#define MAXP 700
static Part parts[MAXP]; static int nparts;

void parts_clear(void) { nparts = 0; }
void parts_spawn(V3 p, V3 v, float size, int hue, float life, float grav) {
    if (nparts >= MAXP) { parts[rnd() % MAXP] = parts[--nparts]; }
    Part *q = &parts[nparts++];
    q->p = p; q->v = v; q->size = size; q->hue = hue; q->life = q->max = life; q->grav = grav;
}
void parts_update(float dt) {
    for (int i = 0; i < nparts;) {
        Part *q = &parts[i];
        q->life -= dt;
        if (q->life <= 0) { parts[i] = parts[--nparts]; continue; }
        q->v.y -= q->grav * dt;
        q->p = v3add(q->p, v3mul(q->v, dt));
        if (q->p.y < q->size * 0.5f && q->grav > 0) { q->p.y = q->size * 0.5f; q->v.y *= -0.35f; q->v.x *= 0.7f; q->v.z *= 0.7f; }
        if (q->grav < 0) { q->v.x *= 0.98f; q->v.z *= 0.98f; }
        i++;
    }
}
void parts_draw(void) {
    if (!nparts) return;
    Vtx *v = gfx_temp_vertices(nparts * 18);
    if (!v) return;
    int n = 0;
    for (int i = 0; i < nparts; i++) {
        Part *q = &parts[i];
        float k = q->life / q->max;
        float s = q->size * (q->grav < 0 ? (0.4f + 0.6f * (1 - k)) * fminf(1, k * 3) : fminf(1, k * 2.5f)) * 0.5f;
        if (s < 0.01f) continue;
        float x0 = q->p.x - s, x1 = q->p.x + s, y0 = q->p.y - s, y1 = q->p.y + s, z0 = q->p.z - s, z1 = q->p.z + s;
        float vv = (q->hue + 0.5f) / 16.0f;
        float ut = 15.5f / 16, ux = 11.5f / 16, uz = 6.5f / 16;
        // top (+Y)
        Vtx f[18] = {
            {x0, y1, z0, ut, vv}, {x1, y1, z0, ut, vv}, {x1, y1, z1, ut, vv}, {x0, y1, z0, ut, vv}, {x1, y1, z1, ut, vv}, {x0, y1, z1, ut, vv},
            {x1, y0, z0, ux, vv}, {x1, y1, z0, ux, vv}, {x1, y1, z1, ux, vv}, {x1, y0, z0, ux, vv}, {x1, y1, z1, ux, vv}, {x1, y0, z1, ux, vv},
            {x0, y0, z1, uz, vv}, {x1, y0, z1, uz, vv}, {x1, y1, z1, uz, vv}, {x0, y0, z1, uz, vv}, {x1, y1, z1, uz, vv}, {x0, y1, z1, uz, vv}};
        memcpy(v + n, f, sizeof f);
        n += 18;
    }
    float tint[4] = {1, 1, 1, 1};
    gfx_draw(v, n, &g_palette_tex, &g_cam.vp, tint, GFX_OPAQUE);
}

void ground_quad(float x0, float z0, float x1, float z1, float y, const float col[4]) {
    Vtx *v = gfx_temp_vertices(6);
    if (!v) return;
    v[0] = (Vtx){x0, y, z0, .5f, .5f}; v[1] = (Vtx){x1, y, z0, .5f, .5f}; v[2] = (Vtx){x1, y, z1, .5f, .5f};
    v[3] = (Vtx){x0, y, z0, .5f, .5f}; v[4] = (Vtx){x1, y, z1, .5f, .5f}; v[5] = (Vtx){x0, y, z1, .5f, .5f};
    gfx_draw(v, 6, &g_white, &g_cam.vp, col, GFX_OPAQUE);
}

// ---------------------------------------------------------------- 2D
static M4 ui_m;
void ui_begin(void) { ui_m = m4_ortho(0, SCREEN_W, SCREEN_H, 0, -1, 1); }

static void quad(Vtx *v, float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
    v[0] = (Vtx){x, y, 0, u0, v0}; v[1] = (Vtx){x + w, y, 0, u1, v0}; v[2] = (Vtx){x + w, y + h, 0, u1, v1};
    v[3] = (Vtx){x, y, 0, u0, v0}; v[4] = (Vtx){x + w, y + h, 0, u1, v1}; v[5] = (Vtx){x, y + h, 0, u0, v1};
}
void ui_rect(float x, float y, float w, float h, const float col[4]) {
    Vtx *v = gfx_temp_vertices(6);
    if (!v) return;
    quad(v, x, y, w, h, .5f, .5f, .5f, .5f);
    gfx_draw(v, 6, &g_white, &ui_m, col, GFX_UI);
}
void ui_image(const Image *im, float x, float y, float w, float h, const float col[4]) {
    if (!im->tex.handle) return;
    Vtx *v = gfx_temp_vertices(6);
    if (!v) return;
    quad(v, x, y, w, h, 0, 0, im->u1, im->v1);
    static const float white[4] = {1, 1, 1, 1};
    gfx_draw(v, 6, &im->tex, &ui_m, col ? col : white, GFX_UI);
}
float ui_text_width(const char *s, float scale) {
    float w = 0;
    for (; *s; s++) { int c = (unsigned char)*s - 32; if (c < 0 || c >= 95) c = 0; w += g_glyph[c].adv * scale; }
    return w;
}
float ui_text(const char *s, float x, float y, float scale, const float col[4], int align) {
    int len = (int)strlen(s);
    if (!len || !g_img[IM_FONT].tex.handle) return 0;
    float w = ui_text_width(s, scale);
    if (align == 1) x -= w * 0.5f; else if (align == 2) x -= w;
    Vtx *v = gfx_temp_vertices(len * 6);
    if (!v) return w;
    int n = 0;
    float cell = (float)g_font_cell;
    for (int i = 0; i < len; i++) {
        int c = (unsigned char)s[i] - 32; if (c < 0 || c >= 95) c = 0;
        const Glyph *g = &g_glyph[c];
        if (c) { quad(v + n, x, y, cell * scale, cell * scale, g->x / 512.0f, g->y / 512.0f, (g->x + cell) / 512.0f, (g->y + cell) / 512.0f); n += 6; }
        x += g->adv * scale;
    }
    gfx_draw(v, n, &g_img[IM_FONT].tex, &ui_m, col, GFX_UI);
    return w;
}
float ui_text_shadow(const char *s, float x, float y, float scale, const float col[4], int align) {
    const float sh[4] = {0, 0, 0, col[3] * 0.85f};
    ui_text(s, x + 3 * scale, y + 3 * scale, scale, sh, align);
    return ui_text(s, x, y, scale, col, align);
}
