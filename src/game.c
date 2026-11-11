// Smashy Road: Wanted - gameplay, AI, menus
#include "common.h"
#include "plat.h"
#include "gfx.h"
#include "assets.h"
#include "render.h"
#include "world.h"
#include "audio.h"
#include "gamedata.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef DESKTOP
#include <time.h>
#endif

uint32_t g_rng = 0x1234567;
#define DT (1.0f / 60.0f)

// ------------------------------------------------------------------ save
typedef struct {
    uint32_t magic, version, coins, unlocked, sel, best_time, best_cops, total_runs, music, sfx;
} Save;
static Save sv;
static void save_write(void) {
    FILE *f = fopen(plat_save_path(), "wb");
    if (f) { fwrite(&sv, sizeof sv, 1, f); fclose(f); }
}
static void save_load(void) {
    memset(&sv, 0, sizeof sv);
    FILE *f = fopen(plat_save_path(), "rb");
    if (f) { if (fread(&sv, sizeof sv, 1, f) != 1) memset(&sv, 0, sizeof sv); fclose(f); }
    if (sv.magic != 0x534D5348 || sv.version != 1) {
        memset(&sv, 0, sizeof sv);
        sv.magic = 0x534D5348; sv.version = 1; sv.unlocked = 1; sv.sel = 0; sv.coins = 0; sv.music = 1; sv.sfx = 1;
    }
    if (sv.sel >= NUM_PLAYERCARS || !(sv.unlocked & (1u << sv.sel))) sv.sel = 0;
    sv.unlocked |= 1;
}

// ------------------------------------------------------------------ vehicles
enum { K_PLAYER, K_POLICE, K_SWAT, K_JEEP, K_TANK, K_TRAFFIC, K_HELI_P, K_HELI_A, K_BLOCK, K_COUNT };
typedef struct {
    int used, kind, model, alive;
    float x, z, y, yaw, vx, vz;
    float hx, hz, h, ox, oy, oz;          // half extents, model offset
    float hp, maxhp, maxspd, acc, grip, turn, mass;
    float steer, thr, hb;
    float stuck, rev, fire, t, hit_t, dead_t, smoke_t;
    float pitch, roll, vpitch, vroll, vy;
    int ti, tj, tdir;                     // traffic route
    float alt;                            // helicopters
} Car;
#define MAXCARS 48
static Car cars[MAXCARS];
static Car *player;

typedef struct { int used, kind; V3 p, v; float life; } Proj;
#define MAXPROJ 24
static Proj projs[MAXPROJ];

// ------------------------------------------------------------------ game state
enum { ST_TITLE, ST_GARAGE, ST_PRIZE, ST_PLAY, ST_PAUSE, ST_OVER };
static int state = ST_TITLE;
static float run_time, star_flash, over_t, title_t, demo_yaw;
static int stars, cops_destroyed, run_coins, last_stars;
static float block_t, traffic_t, spawn_t;
static float cam_x, cam_z, cam_zoom = 8.0f;
static int v_engine = -1, v_skid = -1, v_siren = -1, v_heli = -1, v_scrape = -1;
static int garage_sel, pause_sel;
static float prize_t; static int prize_result, prize_dup, prize_spin_idx, prize_revealed; static float prize_click_t;
static char banner[64]; static float banner_t;
static Input in;
static float global_t;

static const float WHITE[4] = {1, 1, 1, 1};
static const float YELLOW[4] = {1, 0.85f, 0.2f, 1};
static const float RED[4] = {1, 0.25f, 0.2f, 1};

static float car_stat(int n, int which) {
    uint32_t h = hash32(n * 2654435761u + 17);
    const VehDef *d = &PLAYERCARS[n];
    float len = d->hi[2] - d->lo[2], wid = d->hi[0] - d->lo[0];
    float size = len * wid;
    switch (which) {
    case 0: return 15.5f + (h % 6) * 0.9f - size * 0.35f + (n >= 20 ? 2.0f : 0);  // max speed
    case 1: return 70 + size * 22 + ((h >> 8) % 4) * 6 + (n >= 26 ? 30 : 0);     // health
    default: return 3.2f - size * 0.12f + ((h >> 16) % 3) * 0.12f;               // turn rate
    }
}

static void setup_model(Car *c, const VehDef *d) {
    c->model = d->model;
    c->hx = (d->hi[0] - d->lo[0]) * 0.5f; c->hz = (d->hi[2] - d->lo[2]) * 0.5f; c->h = d->hi[1] - d->lo[1];
    c->ox = -(d->lo[0] + d->hi[0]) * 0.5f; c->oz = -(d->lo[2] + d->hi[2]) * 0.5f; c->oy = -d->lo[1];
}

static Car *car_new(int kind) {
    for (int i = 0; i < MAXCARS; i++) if (!cars[i].used) {
        Car *c = &cars[i];
        memset(c, 0, sizeof *c);
        c->used = 1; c->alive = 1; c->kind = kind;
        c->grip = 9; c->mass = 1; c->acc = 14; c->turn = 2.8f;
        switch (kind) {
        case K_POLICE: setup_model(c, &COP_police); c->maxhp = c->hp = 40; c->maxspd = 15.5f + stars * 0.4f; c->acc = 13; break;
        case K_SWAT: setup_model(c, &COP_swat); c->maxhp = c->hp = 75; c->maxspd = 14.0f; c->mass = 1.8f; c->acc = 11; break;
        case K_JEEP: setup_model(c, &COP_armyJeep); c->maxhp = c->hp = 60; c->maxspd = 17.5f; c->mass = 1.4f; c->acc = 15; break;
        case K_TANK: setup_model(c, &COP_tank); c->maxhp = c->hp = 220; c->maxspd = 8.0f; c->mass = 6; c->acc = 7; c->turn = 1.6f; c->fire = 2.5f; break;
        case K_HELI_P: setup_model(c, &COP_policeHeli); c->maxhp = c->hp = 1; c->alt = 9; break;
        case K_HELI_A: setup_model(c, &COP_armyHeli); c->maxhp = c->hp = 1; c->alt = 10; c->fire = 3; break;
        case K_BLOCK: c->maxhp = c->hp = 45; c->mass = 50; c->maxspd = 0; break;
        case K_TRAFFIC: {
            int n = rnd() % NUM_TRAFFIC;
            setup_model(c, &TRAFFICCARS[n]); c->maxhp = c->hp = 25; c->maxspd = 7.5f + frnd() * 2; c->acc = 6; c->mass = 0.9f;
        } break;
        default: break;
        }
        return c;
    }
    return NULL;
}

static int is_cop(const Car *c) { return c->kind >= K_POLICE && c->kind <= K_TANK; }
static int is_heli(const Car *c) { return c->kind == K_HELI_P || c->kind == K_HELI_A; }

// ------------------------------------------------------------------ effects
static void explosion(float x, float z, float y, float radius, float dmg, Car *src);
static void soft_hit(float x, float z, int def) {
    (void)def;
    for (int k = 0; k < 14; k++)
        parts_spawn(v3(x + frange(-1.5f, 1.5f), frange(0.5f, 2.5f), z + frange(-1.5f, 1.5f)),
                    v3(frange(-4, 4), frange(3, 8), frange(-4, 4)), frange(0.3f, 0.6f), (rnd() & 1) ? 6 : 7, frange(0.6f, 1.2f), 18);
    snd_play(S_POP, 0.6f, frange(0.9f, 1.2f));
}

static void damage(Car *c, float d, Car *by) {
    if (!c->alive || d <= 0) return;
    if (c == player && by && is_cop(by)) d *= 1.0f + stars * 0.08f;
    c->hp -= d;
    c->hit_t = 0.15f;
    if (c->hp <= 0) {
        c->hp = 0; c->alive = 0; c->dead_t = 0;
        c->vy = 6 + frnd() * 3; c->vpitch = frange(-4, 4); c->vroll = frange(-5, 5);
        explosion(c->x, c->z, 0.8f, 5.0f, 22, c);
        if (is_cop(c) || c->kind == K_BLOCK) {
            cops_destroyed++;
            if (c->kind != K_BLOCK) { run_coins += 1; }
        }
        if (c == player) g_cam.shake = 1.0f;
    }
}

