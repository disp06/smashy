#pragma once
#include "common.h"

enum {
    BTN_UP = 1 << 0, BTN_DOWN = 1 << 1, BTN_LEFT = 1 << 2, BTN_RIGHT = 1 << 3,
    BTN_CROSS = 1 << 4, BTN_CIRCLE = 1 << 5, BTN_SQUARE = 1 << 6, BTN_TRIANGLE = 1 << 7,
    BTN_L = 1 << 8, BTN_R = 1 << 9, BTN_START = 1 << 10, BTN_SELECT = 1 << 11,
};

typedef struct {
    float lx, ly;            // left stick -1..1
    uint32_t held, pressed;  // buttons
    int touch_n;             // number of front touches
    float tx[4], ty[4];      // in 960x544 space
    int touch_tap;           // 1 on the frame a new touch begins
    float tap_x, tap_y;
} Input;

int  plat_init(void);
void plat_poll(Input *in);
double plat_time(void);
const char *plat_data_dir(void);   // ends with '/'
const char *plat_save_path(void);
void plat_audio_start(void (*mix)(int16_t *out, int frames));
void plat_lock(void);
void plat_unlock(void);
int  plat_should_quit(void);
void plat_present_hook(int frame); // desktop: screenshots
uint8_t *plat_read_file(const char *path, size_t *size);
