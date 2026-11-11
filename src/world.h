#pragma once
#include "common.h"

#define TILE 51.2f
#define CELL 6.4f
#define MAXB 40

typedef struct { short def; signed char cx, cz; uint8_t alive; float x, z; } Bld;
typedef struct {
    int i, j, used, mask, biome;
    uint8_t road[8][8];    // [cz][cx] 1 = road cell
    int nb; Bld b[MAXB];
    int ncoin; float coin_x[4], coin_z[4]; uint8_t coin_alive[4];
    uint32_t stamp;
} Tile;

typedef void (*SoftHitFn)(float x, float z, int def);

void  world_reset(uint32_t seed);
Tile *world_tile(int i, int j);
int   world_mask(int i, int j);
int   world_biome(int i, int j);
void  world_draw(V3 center);
int   world_is_road(float x, float z);
// pushes a circle out of solid buildings; returns 1 if pushed (nx,nz = push normal)
int   world_collide(float *x, float *z, float r, float *nx, float *nz, SoftHitFn soft);
int   world_solid_at(float x, float z, float pad);
float world_ray(float x, float z, float dx, float dz, float len); // distance to first obstacle (len if none)
int   world_collect_coins(float x, float z, float r);
void  world_draw_coins(float t);
int   world_find_road_near(float cx, float cz, float rmin, float rmax, float *ox, float *oz, int tries);
static inline int tile_of(float x) { return (int)floorf((x + TILE * 0.5f) / TILE); }