static void explosion(float x, float z, float y, float radius, float dmg, Car *src) {
    for (int k = 0; k < 28; k++) {
        int hue = k < 8 ? 10 : (k < 18 ? 4 : 5);
        parts_spawn(v3(x + frange(-1, 1), y + frange(0, 1), z + frange(-1, 1)), v3(frange(-6, 6), frange(4, 12), frange(-6, 6)),
                    frange(0.4f, 1.0f), hue, frange(0.4f, 1.0f), 14);
    }
    for (int k = 0; k < 14; k++)
        parts_spawn(v3(x + frange(-1.5f, 1.5f), y + frange(0.5f, 2), z + frange(-1.5f, 1.5f)), v3(frange(-1.5f, 1.5f), frange(2, 4), frange(-1.5f, 1.5f)),
                    frange(1.2f, 2.2f), (rnd() & 1) ? 1 : 2, frange(1.0f, 2.0f), -0.5f);
    float dx = player->x - x, dz = player->z - z;
    float vol = clampf(1.2f - sqrtf(dx * dx + dz * dz) / 50.0f, 0.15f, 1.0f);
    snd_play(S_BOOM, vol, frange(0.85f, 1.1f));
    if (sqrtf(dx * dx + dz * dz) < 25) g_cam.shake = fmaxf(g_cam.shake, 0.5f);
    for (int i = 0; i < MAXCARS; i++) {
        Car *c = &cars[i];
        if (!c->used || !c->alive || c == src || is_heli(c)) continue;
        float ex = c->x - x, ez = c->z - z, d = sqrtf(ex * ex + ez * ez);
        if (d > radius) continue;
        float k = 1 - d / radius;
        if (d > 0.01f) { c->vx += ex / d * 14 * k / c->mass; c->vz += ez / d * 14 * k / c->mass; }
        damage(c, dmg * k, src);
    }
}

// ------------------------------------------------------------------ physics
static void car_circles(const Car *c, float *px, float *pz, float *r, int *n) {
    float s = sinf(c->yaw), co = cosf(c->yaw);
    float fx = -s, fz = -co, rx = co, rz = -s;
    if (c->hz >= c->hx) {
        int k = (int)(c->hz / c->hx + 0.5f); if (k < 1) k = 1; if (k > 6) k = 6;
        *r = c->hx * 1.02f; *n = k;
        for (int i = 0; i < k; i++) {
            float o = k == 1 ? 0 : -(c->hz - c->hx) + 2 * (c->hz - c->hx) * i / (k - 1);
            px[i] = c->x + fx * o; pz[i] = c->z + fz * o;
        }
    } else {
        int k = (int)(c->hx / c->hz + 0.5f); if (k < 1) k = 1; if (k > 12) k = 12;
        *r = c->hz * 1.02f; *n = k;
        for (int i = 0; i < k; i++) {
            float o = k == 1 ? 0 : -(c->hx - c->hz) + 2 * (c->hx - c->hz) * i / (k - 1);
            px[i] = c->x + rx * o; pz[i] = c->z + rz * o;
        }
    }
}

static void car_step(Car *c) {
    if (is_heli(c) || c->kind == K_BLOCK) return;
    if (c->alive) {
        float spd = c->vx * -sinf(c->yaw) + c->vz * -cosf(c->yaw);
        float rate = c->steer * c->turn * clampf(fabsf(spd) / 3.5f, 0, 1) * (spd >= 0 ? 1 : -1);
        rate *= 1.0f - 0.3f * clampf(fabsf(spd) / 22.0f, 0, 1);
        if (c->hb > 0) rate *= 1.35f;
        c->yaw = wrap_angle(c->yaw + rate * DT);
    } else {
        c->yaw += (c->vx * 0.02f) * DT;
    }
    float s = sinf(c->yaw), co = cosf(c->yaw);
    float fx = -s, fz = -co, rx = co, rz = -s;
    float vf = c->vx * fx + c->vz * fz, vr = c->vx * rx + c->vz * rz;
    if (c->alive) {
        float dmgf = c->kind == K_PLAYER ? (0.75f + 0.25f * c->hp / c->maxhp) : 1.0f;
        float top = c->maxspd * dmgf;
        if (c->thr > 0) {
            if (vf < top) vf += c->acc * c->thr * DT * (vf < 0 ? 2.0f : 1.0f);
            else vf -= (vf - top) * 2.5f * DT;
        } else if (c->thr < 0) {
            if (vf > 0.5f) vf -= c->acc * 1.8f * DT * -c->thr;
            else if (vf > -top * 0.45f) vf += c->acc * 0.7f * c->thr * DT;
        }
        vf -= vf * 0.12f * DT;
        float g = c->grip * (c->hb > 0 ? 0.18f : 1.0f);
        if (fabsf(vf) > c->maxspd * 0.85f && c->kind == K_PLAYER) g *= 0.75f;
        vr *= expf(-g * DT);
    } else {
        vf *= expf(-1.6f * DT); vr *= expf(-3.0f * DT);
    }
    c->vx = fx * vf + rx * vr; c->vz = fz * vf + rz * vr;
    c->x += c->vx * DT; c->z += c->vz * DT;
    if (!c->alive) {
        c->vy -= 22 * DT; c->y += c->vy * DT;
        c->pitch += c->vpitch * DT; c->roll += c->vroll * DT;
        if (c->y < 0) { c->y = 0; c->vy = -c->vy * 0.3f; c->vpitch *= 0.5f; c->vroll *= 0.5f;
            if (fabsf(c->vy) < 1) { c->vy = 0; c->pitch = roundf(c->pitch / (PI_F / 2)) * (PI_F / 2) * 0 + c->pitch * 0.9f; c->roll = roundf(c->roll / PI_F) * PI_F; } }
    }
    // buildings
    float px[12], pz[12], r; int n;
    car_circles(c, px, pz, &r, &n);
    for (int i = 0; i < n; i++) {
        float x = px[i], z = pz[i], nx, nz;
        if (world_collide(&x, &z, r, &nx, &nz, (c->kind == K_PLAYER || is_cop(c)) ? soft_hit : NULL)) {
            float dx = x - px[i], dz = z - pz[i];
            c->x += dx; c->z += dz;
            for (int k = 0; k < n; k++) { px[k] += dx; pz[k] += dz; }
            float vn = c->vx * nx + c->vz * nz;
            if (vn < 0) {
                c->vx -= nx * vn * 1.25f; c->vz -= nz * vn * 1.25f;
                float imp = -vn;
                float thr_imp = c->kind == K_PLAYER ? 4.0f : 7.0f;
                if (imp > thr_imp) {
                    damage(c, (imp - thr_imp) * (c->kind == K_PLAYER ? 0.9f : 0.6f), NULL);
                    if (c == player || fabsf(c->x - player->x) + fabsf(c->z - player->z) < 30)
                        snd_play(S_IMPACT, clampf(imp / 16, 0.2f, 1), frange(0.9f, 1.1f));
                    for (int k = 0; k < 4; k++)
                        parts_spawn(v3(px[i] - nx * r, 0.6f, pz[i] - nz * r), v3(frange(-3, 3), frange(2, 5), frange(-3, 3)), 0.25f, 11, 0.5f, 15);
                }
                // scrape: lose some forward speed and twist
                float tang = c->vx * -nz + c->vz * nx;
                c->yaw += clampf(tang * 0.002f, -0.03f, 0.03f);
            }
        }
    }
}

