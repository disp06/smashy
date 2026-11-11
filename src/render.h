#pragma once
#include "assets.h"

typedef struct {
    V3 target; float zoom;      // ortho half height
    M4 view, proj, vp;
    V3 fwd;
    float shake;
} Camera;
extern Camera g_cam;
extern const V3 LIGHT_DIR;      // direction light travels

void cam_update(V3 target, float zoom);
int  cam_visible(V3 lo, V3 hi); // world AABB test
// biome: texture swap for road tiles (0 normal,1 desert,2 grass)
void draw_model(int model, M4 world, float yaw, int is_static, const float color[4], int biome);
void draw_model_ex(int model, M4 world, float yaw, const float color[4], int mode);
void shadow_box(V3 c, float hx, float hz, float h, float yaw, float alpha);
void shadows_flush(void);
// voxel particles
void parts_spawn(V3 p, V3 v, float size, int hue, float life, float grav);
void parts_update(float dt);
void parts_draw(void);
void parts_clear(void);
// 2D
void ui_begin(void);
void ui_rect(float x, float y, float w, float h, const float col[4]);
void ui_image(const Image *im, float x, float y, float w, float h, const float col[4]);
float ui_text(const char *s, float x, float y, float scale, const float col[4], int align); // align 0 left 1 centre 2 right
float ui_text_shadow(const char *s, float x, float y, float scale, const float col[4], int align);
float ui_text_width(const char *s, float scale);
void ground_quad(float x0, float z0, float x1, float z1, float y, const float col[4]);
