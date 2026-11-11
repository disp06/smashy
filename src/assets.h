#pragma once
#include "gfx.h"

typedef struct { int tex; int dir; Vtx *v; int count; } Sub;
typedef struct { float lo[3], hi[3]; int trig; } Box;
typedef struct { float lo[3], hi[3]; int nsub; Sub *subs; int ncol; Box *cols; } Model;
typedef struct { GfxTex tex; float u1, v1; int w, h; } Image;  // u1/v1: used part of the texture

extern Model *g_models; extern int g_nmodels;
extern GfxTex *g_tex; extern int g_ntex;
extern GfxTex g_white, g_palette_tex;

enum {
    IM_PLAY, IM_PLAYD, IM_CAR, IM_CARD, IM_PRIZE, IM_PRIZED, IM_BACK, IM_BACKD, IM_CASH, IM_SIREN_B, IM_SIREN_R,
    IM_PAUSE, IM_BOX, IM_BOXD, IM_BOXG, IM_BOXGREY, IM_BTN, IM_BTND, IM_BTNE, IM_BTNES, IM_BTNGREEN, IM_FONT, IM_COUNT
};
extern Image g_img[IM_COUNT];
extern Image g_caricon[31][2];  // [n][0]=colour [1]=locked
typedef struct { int x, y, adv; } Glyph;
extern Glyph g_glyph[95]; extern int g_font_cell, g_font_h;

int assets_load(void);