static void collide_cars(void) {
    for (int a = 0; a < MAXCARS; a++) {
        Car *A = &cars[a];
        if (!A->used || is_heli(A)) continue;
        for (int b = a + 1; b < MAXCARS; b++) {
            Car *B = &cars[b];
            if (!B->used || is_heli(B)) continue;
            float lim = fmaxf(A->hz, A->hx) + fmaxf(B->hz, B->hx) + 0.5f;
            if (fabsf(A->x - B->x) > lim || fabsf(A->z - B->z) > lim) continue;
            if (A->kind == K_BLOCK && B->kind == K_BLOCK) continue;
            float ax[12], az[12], ar, bx[12], bz[12], br; int an, bn;
            car_circles(A, ax, az, &ar, &an);
            car_circles(B, bx, bz, &br, &bn);
            float best = 0, nx = 0, nz = 0;
            for (int i = 0; i < an; i++)
                for (int j = 0; j < bn; j++) {
                    float dx = bx[j] - ax[i], dz = bz[j] - az[i], d2 = dx * dx + dz * dz, rr = ar + br;
                    if (d2 < rr * rr) {
                        float d = sqrtf(d2), pen = rr - d;
                        if (pen > best) { best = pen; if (d > 1e-4f) { nx = dx / d; nz = dz / d; } else { nx = 1; nz = 0; } }
                    }
                }
            if (best <= 0) continue;
            float ma = A->mass, mb = B->mass, tot = ma + mb;
            A->x -= nx * best * mb / tot; A->z -= nz * best * mb / tot;
            B->x += nx * best * ma / tot; B->z += nz * best * ma / tot;
            float rv = (B->vx - A->vx) * nx + (B->vz - A->vz) * nz;
            if (rv < 0) {
                float j = -(1 + 0.35f) * rv / (1 / ma + 1 / mb);
                A->vx -= nx * j / ma; A->vz -= nz * j / ma;
                B->vx += nx * j / mb; B->vz += nz * j / mb;
                float imp = -rv;
                if (imp > 3.0f) {
                    float base = (imp - 3.0f);
                    damage(A, base * 1.3f * mb / tot * (A->kind == K_PLAYER ? 1.0f : 1.7f), B);
                    damage(B, base * 1.3f * ma / tot * (B->kind == K_PLAYER ? 1.0f : 1.7f), A);
                    if (A == player || B == player || imp > 8) {
                        snd_play(S_IMPACT, clampf(imp / 14, 0.25f, 1.0f), frange(0.85f, 1.15f));
                        if (A == player || B == player) g_cam.shake = fmaxf(g_cam.shake, clampf(imp / 40, 0, 0.4f));
                    }
                    for (int k = 0; k < 5; k++)
                        parts_spawn(v3((A->x + B->x) * 0.5f, 0.7f, (A->z + B->z) * 0.5f), v3(frange(-4, 4), frange(2, 6), frange(-4, 4)),
                                    0.25f, (rnd() & 1) ? 1 : 11, 0.6f, 15);
                }
            }
        }
    }
}

// ------------------------------------------------------------------ AI
static void ai_cop(Car *c) {
    if (!c->alive) { c->thr = 0; c->steer = 0; return; }
    float dx = player->x - c->x, dz = player->z - c->z, dist = sqrtf(dx * dx + dz * dz);
    float look = clampf(dist / 18.0f, 0, 1.0f);
    float tx = player->x + player->vx * look - c->x, tz = player->z + player->vz * look - c->z;
    float want = atan2f(-tx, -tz);
    float diff = wrap_angle(want - c->yaw);
    float steer = clampf(diff * 2.4f, -1, 1);
    float s = sinf(c->yaw), co = cosf(c->yaw);
    float fx = -s, fz = -co;
    float sa = sinf(c->yaw + 0.55f), ca = cosf(c->yaw + 0.55f), sb = sinf(c->yaw - 0.55f), cb = cosf(c->yaw - 0.55f);
    float range = 6.5f + fabsf(c->vx * fx + c->vz * fz) * 0.25f;
    float dfw = world_ray(c->x, c->z, fx, fz, range);
    float dl = world_ray(c->x, c->z, -sa, -ca, range * 0.8f);
    float dr = world_ray(c->x, c->z, -sb, -cb, range * 0.8f);
    if (dist > 7) {
        if (dfw < range) steer += (dl > dr ? 1.0f : -1.0f) * (1.0f - dfw / range) * 1.8f;
        if (dl < range * 0.5f) steer -= 0.6f;
        if (dr < range * 0.5f) steer += 0.6f;
    }
    c->steer = clampf(steer, -1, 1);
    c->thr = 1.0f;
    if (fabsf(diff) > 2.3f && dist < 9) c->thr = 0.5f;
    float spd = sqrtf(c->vx * c->vx + c->vz * c->vz);
    if (c->rev > 0) {
        c->rev -= DT; c->thr = -1; c->steer = -c->steer;
    } else if (spd < 1.2f) {
        c->stuck += DT;
        if (c->stuck > 0.9f) { c->rev = 0.8f + frnd() * 0.4f; c->stuck = 0; }
    } else c->stuck = 0;
    if (c->kind == K_TANK) {
        c->fire -= DT;
        if (c->fire <= 0 && dist < 34 && dist > 6) {
            c->fire = 3.2f - stars * 0.15f;
            for (int i = 0; i < MAXPROJ; i++) if (!projs[i].used) {
                Proj *p = &projs[i];
                float t = dist / 26.0f;
                float ax = player->x + player->vx * t - c->x, az = player->z + player->vz * t - c->z, al = sqrtf(ax * ax + az * az) + 1e-3f;
                p->used = 1; p->kind = 0; p->life = 2.0f;
                p->p = v3(c->x + ax / al * 2.5f, 1.1f, c->z + az / al * 2.5f);
                p->v = v3(ax / al * 26, 0, az / al * 26);
                snd_play(S_BOOM, 0.5f, 1.5f);
                for (int k = 0; k < 5; k++) parts_spawn(p->p, v3(frange(-1, 1), frange(1, 2), frange(-1, 1)), 0.6f, 1, 0.6f, -0.5f);
                break;
            }
        }
    }
}

static void ai_heli(Car *c) {
    c->t += DT;
    float ang = c->t * 0.6f + (c->kind == K_HELI_A ? 2.0f : 0);
    float tx = player->x + cosf(ang) * 7, tz = player->z + sinf(ang) * 7;
    float k = 1 - expf(-1.5f * DT);
    c->vx = (tx - c->x) * 1.5f; c->vz = (tz - c->z) * 1.5f;
    c->x += (tx - c->x) * k; c->z += (tz - c->z) * k;
    c->y = c->alt + sinf(c->t * 1.7f) * 0.3f;
    c->yaw = atan2f(-(player->x - c->x), -(player->z - c->z));
    if (c->kind == K_HELI_A) {
        c->fire -= DT;
        if (c->fire <= 0) {
            c->fire = 3.6f - stars * 0.12f;
            for (int i = 0; i < MAXPROJ; i++) if (!projs[i].used) {
                Proj *p = &projs[i];
                V3 tgt = v3(player->x + player->vx * 0.8f, 0, player->z + player->vz * 0.8f);
                V3 d = v3norm(v3sub(tgt, v3(c->x, c->y, c->z)));
                p->used = 1; p->kind = 1; p->life = 3; p->p = v3(c->x, c->y - 0.5f, c->z); p->v = v3mul(d, 24);
                snd_play(S_ROCKET, 0.7f, 1.0f);
                break;
            }
        }
    }
}

static const int DX4[4] = {0, 1, 0, -1}, DZ4[4] = {-1, 0, 1, 0};
static int pick_dir(int mask, int from) {
    int opts[4], n = 0;
    for (int d = 0; d < 4; d++) if ((mask & (1 << d)) && d != ((from + 2) & 3)) opts[n++] = d;
    if (!n) return (from + 2) & 3;
    return opts[rnd() % n];
}
static void traffic_waypoint(const Car *c, float *wx, float *wz) {
    int d = c->tdir;
    float cx = c->ti * TILE, cz = c->tj * TILE;
    float rx = -DZ4[d], rz = DX4[d];   // right of travel direction
    *wx = cx + DX4[d] * TILE * 0.5f + rx * 3.0f;
    *wz = cz + DZ4[d] * TILE * 0.5f + rz * 3.0f;
}
static void ai_traffic(Car *c) {
    if (!c->alive) { c->thr = 0; return; }
    float wx, wz;
    traffic_waypoint(c, &wx, &wz);
    float dx = wx - c->x, dz = wz - c->z;
    if (dx * dx + dz * dz < 16) {
        c->ti += DX4[c->tdir]; c->tj += DZ4[c->tdir];
        c->tdir = pick_dir(world_mask(c->ti, c->tj), c->tdir);
        traffic_waypoint(c, &wx, &wz);
        dx = wx - c->x; dz = wz - c->z;
    }
    float want = atan2f(-dx, -dz), diff = wrap_angle(want - c->yaw);
    c->steer = clampf(diff * 2.0f, -1, 1);
    c->thr = c->hit_t > 0 || c->hp < c->maxhp * 0.5f ? 0.0f : 1.0f;
    if (c->hp < c->maxhp) c->thr = c->hp < c->maxhp * 0.5f ? 0 : 0.6f;
    // brake for things ahead
    float s = sinf(c->yaw), co = cosf(c->yaw), fx = -s, fz = -co;
    for (int i = 0; i < MAXCARS; i++) {
        Car *o = &cars[i];
        if (!o->used || o == c || is_heli(o)) continue;
        float ox = o->x - c->x, oz = o->z - c->z;
        float ahead = ox * fx + oz * fz, side = fabsf(ox * co + oz * -s);
        if (ahead > 0 && ahead < 6 && side < 2.0f) { c->thr = -0.6f; break; }
    }
}

