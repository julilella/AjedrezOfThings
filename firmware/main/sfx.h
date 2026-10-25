#pragma once

typedef enum {
    SFX_NONE = 0,
    SFX_MOVE,
    SFX_CAPTURE,
    SFX_INVALID,
    SFX_SELECT,
    SFX_START,
    SFX_WIN,
    SFX_LOSE,
    SFX_PANIC_TICK,
} sfx_type_t;

void sfx_init(void);
void sfx_play(sfx_type_t sfx);
