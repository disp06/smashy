// Thin rendering API: implemented by gfx_gxm.c (PS Vita, sceGxm) and gfx_soft.c (desktop test renderer)
#pragma once
#include "common.h"

typedef struct { float x, y, z, u, v; } Vtx; // 20 bytes, matches texture_v.gxp attributes

enum { GFX_OPAQUE = 0, GFX_BLEND = 1, GFX_UI = 2, GFX_SHADOW = 3 }; // SHADOW: blended, each pixel darkened once (stencil)

typedef struct {
    int w, h;          // allocated (power of two) size
    void *handle;      // backend data
} GfxTex;

int  gfx_init(void);
void gfx_shutdown(void);
// static memory (lives for whole run)
Vtx *gfx_static_vertices(int count);              // returns GPU-visible memory to fill
int  gfx_tex_create_p8(GfxTex *t, int w, int h, const uint8_t *pal_rgba, const uint8_t *idx, int repeat);
int  gfx_tex_create_rgba(GfxTex *t, int w, int h, const uint8_t *rgba, int linear_filter);
// per-frame
void gfx_begin_frame(float r, float g, float b);
Vtx *gfx_temp_vertices(int count);                 // valid for the current frame only (NULL when full)
void gfx_draw(const Vtx *v, int count, const GfxTex *tex, const M4 *mvp, const float tint[4], int mode);
void gfx_end_frame(void);