// ------------------------------------------------------------------ spawning
static int count_kind(int k) { int n = 0; for (int i = 0; i < MAXCARS; i++) if (cars[i].used && cars[i].alive && cars[i].kind == k) n++; return n; }
static int offscreen(float x, float z) { return !cam_visible(v3(x - 3, 0, z - 3), v3(x + 3, 3, z + 3)); }

static void spawn_cop(int kind) {
    float x, z;
    for (int tries = 0; tries < 6; tries++) {
        if (!world_find_road_near(player->x, player->z, 30, 46, &x, &z, 12)) continue;
        if (!offscreen(x, z)) continue;
        Car *c = car_new(kind);
        if (!c) return;
        c->x = x; c->z = z;
        c->yaw = atan2f(-(player->x - x), -(player->z - z));
        return;
    }
}
static void spawn_heli(int kind) {
    Car *c = car_new(kind);
    if (!c) return;
    float a = frange(0, 2 * PI_F);
    c->x = player->x + cosf(a) * 45; c->z = player->z + sinf(a) * 45; c->y = c->alt;
}
static void spawn_traffic(void) {
    float x, z;
    if (!world_find_road_near(player->x, player->z, 28, 55, &x, &z, 10) || !offscreen(x, z)) return;
    int i = tile_of(x), j = tile_of(z), m = world_mask(i, j);
    if (!m) return;
    Car *c = car_new(K_TRAFFIC);
    if (!c) return;
    int d = pick_dir(m, rnd() & 3);
    c->ti = i; c->tj = j; c->tdir = d;
    float rx = -DZ4[d], rz = DX4[d];
    c->x = i * TILE + rx * 3.0f - DX4[d] * 8; c->z = j * TILE + rz * 3.0f - DZ4[d] * 8;
    if (world_solid_at(c->x, c->z, 1.5f)) { c->used = 0; return; }
    c->yaw = atan2f(-(float)DX4[d], -(float)DZ4[d]);
}
static void spawn_roadblock(void) {
    float s = sinf(player->yaw), co = cosf(player->yaw);
    float fx = -s, fz = -co;
    for (float dist = 40; dist <= 70; dist += 10) {
        int i = tile_of(player->x + fx * dist), j = tile_of(player->z + fz * dist);
        int m = world_mask(i, j);
        if (m != 5 && m != 10) continue;
        float x = i * TILE, z = j * TILE;
        if (!offscreen(x, z)) continue;
        for (int k = 0; k < MAXCARS; k++) if (cars[k].used && cars[k].kind == K_BLOCK && fabsf(cars[k].x - x) < 1 && fabsf(cars[k].z - z) < 1) return;
        Car *c = car_new(K_BLOCK);
        if (!c) return;
        const VehDef *d = stars >= 6 ? &COP_roadBlockTank : (stars >= 5 ? &COP_roadBlockJeep : (stars >= 3 ? &COP_roadBlockSwat : &COP_roadBlockPolice));
        setup_model(c, d);
        c->x = x; c->z = z; c->yaw = (m == 5) ? 0 : PI_F * 0.5f;
        return;
    }
}

typedef struct { int police, swat, jeep, tank, hp, ha; } Wave;
static const Wave WAVES[8] = {
    {0, 0, 0, 0, 0, 0}, {2, 0, 0, 0, 0, 0}, {3, 0, 0, 0, 0, 0}, {3, 1, 0, 0, 0, 0},
    {3, 2, 0, 0, 1, 0}, {2, 2, 2, 0, 1, 0}, {1, 2, 3, 1, 0, 1}, {1, 1, 3, 2, 1, 1}};

static void update_spawns(void) {
    spawn_t -= DT;
    if (spawn_t <= 0) {
        spawn_t = 0.6f;
        const Wave *w = &WAVES[stars];
        if (count_kind(K_POLICE) < w->police) spawn_cop(K_POLICE);
        else if (count_kind(K_SWAT) < w->swat) spawn_cop(K_SWAT);
        else if (count_kind(K_JEEP) < w->jeep) spawn_cop(K_JEEP);
        else if (count_kind(K_TANK) < w->tank) spawn_cop(K_TANK);
        else if (count_kind(K_HELI_P) < w->hp) spawn_heli(K_HELI_P);
        else if (count_kind(K_HELI_A) < w->ha) spawn_heli(K_HELI_A);
    }
    traffic_t -= DT;
    if (traffic_t <= 0) { traffic_t = 0.5f; if (count_kind(K_TRAFFIC) < 9) spawn_traffic(); }
    if (stars >= 2) {
        block_t -= DT;
        if (block_t <= 0) { block_t = 16 - stars; spawn_roadblock(); }
    }
    // despawn far / finished wrecks
    for (int i = 0; i < MAXCARS; i++) {
        Car *c = &cars[i];
        if (!c->used || c == player) continue;
        float dx = c->x - player->x, dz = c->z - player->z, d2 = dx * dx + dz * dz;
        if (d2 > 90 * 90) c->used = 0;
        else if (!c->alive && c->dead_t > 7 && offscreen(c->x, c->z)) c->used = 0;
        else if (!c->alive && c->dead_t > 14) c->used = 0;
    }
}

// ------------------------------------------------------------------ projectiles
static void update_projs(void) {
    for (int i = 0; i < MAXPROJ; i++) {
        Proj *p = &projs[i];
        if (!p->used) continue;
        p->life -= DT;
        p->p = v3add(p->p, v3mul(p->v, DT));
        if (((int)(p->life * 60)) % 2 == 0) parts_spawn(p->p, v3(0, 0.5f, 0), 0.35f, p->kind ? 1 : 11, 0.5f, -0.2f);
        int boom = p->life <= 0 || p->p.y <= 0.3f || world_solid_at(p->p.x, p->p.z, 0.2f);
        for (int k = 0; k < MAXCARS && !boom; k++) {
            Car *c = &cars[k];
            if (!c->used || !c->alive || is_heli(c) || c->kind == K_TANK) continue;
            float dx = c->x - p->p.x, dz = c->z - p->p.z;
            if (dx * dx + dz * dz < (c->hz + 0.5f) * (c->hz + 0.5f) && p->p.y < c->h + 0.5f) boom = 1;
        }
        if (boom) { explosion(p->p.x, p->p.z, fmaxf(p->p.y, 0.5f), 5.0f, 34, NULL); p->used = 0; }
    }
}

// ------------------------------------------------------------------ run control
static void start_run(void) {
    memset(cars, 0, sizeof cars);
    memset(projs, 0, sizeof projs);
    parts_clear();
    world_reset(hash32((uint32_t)(plat_time() * 1000) ^ (sv.total_runs * 7919u)) | 1);
    g_rng ^= hash32(sv.total_runs + 99);
    player = car_new(K_PLAYER);
    int n = sv.sel;
    setup_model(player, &PLAYERCARS[n]);
    player->maxspd = car_stat(n, 0); player->maxhp = player->hp = car_stat(n, 1); player->turn = car_stat(n, 2);
    player->acc = 15; player->grip = 8.5f; player->mass = 1.0f + (player->hx * player->hz - 0.6f) * 0.5f;
    player->x = 0; player->z = 8; player->yaw = 0;
    cam_x = player->x; cam_z = player->z; cam_zoom = 8;
    run_time = 0; stars = 1; last_stars = 1; cops_destroyed = 0; run_coins = 0;
    block_t = 12; traffic_t = 0; spawn_t = 2.0f; banner_t = 0;
    state = ST_PLAY;
    sv.total_runs++;
    for (int k = 0; k < 6; k++) spawn_traffic();
}

static void end_run(void) {
    state = ST_OVER; over_t = 0;
    snd_stop_all_loops();
    v_engine = v_skid = v_siren = v_heli = v_scrape = -1;
    sv.coins += run_coins;
    if ((uint32_t)run_time > sv.best_time) sv.best_time = (uint32_t)run_time;
    if ((uint32_t)cops_destroyed > sv.best_cops) sv.best_cops = cops_destroyed;
    save_write();
}

