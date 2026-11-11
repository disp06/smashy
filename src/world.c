// Endless procedural city built from the original road tiles and building prefabs.
// Street grid: some columns/rows are streets; whole segments between junctions are removed
// in a way that never produces dead ends, so every road tile is one of the game's real shapes.
#include "world.h"
#include "render.h"
#include "gamedata.h"
#include <stdlib.h>

#define NCACHE 64
static Tile cache[NCACHE];
static uint32_t seed, stamp;

static int street_col(int i) { return (i % 3 == 0) || hashf(hash2i(i, 0x51, seed)) < 0.42f; }
static int street_row(int j) { return (j % 3 == 0) || hashf(hash2i(0x77, j, seed)) < 0.42f; }
static int prev_col(int i) { while (!street_col(i)) i--; return i; }
static int prev_row(int j) { while (!street_row(j)) j--; return j; }
static int next_row(int j) { while (!street_row(j)) j++; return j; }
static int near_start(int i, int j) { return abs(i) <= 1 && abs(j) <= 1; }

// row j segment starting at street column a (to the next street column)
static int row_cut(int j, int a) {
    if (near_start(a, j) || (abs(j) <= 1 && abs(a) <= 3)) return 0;
    return hashf(hash2i(a * 7 + 1, j * 13 + 5, seed ^ 0xA5A5)) < 0.24f;
}
static int h_arms(int i, int r) { // at a junction (street col i, street row r)
    return !row_cut(r, prev_col(i - 1)) + !row_cut(r, i);
}
// column i segment starting at street row c
static int col_cut(int i, int c) {
    if (abs(i) <= 3 && abs(c) <= 3) return 0;
    if (hashf(hash2i(i * 11 + 3, c * 5 + 9, seed ^ 0x3C3C)) >= 0.24f) return 0;
    int d = next_row(c + 1);
    return h_arms(i, c) == 2 && h_arms(i, d) == 2;
}

int world_mask(int i, int j) {
    int cs = street_col(i), rs = street_row(j), m = 0;
    if (rs) {
        if (cs) { if (!row_cut(j, prev_col(i - 1))) m |= 8; if (!row_cut(j, i)) m |= 2; }
        else if (!row_cut(j, prev_col(i))) m |= 8 | 2;
    }
    if (cs) {
        if (rs) { if (!col_cut(i, prev_row(j - 1))) m |= 1; if (!col_cut(i, j)) m |= 4; }
        else if (!col_cut(i, prev_row(j))) m |= 1 | 4;
    }
    return m;
}

int world_biome(int i, int j) {
    int bi = (int)floorf(i / 5.0f), bj = (int)floorf(j / 5.0f);
    if (abs(bi) + abs(bj) == 0) return 0;
    float h = hashf(hash2i(bi, bj, seed ^ 0xB10E));
    return h < 0.6f ? 0 : (h < 0.8f ? 1 : 2);
}

static float cell_c(int c) { return -TILE * 0.5f + CELL * 0.5f + CELL * c; }

