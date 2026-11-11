#pragma once
#include "common.h"

enum {
    S_BUTTON, S_GRAVEL, S_ENGINE, S_COIN, S_CLAP, S_REVEAL, S_SCRAPE, S_SPLASH, S_CIRCUS, S_ROCKET, S_IMPACT,
    S_JUMP, S_SIREN, S_TURBO, S_HELI, S_SKID, S_WIND, S_BOOM, S_MUSIC, S_HORN, S_POP, S_CLICK, S_BEEP,
    S_BTN1, S_BTN2, S_ERROR, S_COUNT
};

int  audio_init(void);
int  snd_play(int id, float vol, float pitch);               // one-shot, returns voice
void snd_loop(int *voice, int id, float vol, float pitch);   // keeps a looping voice alive; vol<=0 stops it
void snd_stop_all_loops(void);
void snd_music(int on);
void snd_set_volumes(float sfx, float music);