// ------------------------------------------------------------------ update
static int touch_in(float x, float y, float w, float h) {
    return in.touch_tap && in.tap_x >= x && in.tap_x <= x + w && in.tap_y >= y && in.tap_y <= y + h;
}

static void update_play(void) {
    if (in.pressed & BTN_START) { state = ST_PAUSE; pause_sel = 0; snd_stop_all_loops(); v_engine = v_skid = v_siren = v_heli = v_scrape = -1; snd_play(S_BTN1, 0.8f, 1); return; }
    if (touch_in(SCREEN_W - 70, 70, 60, 60)) { state = ST_PAUSE; pause_sel = 0; snd_stop_all_loops(); v_engine = v_skid = v_siren = v_heli = v_scrape = -1; return; }
    run_time += DT;
    // controls
    float steer = -in.lx;
    if (in.held & BTN_LEFT) steer = 1;
    if (in.held & BTN_RIGHT) steer = -1;
    int tl = 0, tr = 0;
    for (int i = 0; i < in.touch_n; i++) { if (in.ty[i] < 130 && in.tx[i] > SCREEN_W - 90) continue; if (in.tx[i] < SCREEN_W / 2) tl = 1; else tr = 1; }
    float thr = 1.0f;
    if (tl && tr) thr = -1;
    else if (tl) steer = 1;
    else if (tr) steer = -1;
    if (in.held & (BTN_SQUARE | BTN_L | BTN_DOWN)) thr = -1;
    if (player->alive) {
        player->steer = steer; player->thr = thr;
        player->hb = (in.held & BTN_R) ? 1.0f : 0.0f;
        if (in.pressed & BTN_CIRCLE) snd_play(S_HORN, 0.8f, 1);
    } else { player->thr = 0; player->steer = 0; }

    // wanted level
    int target = 1 + (int)(run_time / 24.0f) + cops_destroyed / 5;
#ifdef DESKTOP
    if (getenv("SMASHY_STARS")) target = atoi(getenv("SMASHY_STARS"));
#endif
    if (target > 7) target = 7;
    if (target > stars) { stars = target; star_flash = 1.5f; snd_play(S_BEEP, 0.9f, 1); snprintf(banner, sizeof banner, "WANTED LEVEL %d", stars); banner_t = 2.0f; }

    for (int i = 0; i < MAXCARS; i++) {
        Car *c = &cars[i];
        if (!c->used) continue;
        c->t += DT;
        if (c->hit_t > 0) c->hit_t -= DT;
        if (!c->alive) {
            c->dead_t += DT; c->smoke_t -= DT;
            if (c->smoke_t <= 0 && c->dead_t < 6) {
                c->smoke_t = 0.12f;
                parts_spawn(v3(c->x + frange(-.5f, .5f), c->y + 1.0f, c->z + frange(-.5f, .5f)), v3(frange(-.5f, .5f), 2.5f, frange(-.5f, .5f)), frange(0.8f, 1.4f), 2, 1.6f, -0.4f);
                if (c->dead_t < 2.5f) parts_spawn(v3(c->x, c->y + 0.8f, c->z), v3(frange(-1, 1), 3, frange(-1, 1)), 0.6f, 4, 0.5f, -0.3f);
            }
        }
        if (is_cop(c)) ai_cop(c);
        else if (c->kind == K_TRAFFIC) ai_traffic(c);
        else if (is_heli(c)) ai_heli(c);
        car_step(c);
    }
    collide_cars();
    update_projs();
    update_spawns();
    parts_update(DT);

    // player effects
    float s = sinf(player->yaw), co = cosf(player->yaw);
    float vf = player->vx * -s + player->vz * -co, vr = player->vx * co + player->vz * -s;
    float spd = sqrtf(player->vx * player->vx + player->vz * player->vz);
    int got = world_collect_coins(player->x, player->z, 1.8f);
    if (got) {
        run_coins += got;
        snd_play(S_COIN, 0.8f, 1);
        for (int k = 0; k < 8; k++) parts_spawn(v3(player->x, 1.2f, player->z), v3(frange(-3, 3), frange(3, 6), frange(-3, 3)), 0.25f, 3, 0.6f, 12);
    }
    if (player->alive) {
        if (fabsf(vr) > 4.5f) {
            if (((int)(run_time * 60)) % 3 == 0)
                parts_spawn(v3(player->x + co * player->hx * (rnd() & 1 ? 1 : -1) + s * player->hz, 0.2f, player->z - s * player->hx + co * player->hz),
                            v3(frange(-.5f, .5f), 1, frange(-.5f, .5f)), 0.5f, 11, 0.7f, -0.2f);
        }
        if (player->hp < player->maxhp * 0.4f && ((int)(run_time * 60)) % 6 == 0)
            parts_spawn(v3(player->x + s * -player->hz * 0.6f, 0.9f, player->z + co * -player->hz * 0.6f), v3(0, 2, 0), 0.7f, player->hp < player->maxhp * 0.2f ? 2 : 1, 1.0f, -0.4f);
        snd_loop(&v_engine, S_ENGINE, 0.28f, 0.65f + clampf(fabsf(vf) / player->maxspd, 0, 1.2f) * 0.8f);
        snd_loop(&v_skid, S_SKID, clampf((fabsf(vr) - 4) / 6, 0, 0.6f), 1.0f);
    } else {
        snd_loop(&v_engine, S_ENGINE, 0, 1);
        snd_loop(&v_skid, S_SKID, 0, 1);
    }
    float nearest = 1e9f; int helis = 0;
    for (int i = 0; i < MAXCARS; i++) {
        Car *c = &cars[i];
        if (!c->used || !c->alive) continue;
        if (is_cop(c)) { float dx = c->x - player->x, dz = c->z - player->z; float d = sqrtf(dx * dx + dz * dz); if (d < nearest) nearest = d; }
        if (is_heli(c)) helis++;
    }
    snd_loop(&v_siren, S_SIREN, clampf(1.0f - nearest / 45.0f, 0, 0.45f), 1.0f);
    snd_loop(&v_heli, S_HELI, helis ? 0.35f : 0, 1.0f);

    // camera
    float lx = player->x + player->vx * 0.35f, lz = player->z + player->vz * 0.35f;
    float k = 1 - expf(-5 * DT);
    cam_x += (lx - cam_x) * k; cam_z += (lz - cam_z) * k;
    cam_zoom += ((8.0f + clampf(spd - 8, 0, 14) * 0.17f) - cam_zoom) * (1 - expf(-1.5f * DT));
    if (g_cam.shake > 0) g_cam.shake = fmaxf(0, g_cam.shake - DT * 1.5f);
    if (star_flash > 0) star_flash -= DT;
    if (banner_t > 0) banner_t -= DT;

    if (!player->alive && player->dead_t > 2.2f) end_run();
#ifdef DESKTOP
    if (((int)(run_time * 60)) % 120 == 0) {
        int alive = 0; float md = 1e9f;
        for (int i = 0; i < MAXCARS; i++) if (cars[i].used && is_cop(&cars[i]) && cars[i].alive) { alive++; float dx = cars[i].x - player->x, dz = cars[i].z - player->z; md = fminf(md, sqrtf(dx*dx+dz*dz)); }
        dbg_log("t=%.1f stars=%d cops=%d nearest=%.1f traffic=%d destroyed=%d hp=%.0f/%.0f pos=%.1f,%.1f spd=%.1f", run_time, stars, alive, md, count_kind(K_TRAFFIC), cops_destroyed, player->hp, player->maxhp, player->x, player->z, spd);
    }
#endif
}

