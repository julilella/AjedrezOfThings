#include "launcher.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "display.h"
#include "sfx.h"

#define MAX_VISIBLE 4

static int s_selected = 0;
static launcher_game_t s_chosen = LAUNCHER_GAME_NONE;
static char s_tag[8] = "AAA";

typedef struct {
    const char *title;
    const char *desc;
} game_info_t;

static const game_info_t s_games[LAUNCHER_GAME_COUNT] = {
    [LAUNCHER_GAME_CHESS]      = {"1. AJEDREZ 5x5", "Vs CPU, 2P o Wi-Fi"},
    [LAUNCHER_GAME_BUSCAMINAS] = {"2. BUSCAMINAS",   "Minesweeper 9x7"},
    [LAUNCHER_GAME_FLAPPY]     = {"3. FLAPPY KIWI",  "Aletea y esquiva"},
    [LAUNCHER_GAME_RUNNER]     = {"4. KIWI RUNNER",  "Salta y agachate"},
    [LAUNCHER_GAME_SNAKE]      = {"5. VIBORITA",     "Come manzanas"},
    [LAUNCHER_GAME_ARKANOID]   = {"6. ARKANOID",     "Rompe los ladrillos"},
};

void launcher_init(const char *player_tag)
{
    if (player_tag && strlen(player_tag) > 0) {
        strncpy(s_tag, player_tag, sizeof(s_tag) - 1);
        s_tag[sizeof(s_tag) - 1] = '\0';
    }
    s_chosen = LAUNCHER_GAME_NONE;
}

void launcher_reset_selection(void)
{
    s_chosen = LAUNCHER_GAME_NONE;
}

launcher_game_t launcher_get_selected_game(void)
{
    return s_chosen;
}

void launcher_on_button(button_id_t btn)
{
    // Physical UP is received as BTN_DOWN
    if (btn == BTN_DOWN) {
        s_selected = (s_selected + LAUNCHER_GAME_COUNT - 1) % LAUNCHER_GAME_COUNT;
        sfx_play(SFX_SELECT);
    }
    // Physical DOWN is received as BTN_UP
    else if (btn == BTN_UP) {
        s_selected = (s_selected + 1) % LAUNCHER_GAME_COUNT;
        sfx_play(SFX_SELECT);
    }
    // Select / Play with B1 or Center
    else if (btn == BTN_KEY1 || btn == BTN_CENTER) {
        s_chosen = (launcher_game_t)s_selected;
        sfx_play(SFX_START);
    }
}

void launcher_draw(void)
{
    display_clear();

    // Top Header: KIWI ARCADE
    display_fill_rect(0, 0, BOARD_LCD_WIDTH, 11, true);
    display_text(16, 2, "KIWI ARCADE", true);
    display_text(100, 2, s_tag, true);

    // Scrolling list of games (shows 4 at a time)
    int first = (s_selected < MAX_VISIBLE) ? 0 : (s_selected - MAX_VISIBLE + 1);
    int start_y = 14;

    for (int i = first; i < first + MAX_VISIBLE && i < LAUNCHER_GAME_COUNT; i++) {
        int y = start_y + ((i - first) * 10);
        bool is_cur = (i == s_selected);
        if (is_cur) {
            display_fill_rect(4, y - 1, 116, 9, true);
            display_text(8, y, s_games[i].title, true);
        } else {
            display_text(8, y, s_games[i].title, false);
        }
    }

    // Scroll indicators
    if (first > 0) {
        display_text(122, 14, "^", false);
    }
    if (first + MAX_VISIBLE < LAUNCHER_GAME_COUNT) {
        display_text(122, 44, "v", false);
    }

    // Description of selected game at bottom
    display_text(8, 55, s_games[s_selected].desc, false);

    display_flush();
}