static void gen_tile(Tile *t, int i, int j) {
    memset(t, 0, sizeof *t);
    t->i = i; t->j = j; t->used = 1;
    t->mask = world_mask(i, j);
    t->biome = world_biome(i, j);
    int m = t->mask;
    for (int cz = 0; cz < 8; cz++)
        for (int cx = 0; cx < 8; cx++) {
            int r = 0, inx = (cx == 3 || cx == 4), inz = (cz == 3 || cz == 4);
            if (m && inx && inz) r = 1;
            if ((m & 1) && inx && cz <= 4) r = 1;
            if ((m & 4) && inx && cz >= 3) r = 1;
            if ((m & 8) && inz && cx <= 4) r = 1;
            if ((m & 2) && inz && cx >= 3) r = 1;
            t->road[cz][cx] = (uint8_t)r;
        }
    uint32_t rs = hash2i(i, j, seed ^ 0x7777) | 1;
#define TR() (rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5, rs)
    uint8_t occ[8][8];
    memcpy(occ, t->road, sizeof occ);
    const short *small, *big, *rock; int ns, nbg, nr;
    if (t->biome == 1) { small = BL_gebouw1Woestijn; ns = NBL_gebouw1Woestijn; big = BL_gebouw4Woestijn; nbg = NBL_gebouw4Woestijn; rock = BL_gebouw1WoestijnRock; nr = NBL_gebouw1WoestijnRock; }
    else if (t->biome == 2) { small = BL_gebouw1Land; ns = NBL_gebouw1Land; big = BL_gebouw4Land; nbg = NBL_gebouw4Land; rock = BL_gebouw1LandRock; nr = NBL_gebouw1LandRock; }
    else { small = BL_gebouw1; ns = NBL_gebouw1; big = BL_gebouw4; nbg = NBL_gebouw4; rock = BL_gebouwrecht1; nr = NBL_gebouwrecht1; }
    int start_clear = (i == 0 && j == 0);
    for (int cz = 0; cz < 8; cz++)
        for (int cx = 0; cx < 8; cx++) {
            if (occ[cz][cx] || t->nb >= MAXB) continue;
            int def = -1;
            if (nbg && (TR() % 100) < 30) {
                int d = big[TR() % nbg];
                const BuildingDef *bd = &BUILDINGS[d];
                int ok = 1;
                for (int z = cz + bd->z0; z <= cz + bd->z1 && ok; z++)
                    for (int x = cx + bd->x0; x <= cx + bd->x1 && ok; x++)
                        if (x < 0 || x > 7 || z < 0 || z > 7 || occ[z][x]) ok = 0;
                if (ok) def = d;
            }
            if (def < 0) {
                if (nr && (TR() % 100) < 12) def = rock[TR() % nr];
                else def = small[TR() % ns];
                const BuildingDef *bd = &BUILDINGS[def];
                if (bd->x0 || bd->z0 || bd->x1 || bd->z1) def = small[0];
            }
            const BuildingDef *bd = &BUILDINGS[def];
            for (int z = cz + bd->z0; z <= cz + bd->z1; z++)
                for (int x = cx + bd->x0; x <= cx + bd->x1; x++)
                    if (x >= 0 && x < 8 && z >= 0 && z < 8) occ[z][x] = 1;
            if (start_clear && abs(cx - 3) <= 2 && abs(cz - 3) <= 2 && bd->height > 0.5f && !bd->soft) continue;
            Bld *b = &t->b[t->nb++];
            b->def = (short)def; b->cx = (signed char)cx; b->cz = (signed char)cz; b->alive = 1;
            b->x = i * TILE + cell_c(cx); b->z = j * TILE + cell_c(cz);
        }
    // coins on road cells
    if (m) {
        int want = 1 + (int)(TR() % 3);
        for (int k = 0; k < 20 && t->ncoin < want; k++) {
            int cx = TR() % 8, cz = TR() % 8;
            if (!t->road[cz][cx]) continue;
            t->coin_x[t->ncoin] = i * TILE + cell_c(cx) + ((int)(TR() % 5) - 2) * 0.8f;
            t->coin_z[t->ncoin] = j * TILE + cell_c(cz) + ((int)(TR() % 5) - 2) * 0.8f;
            t->coin_alive[t->ncoin] = !(i == 0 && j == 0 && abs(cx - 3) < 2 && abs(cz - 3) < 2);
            t->ncoin++;
        }
    }
#undef TR
}

void world_reset(uint32_t s) { seed = s; memset(cache, 0, sizeof cache); stamp = 0; }

Tile *world_tile(int i, int j) {
    stamp++;
    int worst = 0; uint32_t wst = 0xFFFFFFFF;
    for (int k = 0; k < NCACHE; k++) {
        Tile *t = &cache[k];
        if (t->used && t->i == i && t->j == j) { t->stamp = stamp; return t; }
        uint32_t st = t->used ? t->stamp : 0;
        if (st < wst) { wst = st; worst = k; }
    }
    Tile *t = &cache[worst];
    gen_tile(t, i, j);
    t->stamp = stamp;
    return t;
}