// ------------------------------------------------------------------ drawing
static void draw_car(const Car *c) {
    float col[4] = {1, 1, 1, 1};
    if (c->hit_t > 0) { col[0] = 1.6f; col[1] = 1.4f; col[2] = 1.4f; }
    if (!c->alive) { float d = 0.35f; col[0] = col[1] = col[2] = d; }
    V3 off = v3(c->ox, c->oy, c->oz);
    if (is_heli(c)) {
        M4 w = m4_mul(m4_trs_ypr(v3(c->x, c->y, c->z), c->yaw, -0.12f, 0, 1), m4_trs_y(v3(c->ox, -1.0f, c->oz), 0, 1));
        draw_model_ex(c->model, w, c->yaw, col, GFX_OPAQUE);
        shadow_box(v3(c->x, 0, c->z), c->hx * 0.8f, c->hz * 0.8f, 0.2f, c->yaw, 0.25f);
        return;
    }
    if (!cam_visible(v3(c->x - 7, 0, c->z - 7), v3(c->x + 7, 4, c->z + 7))) return;
    if (c->alive || (fabsf(c->pitch) < 0.01f && fabsf(c->roll) < 0.01f && c->y <= 0.001f)) {
        M4 w = m4_mul(m4_trs_y(v3(c->x, c->y, c->z), c->yaw, 1), m4_trs_y(off, 0, 1));
        draw_model(c->model, w, c->yaw, 0, col, 0);
    } else {
        M4 w = m4_mul(m4_trs_ypr(v3(c->x, c->y + c->h * 0.5f, c->z), c->yaw, c->pitch, c->roll, 1), m4_trs_y(v3(c->ox, c->oy - c->h * 0.5f, c->oz), 0, 1));
        draw_model_ex(c->model, w, c->yaw, col, GFX_OPAQUE);
    }
    shadow_box(v3(c->x, 0, c->z), c->hx * 0.95f, c->hz * 0.95f, c->h * 0.85f, c->yaw, 0.3f);
    // police lights
    if (state == ST_PLAY && c->alive && (c->kind == K_POLICE || c->kind == K_SWAT)) {
        int ph = ((int)(c->t * 8)) & 1;
        float s = sinf(c->yaw), co = cosf(c->yaw);
        V3 top = v3(c->x, c->h + 0.1f, c->z);
        parts_spawn(v3(top.x + co * 0.3f, top.y, top.z - s * 0.3f), v3(0, 0, 0), 0.32f, ph ? 9 : 5, 0.02f, 0);
        parts_spawn(v3(top.x - co * 0.3f, top.y, top.z + s * 0.3f), v3(0, 0, 0), 0.32f, ph ? 5 : 9, 0.02f, 0);
    }
}

static void draw_world_scene(float tx, float tz, float zoom) {
    cam_update(v3(tx, 0, tz), zoom);
    world_draw(g_cam.target);
    world_draw_coins(global_t);
    for (int i = 0; i < MAXCARS; i++) if (cars[i].used && !is_heli(&cars[i])) draw_car(&cars[i]);
    for (int i = 0; i < MAXPROJ; i++) if (projs[i].used) {
        static const float c[4] = {1, 1, 1, 1};
        M4 w = m4_trs_y(projs[i].p, 0, projs[i].kind ? 2.5f : 2.0f);
        draw_model_ex(BULLET_DEF.model, w, 0, c, GFX_OPAQUE);
    }
    shadows_flush();
    for (int i = 0; i < MAXCARS; i++) if (cars[i].used && is_heli(&cars[i])) draw_car(&cars[i]);
    shadows_flush();
    parts_draw();
}

static void draw_coins_hud(void) {
    char b[32];
    snprintf(b, sizeof b, "%u", (unsigned)(sv.coins + (state == ST_PLAY || state == ST_PAUSE ? run_coins : 0)));
    float w = ui_text_width(b, 1.0f);
    ui_image(&g_img[IM_CASH], SCREEN_W - 20 - w - 46, 14, 40, 40, NULL);
    ui_text_shadow(b, SCREEN_W - 20, 18, 1.0f, YELLOW, 2);
}

static void draw_stars(void) {
    for (int i = 0; i < 7; i++) {
        float x = 16 + i * 46, y = 12;
        int on = i < stars;
        const Image *im = &g_img[on ? (((int)(global_t * 6) + i) & 1 ? IM_SIREN_R : IM_SIREN_B) : IM_SIREN_B];
        float col[4] = {1, 1, 1, on ? 1.0f : 0.3f};
        float sc = (on && star_flash > 0 && i == stars - 1) ? 1.0f + 0.3f * sinf(star_flash * 12) : 1.0f;
        float w = 40, h = im->w ? 40.0f * im->h / im->w : 10;
        ui_image(im, x + 20 - w * 0.5f * sc, y + 14 - h * 0.5f * sc, w * sc, h * sc, col);
    }
}

static void draw_play_hud(void) {
    ui_begin();
    draw_stars();
    draw_coins_hud();
    char b[64];
    int t = (int)run_time;
    snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    ui_text_shadow(b, SCREEN_W / 2, 16, 1.1f, WHITE, 1);
    // health
    float hp = player->hp / player->maxhp;
    const float bg[4] = {0, 0, 0, 0.55f};
    ui_rect(16, SCREEN_H - 34, 220, 18, bg);
    float hc[4] = {1 - hp, 0.2f + 0.7f * hp, 0.2f, 1};
    ui_rect(19, SCREEN_H - 31, 214 * clampf(hp, 0, 1), 12, hc);
    snprintf(b, sizeof b, "x%d", cops_destroyed);
    ui_text_shadow(b, 250, SCREEN_H - 44, 0.8f, WHITE, 0);
    ui_image(&g_img[IM_PAUSE], SCREEN_W - 66, 70, 48, 48, NULL);
    if (banner_t > 0) {
        float a = clampf(banner_t, 0, 1);
        float c[4] = {1, 0.3f, 0.25f, a};
        ui_text_shadow(banner, SCREEN_W / 2, 150, 1.3f, c, 1);
    }
    if (run_time < 4) {
        float a = clampf(4 - run_time, 0, 1);
        float c[4] = {1, 1, 1, a};
        ui_text_shadow("STEER: STICK / D-PAD / TOUCH SIDES", SCREEN_W / 2, SCREEN_H - 120, 0.6f, c, 1);
        ui_text_shadow("SQUARE / L: BRAKE + REVERSE    R: DRIFT", SCREEN_W / 2, SCREEN_H - 90, 0.6f, c, 1);
    }
}

static int button(const Image *im, const Image *imd, float x, float y, float w, float h, const char *label, int focus) {
    ui_image(focus ? imd : im, x, y, w, h, NULL);
    if (label) {
        ui_text_shadow(label, x + w / 2, y + h + 6, 0.7f, focus ? YELLOW : WHITE, 1);
    }
    return touch_in(x, y, w, h);
}

static void demo_scene(void) {
    // idle city flyover used behind menus
    title_t += DT;
    demo_yaw += DT * 0.05f;
    float x = sinf(demo_yaw) * 60, z = title_t * 3.0f;
    if (!player) {
        memset(cars, 0, sizeof cars);
        world_reset(0xC0FFEE);
        player = car_new(K_PLAYER);
        player->alive = 1; player->x = 0; player->z = 0;
    }
    setup_model(player, &PLAYERCARS[sv.sel]);
    player->used = 0;
    draw_world_scene(x, z, 10);
}

static void update_title(void) {
    if (in.pressed & BTN_CROSS) { snd_play(S_BTN1, 0.8f, 1); start_run(); return; }
    if (in.pressed & BTN_TRIANGLE) { snd_play(S_BTN1, 0.8f, 1); state = ST_GARAGE; garage_sel = sv.sel; return; }
    if (in.pressed & BTN_SQUARE) {
        if (sv.coins >= 100) { sv.coins -= 100; state = ST_PRIZE; prize_t = 0; prize_result = -1; snd_play(S_BTN2, 0.8f, 1); }
        else snd_play(S_ERROR, 0.8f, 1);
    }
}
static void draw_title(void) {
    demo_scene();
    ui_begin();
    const float dim[4] = {0, 0, 0, 0.35f};
    ui_rect(0, 0, SCREEN_W, SCREEN_H, dim);
    float bob = sinf(global_t * 2) * 4;
    ui_text_shadow("SMASHY ROAD", SCREEN_W / 2, 70 + bob, 2.3f, WHITE, 1);
    ui_text_shadow("WANTED", SCREEN_W / 2, 150 + bob, 1.8f, RED, 1);
    draw_coins_hud();
    float bw = 110, y = 290;
    if (button(&g_img[IM_CAR], &g_img[IM_CARD], SCREEN_W / 2 - 260, y, bw, bw, "GARAGE /\\", 0)) { state = ST_GARAGE; garage_sel = sv.sel; snd_play(S_BTN1, 0.8f, 1); }
    if (button(&g_img[IM_PLAY], &g_img[IM_PLAYD], SCREEN_W / 2 - 75, y - 20, 150, 150, "PLAY X", 1)) { snd_play(S_BTN1, 0.8f, 1); start_run(); return; }
    if (button(&g_img[IM_PRIZE], &g_img[IM_PRIZED], SCREEN_W / 2 + 150, y, bw, bw, "100 COINS []", 0)) {
        if (sv.coins >= 100) { sv.coins -= 100; state = ST_PRIZE; prize_t = 0; prize_result = -1; snd_play(S_BTN2, 0.8f, 1); }
        else snd_play(S_ERROR, 0.8f, 1);
    }
    char b[64];
    snprintf(b, sizeof b, "BEST %d:%02d   COPS %u", sv.best_time / 60, sv.best_time % 60, sv.best_cops);
    ui_text_shadow(b, SCREEN_W / 2, SCREEN_H - 40, 0.6f, WHITE, 1);
}

