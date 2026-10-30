#pragma once

#include <stdbool.h>
#include "buttons.h"

typedef enum {
    LAUNCHER_GAME_NONE = -1,
    LAUNCHER_GAME_CHESS = 0,
    LAUNCHER_GAME_BUSCAMINAS,
    LAUNCHER_GAME_FLAPPY,
    LAUNCHER_GAME_RUNNER,
    LAUNCHER_GAME_SNAKE,
    LAUNCHER_GAME_ARKANOID,
    LAUNCHER_GAME_COUNT
} launcher_game_t;

void launcher_init(const char *player_tag);
void launcher_on_button(button_id_t btn);
launcher_game_t launcher_get_selected_game(void);
void launcher_reset_selection(void);
void launcher_draw(void);