void world_draw(V3 c) {
    int ti = tile_of(c.x), tj = tile_of(c.z);
    static const float idc[4] = {1, 1, 1, 1};
    for (int dj = -2; dj <= 2; dj++)
        for (int di = -2; di <= 2; di++) {
            int i = ti + di, j = tj + dj;
            float x0 = i * TILE - TILE * 0.5f, z0 = j * TILE - TILE * 0.5f;
            if (!cam_visible(v3(x0, 0, z0), v3(x0 + TILE, 14, z0 + TILE))) continue;
            Tile *t = world_tile(i, j);
            const unsigned char *gc = BIOME_GROUND[t->biome];
            float col[4] = {gc[0] / 255.0f * 0.92f, gc[1] / 255.0f * 0.92f, gc[2] / 255.0f * 0.92f, 1};
            ground_quad(x0, z0, x0 + TILE, z0 + TILE, 0.0f, col);
            if (t->mask) {
                int mi = ROADTILE_BY_MASK[t->mask];
                if (mi >= 0) {
                    M4 w = m4_trs_y(v3(i * TILE, -0.06f, j * TILE), 0, 1);
                    draw_model(mi, w, 0, 1, idc, t->biome);
                }
            }
            for (int k = 0; k < t->nb; k++) {
                Bld *b = &t->b[k];
                if (!b->alive) continue;
                const BuildingDef *bd = &BUILDINGS[b->def];
                const Model *m = &g_models[bd->model];
                V3 lo = v3(b->x + m->lo[0], m->lo[1], b->z + m->lo[2]), hi = v3(b->x + m->hi[0], m->hi[1], b->z + m->hi[2]);
                float ox = -LIGHT_DIR.x / LIGHT_DIR.y * bd->height, oz = -LIGHT_DIR.z / LIGHT_DIR.y * bd->height;
                V3 slo = v3(fminf(lo.x, lo.x + ox), 0, fminf(lo.z, lo.z + oz)), shi = v3(fmaxf(hi.x, hi.x + ox), hi.y, fmaxf(hi.z, hi.z + oz));
                if (!cam_visible(slo, shi)) continue;
                if (cam_visible(lo, hi)) draw_model(bd->model, m4_trs_y(v3(b->x, 0, b->z), 0, 1), 0, 1, idc, 0);
                if (bd->height > 0.6f) {
                    if (bd->ncol) {
                        for (int c2 = 0; c2 < bd->ncol; c2++) {
                            const float *bc = BCOLS[bd->col0 + c2];
                            shadow_box(v3(b->x + (bc[0] + bc[3]) * 0.5f, 0, b->z + (bc[2] + bc[5]) * 0.5f),
                                       (bc[3] - bc[0]) * 0.5f, (bc[5] - bc[2]) * 0.5f, bc[4], 0, 0.3f);
                        }
                    } else if (!bd->soft || bd->height > 1.0f) {
                        shadow_box(v3(b->x + (m->lo[0] + m->hi[0]) * 0.5f, 0, b->z + (m->lo[2] + m->hi[2]) * 0.5f),
                                   (m->hi[0] - m->lo[0]) * 0.42f, (m->hi[2] - m->lo[2]) * 0.42f, bd->height * 0.9f, 0, 0.3f);
                    }
                }
            }
        }
}

int world_is_road(float x, float z) {
    int i = tile_of(x), j = tile_of(z);
    Tile *t = world_tile(i, j);
    int cx = (int)floorf((x - (i * TILE - TILE * 0.5f)) / CELL), cz = (int)floorf((z - (j * TILE - TILE * 0.5f)) / CELL);
    if (cx < 0 || cx > 7 || cz < 0 || cz > 7) return 0;
    return t->road[cz][cx];
}

// iterate building boxes near a point
typedef int (*BoxFn)(const Bld *b, const BuildingDef *bd, float lx, float lz, float hx, float hz, float top, void *u);
static int for_boxes(float x, float z, float rad, BoxFn fn, void *u) {
    int i0 = tile_of(x - rad - CELL * 2), i1 = tile_of(x + rad + CELL * 2);
    int j0 = tile_of(z - rad - CELL * 2), j1 = tile_of(z + rad + CELL * 2);
    int hit = 0;
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            Tile *t = world_tile(i, j);
            for (int k = 0; k < t->nb; k++) {
                Bld *b = &t->b[k];
                if (!b->alive) continue;
                if (fabsf(b->x - x) > rad + 16 || fabsf(b->z - z) > rad + 16) continue;
                const BuildingDef *bd = &BUILDINGS[b->def];
                if (bd->ncol) {
                    for (int c = 0; c < bd->ncol; c++) {
                        const float *bc = BCOLS[bd->col0 + c];
                        hit |= fn(b, bd, b->x + bc[0], b->z + bc[2], b->x + bc[3], b->z + bc[5], bc[4], u);
                    }
                } else if (bd->height > 0.6f) {
                    const Model *m = &g_models[bd->model];
                    float sx = bd->soft ? 0.35f : 0.5f;
                    float cxm = (m->lo[0] + m->hi[0]) * 0.5f, czm = (m->lo[2] + m->hi[2]) * 0.5f;
                    float ex = (m->hi[0] - m->lo[0]) * sx, ez = (m->hi[2] - m->lo[2]) * sx;
                    hit |= fn(b, bd, b->x + cxm - ex, b->z + czm - ez, b->x + cxm + ex, b->z + czm + ez, bd->height, u);
                }
            }
        }
    return hit;
}