static void update_garage(void) {
    int cols = 10;
    if (in.pressed & BTN_RIGHT) garage_sel = (garage_sel + 1) % NUM_PLAYERCARS;
    if (in.pressed & BTN_LEFT) garage_sel = (garage_sel + NUM_PLAYERCARS - 1) % NUM_PLAYERCARS;
    if (in.pressed & BTN_DOWN) garage_sel = (garage_sel + cols) % NUM_PLAYERCARS;
    if (in.pressed & BTN_UP) garage_sel = (garage_sel + NUM_PLAYERCARS - cols) % NUM_PLAYERCARS;
    if (in.pressed & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN)) snd_play(S_CLICK, 0.5f, 1);
    if (in.pressed & BTN_CROSS) {
        if (sv.unlocked & (1u << garage_sel)) { sv.sel = garage_sel; save_write(); snd_play(S_BTN1, 0.8f, 1); state = ST_TITLE; }
        else snd_play(S_ERROR, 0.8f, 1);
    }
    if (in.pressed & BTN_CIRCLE) { state = ST_TITLE; snd_play(S_BTN2, 0.8f, 1); }
}
static void draw_car_preview(int n, float cx, float cy, float zoom, float yaw) {
    // draw a car model in its own little showroom at a far-away spot
    V3 at = v3(5000, 0, 5000);
    M4 save_vp = g_cam.vp;
    float a = (float)SCREEN_W / SCREEN_H;
    // shift the projection so the car appears at (cx, cy) on screen
    float ndc_x = cx / SCREEN_W * 2 - 1, ndc_y = 1 - cy / SCREEN_H * 2;
    M4 view = m4_lookdir(v3sub(at, v3mul(g_cam.fwd, 40)), g_cam.fwd, v3(0, 1, 0));
    M4 proj = m4_ortho(-zoom * a, zoom * a, -zoom, zoom, 1, 300);
    M4 shift = m4_identity(); shift.m[12] = ndc_x; shift.m[13] = ndc_y;
    g_cam.vp = m4_mul(shift, m4_mul(proj, view));
    const VehDef *d = &PLAYERCARS[n];
    float col[4] = {1, 1, 1, 1};
    if (!(sv.unlocked & (1u << n)) && state != ST_PRIZE) { col[0] = col[1] = col[2] = 0.08f; }
    M4 w = m4_mul(m4_trs_y(at, yaw, 1), m4_trs_y(v3(-(d->lo[0] + d->hi[0]) * 0.5f, -d->lo[1], -(d->lo[2] + d->hi[2]) * 0.5f), 0, 1));
    draw_model_ex(d->model, w, yaw, col, GFX_OPAQUE);
    g_cam.vp = save_vp;
}
static void draw_garage(void) {
    demo_scene();
    ui_begin();
    const float dim[4] = {0, 0, 0, 0.6f};
    ui_rect(0, 0, SCREEN_W, SCREEN_H, dim);
    ui_text_shadow("GARAGE", 24, 14, 1.2f, WHITE, 0);
    draw_coins_hud();
    int cols = 10;
    float cell = 60, gx = 30, gy = 290;
    for (int n = 0; n < NUM_PLAYERCARS; n++) {
        float x = gx + (n % cols) * (cell + 28), y = gy + (n / cols) * (cell + 16);
        int own = (sv.unlocked >> n) & 1;
        const Image *box = &g_img[n == garage_sel ? IM_BOXG : (n == (int)sv.sel ? IM_BOXD : IM_BOX)];
        ui_image(box, x - 6, y - 6, cell + 12, cell + 12, NULL);
        { const VehDef *vd = &PLAYERCARS[n]; float len = fmaxf(vd->hi[2] - vd->lo[2], vd->hi[0] - vd->lo[0]);
          draw_car_preview(n, x + cell * 0.5f, y + cell * 0.55f, clampf(len * 4.2f, 8.0f, 20.0f), 0.7f + (n == garage_sel ? global_t : 0)); }
        if (touch_in(x - 6, y - 6, cell + 12, cell + 12)) {
            if (garage_sel == n && own) { sv.sel = n; save_write(); snd_play(S_BTN1, 0.8f, 1); state = ST_TITLE; }
            garage_sel = n;
        }
    }
    draw_car_preview(garage_sel, SCREEN_W / 2, 170, 2.6f, global_t * 0.8f);
    int own = (sv.unlocked >> garage_sel) & 1;
    char b[64];
    if (own) {
        snprintf(b, sizeof b, "SPEED %d  ARMOR %d  HANDLING %d", (int)(car_stat(garage_sel, 0) * 5), (int)car_stat(garage_sel, 1), (int)(car_stat(garage_sel, 2) * 30));
        ui_text_shadow(b, SCREEN_W / 2, 250, 0.6f, WHITE, 1);
    } else ui_text_shadow("LOCKED - WIN IT FROM THE PRIZE MACHINE", SCREEN_W / 2, 250, 0.6f, RED, 1);
    ui_text_shadow("X SELECT   O BACK", SCREEN_W - 24, 64, 0.6f, YELLOW, 2);
    if (button(&g_img[IM_BACK], &g_img[IM_BACKD], 20, 80, 70, 70, NULL, 0)) { state = ST_TITLE; }
}

static int roll_prize(void) {
    // common cars are more likely than the late, rare ones
    int total = 0, w[32];
    for (int n = 0; n < NUM_PLAYERCARS; n++) { w[n] = n < 20 ? 6 : (n < 26 ? 3 : 1); total += w[n]; }
    int r = rnd() % total;
    for (int n = 0; n < NUM_PLAYERCARS; n++) { if (r < w[n]) return n; r -= w[n]; }
    return 0;
}
static void update_prize(void) {
    prize_t += DT;
    if (prize_result < 0) {
        prize_result = roll_prize();
        prize_dup = (sv.unlocked >> prize_result) & 1;
        prize_click_t = 0; prize_revealed = 0;
    }
    if (prize_t < 3.0f) {
        prize_click_t -= DT;
        if (prize_click_t <= 0) {
            prize_click_t = 0.05f + prize_t * prize_t * 0.03f;
            prize_spin_idx = rnd() % NUM_PLAYERCARS;
            snd_play(S_CLICK, 0.6f, 1.0f + prize_t * 0.1f);
        }
    } else if (!prize_revealed) {
        prize_revealed = 1;
        prize_spin_idx = prize_result;
        if (prize_dup) { sv.coins += 40; snd_play(S_POP, 0.9f, 1); }
        else { sv.unlocked |= 1u << prize_result; sv.sel = prize_result; snd_play(S_REVEAL, 0.9f, 1); snd_play(S_CLAP, 0.7f, 1); }
        save_write();
        for (int k = 0; k < 40; k++) parts_spawn(v3(5000 + frange(-1, 1), 1, 5000 + frange(-1, 1)), v3(frange(-5, 5), frange(5, 10), frange(-5, 5)), 0.3f, 3 + (rnd() % 8), 1.5f, 12);
    }
    if (prize_t > 3.5f && (in.pressed & (BTN_CROSS | BTN_CIRCLE) || in.touch_tap)) { state = prize_dup ? ST_TITLE : ST_GARAGE; garage_sel = prize_result; }
}
static void draw_prize(void) {
    demo_scene();
    ui_begin();
    const float dim[4] = {0, 0, 0, 0.65f};
    ui_rect(0, 0, SCREEN_W, SCREEN_H, dim);
    ui_text_shadow("PRIZE MACHINE", SCREEN_W / 2, 20, 1.2f, YELLOW, 1);
    int n = prize_spin_idx;
    ui_image(&g_img[IM_BOXG], SCREEN_W / 2 - 110, 110, 220, 220, NULL);
    draw_car_preview(n, SCREEN_W / 2, 225, prize_t >= 3.0f ? 2.6f : 3.2f, prize_t >= 3.0f ? global_t : 0.7f);
    if (!(sv.unlocked & (1u << n)) && prize_t < 3.0f) { /* silhouette while spinning */ }
    if (prize_t >= 3.0f) {
        ui_text_shadow(prize_dup ? "DUPLICATE! +40 COINS" : "NEW CAR UNLOCKED!", SCREEN_W / 2, 350, 0.9f, prize_dup ? WHITE : YELLOW, 1);
        ui_text_shadow("X CONTINUE", SCREEN_W / 2, SCREEN_H - 34, 0.6f, WHITE, 1);
    }
    draw_coins_hud();
}