typedef struct { float x, z, r, nx, nz; int pushed; SoftHitFn soft; } CollU;
static int coll_fn(const Bld *b, const BuildingDef *bd, float lx, float lz, float hx, float hz, float top, void *up) {
    CollU *u = up;
    if (top < 0.5f) return 0;
    float px = clampf(u->x, lx, hx), pz = clampf(u->z, lz, hz);
    float dx = u->x - px, dz = u->z - pz, d2 = dx * dx + dz * dz;
    if (d2 >= u->r * u->r) return 0;
    if (bd->soft) {
        Bld *mb = (Bld *)b;
        mb->alive = 0;
        if (u->soft) u->soft(b->x, b->z, b->def);
        return 0;
    }
    float d = sqrtf(d2), nx, nz, pen;
    if (d > 1e-4f) { nx = dx / d; nz = dz / d; pen = u->r - d; }
    else {
        // centre inside the box: push out along the smallest axis
        float l = u->x - lx, r = hx - u->x, f = u->z - lz, k = hz - u->z;
        float mn = fminf(fminf(l, r), fminf(f, k));
        if (mn == l) { nx = -1; nz = 0; } else if (mn == r) { nx = 1; nz = 0; } else if (mn == f) { nx = 0; nz = -1; } else { nx = 0; nz = 1; }
        pen = mn + u->r;
    }
    u->x += nx * pen; u->z += nz * pen;
    u->nx += nx; u->nz += nz; u->pushed = 1;
    return 1;
}

int world_collide(float *x, float *z, float r, float *nx, float *nz, SoftHitFn soft) {
    CollU u = {*x, *z, r, 0, 0, 0, soft};
    for_boxes(*x, *z, r, coll_fn, &u);
    *x = u.x; *z = u.z;
    float l = sqrtf(u.nx * u.nx + u.nz * u.nz);
    if (l > 1e-4f) { *nx = u.nx / l; *nz = u.nz / l; } else { *nx = *nz = 0; }
    return u.pushed;
}

typedef struct { float x, z, pad; } SolU;
static int sol_fn(const Bld *b, const BuildingDef *bd, float lx, float lz, float hx, float hz, float top, void *up) {
    (void)b;
    SolU *u = up;
    if (top < 0.5f || bd->soft) return 0;
    return u->x > lx - u->pad && u->x < hx + u->pad && u->z > lz - u->pad && u->z < hz + u->pad;
}
int world_solid_at(float x, float z, float pad) { SolU u = {x, z, pad}; return for_boxes(x, z, pad + 1, sol_fn, &u); }

float world_ray(float x, float z, float dx, float dz, float len) {
    for (float t = 1.0f; t <= len; t += 1.25f)
        if (world_solid_at(x + dx * t, z + dz * t, 0.6f)) return t;
    return len;
}

int world_collect_coins(float x, float z, float r) {
    int ti = tile_of(x), tj = tile_of(z), got = 0;
    for (int dj = -1; dj <= 1; dj++)
        for (int di = -1; di <= 1; di++) {
            Tile *t = world_tile(ti + di, tj + dj);
            for (int k = 0; k < t->ncoin; k++) {
                if (!t->coin_alive[k]) continue;
                float dx = t->coin_x[k] - x, dz = t->coin_z[k] - z;
                if (dx * dx + dz * dz < r * r) { t->coin_alive[k] = 0; got++; }
            }
        }
    return got;
}

void world_draw_coins(float time) {
    int ti = tile_of(g_cam.target.x), tj = tile_of(g_cam.target.z);
    static const float col[4] = {1, 1, 1, 1};
    for (int dj = -2; dj <= 2; dj++)
        for (int di = -2; di <= 2; di++) {
            Tile *t = world_tile(ti + di, tj + dj);
            for (int k = 0; k < t->ncoin; k++) {
                if (!t->coin_alive[k]) continue;
                V3 p = v3(t->coin_x[k], 1.0f + 0.25f * sinf(time * 3 + k), t->coin_z[k]);
                if (!cam_visible(v3(p.x - 1, 0, p.z - 1), v3(p.x + 1, 2, p.z + 1))) continue;
                float yaw = time * 3.0f + k;
                const Model *m = &g_models[COIN_DEF.model];
                V3 off = v3(-(m->lo[0] + m->hi[0]) * 0.5f, -(m->lo[1] + m->hi[1]) * 0.5f, -(m->lo[2] + m->hi[2]) * 0.5f);
                M4 w = m4_mul(m4_trs_y(p, yaw, 1), m4_trs_y(off, 0, 1));
                draw_model_ex(COIN_DEF.model, w, yaw, col, GFX_OPAQUE);
                shadow_box(v3(p.x, 0, p.z), 0.4f, 0.1f, 0.8f, yaw, 0.3f);
            }
        }
}

int world_find_road_near(float cx, float cz, float rmin, float rmax, float *ox, float *oz, int tries) {
    for (int k = 0; k < tries; k++) {
        float a = frange(0, 2 * PI_F), d = frange(rmin, rmax);
        float x = cx + cosf(a) * d, z = cz + sinf(a) * d;
        if (world_is_road(x, z) && !world_solid_at(x, z, 2.0f)) {
            // snap to lane centre of the cell
            *ox = x; *oz = z;
            return 1;
        }
    }
    return 0;
}