static void update_pause(void) {
    if (in.pressed & (BTN_UP | BTN_DOWN)) { pause_sel ^= 1; snd_play(S_CLICK, 0.5f, 1); }
    if (in.pressed & BTN_START || ((in.pressed & BTN_CROSS) && pause_sel == 0) || (in.pressed & BTN_CIRCLE)) { state = ST_PLAY; snd_play(S_BTN1, 0.8f, 1); return; }
    if ((in.pressed & BTN_CROSS) && pause_sel == 1) { player->hp = 0; player->alive = 0; end_run(); state = ST_TITLE; player = NULL; return; }
    if (touch_in(SCREEN_W / 2 - 150, 220, 300, 60)) { state = ST_PLAY; return; }
    if (touch_in(SCREEN_W / 2 - 150, 300, 300, 60)) { end_run(); state = ST_TITLE; player = NULL; return; }
}
static void draw_pause(void) {
    ui_begin();
    const float dim[4] = {0, 0, 0, 0.55f};
    ui_rect(0, 0, SCREEN_W, SCREEN_H, dim);
    ui_text_shadow("PAUSED", SCREEN_W / 2, 120, 1.6f, WHITE, 1);
    ui_text_shadow("RESUME", SCREEN_W / 2, 235, 1.0f, pause_sel == 0 ? YELLOW : WHITE, 1);
    ui_text_shadow("QUIT RUN", SCREEN_W / 2, 315, 1.0f, pause_sel == 1 ? YELLOW : WHITE, 1);
}

static void update_over(void) {
    over_t += DT;
    for (int i = 0; i < MAXCARS; i++) {
        Car *c = &cars[i];
        if (!c->used) continue;
        if (is_cop(c)) { c->thr = 0.3f; ai_cop(c); c->thr *= 0.3f; }
        else if (c->kind == K_TRAFFIC) ai_traffic(c);
        else if (is_heli(c)) ai_heli(c);
        if (!c->alive) c->dead_t += DT;
        car_step(c);
    }
    collide_cars();
    parts_update(DT);
    if (over_t > 1.0f) {
        if (in.pressed & BTN_CROSS || touch_in(SCREEN_W / 2 - 200, 380, 180, 70)) { start_run(); return; }
        if (in.pressed & BTN_CIRCLE || touch_in(SCREEN_W / 2 + 20, 380, 180, 70)) { state = ST_TITLE; player = NULL; }
    }
}
static void draw_over(void) {
    ui_begin();
    float a = clampf(over_t * 2, 0, 0.6f);
    const float dim[4] = {0.15f, 0, 0, a};
    ui_rect(0, 0, SCREEN_W, SCREEN_H, dim);
    ui_text_shadow("WASTED", SCREEN_W / 2, 90, 2.2f, RED, 1);
    char b[64];
    snprintf(b, sizeof b, "TIME %d:%02d", (int)run_time / 60, (int)run_time % 60);
    ui_text_shadow(b, SCREEN_W / 2, 200, 1.0f, WHITE, 1);
    snprintf(b, sizeof b, "COPS SMASHED %d", cops_destroyed);
    ui_text_shadow(b, SCREEN_W / 2, 250, 1.0f, WHITE, 1);
    snprintf(b, sizeof b, "COINS +%d", run_coins);
    ui_text_shadow(b, SCREEN_W / 2, 300, 1.0f, YELLOW, 1);
    if (over_t > 1.0f) {
        ui_text_shadow("X RETRY", SCREEN_W / 2 - 110, 400, 0.9f, WHITE, 1);
        ui_text_shadow("O MENU", SCREEN_W / 2 + 110, 400, 0.9f, WHITE, 1);
    }
    draw_coins_hud();
}

// ------------------------------------------------------------------ main loop
int game_main(void) {
    plat_init();
    int r = gfx_init();
    if (r < 0) { dbg_log("gfx_init failed %d", r); return 1; }
    r = assets_load();
    if (r < 0) {
        dbg_log("assets_load failed %d - copy the data folder to %s", r, plat_data_dir());
        // show an error screen forever
        for (int f = 0; !plat_should_quit(); f++) {
            gfx_begin_frame(0.5f, 0.05f, 0.05f);
            gfx_end_frame();
            plat_present_hook(f);
        }
        return 1;
    }
    save_load();
#ifdef DESKTOP
    if (getenv("SMASHY_COINS")) sv.coins = atoi(getenv("SMASHY_COINS"));
#endif
    audio_init();
    snd_set_volumes(sv.sfx ? 1.0f : 0, sv.music ? 0.8f : 0);
    snd_music(1);
    g_rng = hash32((uint32_t)(plat_time() * 1e6)) | 1;
    double last = plat_time(), acc = 0;
    int tap_latch = 0; float tap_lx = 0, tap_ly = 0;
    for (int frame = 0; !plat_should_quit(); frame++) {
        double now = plat_time();
        double dt = now - last; last = now;
        if (dt > 0.1) dt = 0.1;
        acc += dt;
        int steps = 0, nsteps = (int)(acc / DT + 0.5);
        if (nsteps > 3) nsteps = 3;
#ifdef DESKTOP
        nsteps = 1; acc = DT; // deterministic: exactly one step per frame
#endif
        acc -= nsteps * DT;
        if (acc < -DT * 0.5) acc = 0;
        while (steps < nsteps) {
            plat_poll(&in);
            if (in.touch_tap) { tap_latch = 1; tap_lx = in.tap_x; tap_ly = in.tap_y; }
            global_t += DT;
            switch (state) {
            case ST_TITLE: update_title(); break;
            case ST_GARAGE: update_garage(); break;
            case ST_PRIZE: update_prize(); parts_update(DT); break;
            case ST_PLAY: {
#ifdef DESKTOP
                struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
                update_play();
                clock_gettime(CLOCK_MONOTONIC, &t1);
                static double tot, mx; static int nn;
                double us = (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
                tot += us; nn++; if (us > mx) mx = us;
                if (nn % 300 == 0) dbg_log("update_play avg %.1f us max %.1f us", tot / nn, mx);
#else
                update_play();
#endif
            } break;
            case ST_PAUSE: update_pause(); break;
            case ST_OVER: update_over(); break;
            }
            in.pressed = 0; in.touch_tap = 0;
            steps++;
        }
        in.touch_tap = (state == ST_TITLE || state == ST_GARAGE) ? tap_latch : 0; in.tap_x = tap_lx; in.tap_y = tap_ly;
        tap_latch = 0;
        gfx_begin_frame(0.22f, 0.22f, 0.24f);
        switch (state) {
        case ST_TITLE: draw_title(); break;
        case ST_GARAGE: draw_garage(); break;
        case ST_PRIZE: draw_prize(); break;
        case ST_PLAY: draw_world_scene(cam_x, cam_z, cam_zoom); draw_play_hud(); break;
        case ST_PAUSE: draw_world_scene(cam_x, cam_z, cam_zoom); draw_play_hud(); draw_pause(); break;
        case ST_OVER: draw_world_scene(cam_x, cam_z, cam_zoom); draw_over(); break;
        }
        gfx_end_frame();
        plat_present_hook(frame);
    }
    gfx_shutdown();
    return 0;
}

int main(void) { return game_main(); }
