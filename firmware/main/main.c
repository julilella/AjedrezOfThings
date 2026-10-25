/*
 * AjedrezOfThings firmware: start-up and the main loop.
 *
 * The network side is done: components/link finds nearby boards and handles
 * invite / accept / hang up (see link.h).
 * Supports:
 *   - Online multiplayer via ESP-NOW
 *   - Solo play vs native CPU / AI (Easy / Medium / Hard)
 *   - Local 2-player Pass & Play
 *
 * Joystick note: buttons.c names its directions as in the organizers' board.h,
 * which are shifted 90 degrees. Pushing up arrives as BTN_RIGHT, right as
 * BTN_DOWN, down as BTN_LEFT, left as BTN_UP. The keys (BTN_KEY1..4) are fine.
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs_flash.h"

#include "buttons.h"
#include "display.h"
#include "events.h"
#include "link.h"
#include "match.h"
#include "player.h"
#include "screens.h"
#include "ai.h"
#include "audio.h"
#include "sfx.h"

static const char *TAG = "main";

#define UI_PERIOD_MS 100    /* redraw at least this often */
#define MAX_LISTED   16     /* boards in the lobby list (the screen scrolls) */

typedef enum {
    APP_STATE_LOBBY = 0,
    APP_STATE_CPU_MENU,
} app_screen_state_t;

static app_screen_state_t s_app_state = APP_STATE_LOBBY;
static ai_difficulty_t s_selected_diff = AI_MEDIUM;
static int s_selected_color = 0; /* 0: Blancas, 1: Negras, 2: Azar */

static char s_name[LINK_NAME_LEN + 1];
char player_tag[PLAYER_NAME_LEN];
static int s_selected;      /* lobby: index of the highlighted board in link_nearby() */

/* Match screen: the square the joystick points at, and the piece picked up. */
static int s_cursor_x;
static int s_cursor_y;
static bool s_picked;
static int s_pick_x;
static int s_pick_y;
static int is_invalid_move;

static void reset_match_cursor(void)
{
    s_cursor_x = 2;
    s_cursor_y = 2;
    s_picked = false;
    is_invalid_move = false;
}

/* Arcade-style initials from the MAC, so every board gets its own name. */
static void name_from_mac(char out[LINK_NAME_LEN + 1])
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    for (int i = 0; i < LINK_NAME_LEN; i++) {
        out[i] = 'A' + mac[3 + i] % 26;
    }
    out[LINK_NAME_LEN] = '\0';
}

/* Joystick moves the cursor. KEY1 picks up a piece (on our turn), then KEY1
 * again asks match to move it; KEY2 puts it back down. */
static void on_match_button(button_id_t key)
{
    /* Joystick: same mapping as Franco and Fede's draw_ui(). */
    if (key == BTN_DOWN && s_cursor_y > 0) {
        s_cursor_y--;
    } else if (key == BTN_RIGHT && s_cursor_x > 0) {
        s_cursor_x--;
    } else if (key == BTN_LEFT && s_cursor_x < 4) {
        s_cursor_x++;
    } else if (key == BTN_UP && s_cursor_y < 4) {
        s_cursor_y++;
    } else if (key == BTN_KEY1 && !s_picked) {
        if (match_my_turn()) {
            const match_t *m = match_get();
            bool imwhite = (m->mode == MATCH_MODE_PASS_AND_PLAY) ?
                           (m->turn_count % 2 == 0) : (m->my_color == COLOR_WHITE);
            bool square_is_white = m->board.sq[s_cursor_y][s_cursor_x].is_white;
            bool square_is_empty = m->board.sq[s_cursor_y][s_cursor_x].piece == PIECE_EMPTY;
            if (square_is_white == imwhite && !square_is_empty) {
                s_picked = true;
                s_pick_x = s_cursor_x;
                s_pick_y = s_cursor_y;
                sfx_play(SFX_SELECT);
            }
        }
    } else if (key == BTN_KEY1) {
        /* Illegal moves do nothing (tell the player). */
        if (!match_move(s_pick_x, s_pick_y, s_cursor_x, s_cursor_y)) {
            is_invalid_move = true;
        }
        s_picked = false;
    } else if (key == BTN_KEY2) {
        if (s_picked) {
            sfx_play(SFX_SELECT);
        }
        s_picked = false;
    }
}

static void on_button(button_id_t key)
{
    if (match_is_active()) {
        if (match_get()->is_game_over) {
            if (key == BTN_KEY4 || key == BTN_KEY1) {
                sfx_play(SFX_SELECT);
                match_quit();
                s_app_state = APP_STATE_LOBBY;
            }
        } else if (key == BTN_KEY4) {
            sfx_play(SFX_SELECT);
            match_quit();
            s_app_state = APP_STATE_LOBBY;
        } else {
            on_match_button(key);
        }
        return;
    }

    if (s_app_state == APP_STATE_CPU_MENU) {
        if (key == BTN_KEY1) {
            /* JUGAR */
            player_color_t color;
            if (s_selected_color == 2) {
                color = (esp_random() % 2 == 0) ? COLOR_WHITE : COLOR_BLACK;
            } else {
                color = (s_selected_color == 0) ? COLOR_WHITE : COLOR_BLACK;
            }
            reset_match_cursor();
            match_start_cpu(color, s_selected_diff);
            s_app_state = APP_STATE_LOBBY;
        } else if (key == BTN_KEY2) {
            /* DIFICULTAD */
            sfx_play(SFX_SELECT);
            s_selected_diff = (s_selected_diff + 1) % AI_DIFFICULTY_COUNT;
        } else if (key == BTN_KEY3) {
            /* COLOR */
            sfx_play(SFX_SELECT);
            s_selected_color = (s_selected_color + 1) % 3;
        } else if (key == BTN_KEY4) {
            /* ATRAS */
            sfx_play(SFX_SELECT);
            s_app_state = APP_STATE_LOBBY;
        }
        return;
    }

    switch (link_get_state(NULL)) {
    case LINK_IDLE: {
        link_peer_t nearby[MAX_LISTED];
        const int n = link_nearby(nearby, MAX_LISTED);

        if (key == BTN_KEY3) {
            sfx_play(SFX_SELECT);
            s_app_state = APP_STATE_CPU_MENU;
            break;
        }
        if (key == BTN_KEY4) {
            reset_match_cursor();
            match_start_pass_and_play();
            break;
        }

        if (n == 0) {
            break;
        }
        if (s_selected >= n) {
            s_selected = 0;  /* a board left the list */
        }
        if (key == BTN_KEY2) {
            sfx_play(SFX_SELECT);
            s_selected = (s_selected + 1) % n;
        } else if (key == BTN_KEY1 && !nearby[s_selected].busy) {
            sfx_play(SFX_SELECT);
            reset_match_cursor();
            link_invite(&nearby[s_selected]);  /* busy boards wouldn't answer */
        }
        break;
    }
    case LINK_INVITING:
        if (key == BTN_KEY1 || key == BTN_KEY2) {
            sfx_play(SFX_SELECT);
            link_hangup();
        }
        break;
    case LINK_INVITED:
        if (key == BTN_KEY1) {
            sfx_play(SFX_SELECT);
            reset_match_cursor();
            match_accept();  /* we play black */
        } else if (key == BTN_KEY2) {
            sfx_play(SFX_SELECT);
            link_hangup();
        }
        break;
    case LINK_CONNECTED:
        break;
    }
}

static void on_link_event(const event_t *ev)
{
    /* If playing local match, decline incoming invites to avoid interrupting */
    if (match_is_active() && match_get()->mode != MATCH_MODE_ONLINE) {
        if (ev->link == LINK_EVT_INVITED) {
            link_hangup();
        }
        return;
    }

    match_on_link_event(ev->link, &ev->peer, ev->data, ev->len);

    switch (ev->link) {
    case LINK_EVT_INVITED:
        ESP_LOGI(TAG, "%s invites us", ev->peer.name);
        break;
    case LINK_EVT_CONNECTED:
        ESP_LOGI(TAG, "%s accepted", ev->peer.name);
        reset_match_cursor();
        break;
    case LINK_EVT_DISCONNECTED:
        ESP_LOGI(TAG, "%s is gone", ev->peer.name);
        break;
    case LINK_EVT_DATA:
        break;
    }
}

/* Draw the screen for whatever state the app is in. */
static void draw(void)
{
    if (match_is_active()) {
        screen_match(match_get(), s_cursor_x, s_cursor_y, s_pick_x, s_pick_y, s_picked, is_invalid_move);
        if (is_invalid_move) {
            is_invalid_move = false;  // Reset the flag after displaying the alert
        }
        return;
    }

    if (s_app_state == APP_STATE_CPU_MENU) {
        screen_cpu_menu(s_selected_diff, s_selected_color);
        return;
    }

    link_peer_t peer;
    link_peer_t nearby[MAX_LISTED];

    switch (link_get_state(&peer)) {
    case LINK_IDLE: {
        const int n = link_nearby(nearby, MAX_LISTED);
        if (s_selected >= n) {
            s_selected = 0;  /* a board left the list */
        }
        screen_lobby(s_name, nearby, n, s_selected);
        break;
    }
    case LINK_INVITING:
        screen_inviting(&peer);
        break;
    case LINK_INVITED:
        screen_invited(&peer);
        break;
    case LINK_CONNECTED:
        screen_match(match_get(), s_cursor_x, s_cursor_y, s_pick_x, s_pick_y, s_picked, is_invalid_move);
        if (is_invalid_move) {
            is_invalid_move = false;
        }
        break;
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(buttons_init());

    esp_err_t audio_err = audio_init();
    if (audio_err == ESP_OK) {
        sfx_init();
    } else {
        ESP_LOGW(TAG, "audio_init failed: %s", esp_err_to_name(audio_err));
    }

    bool has_name = player_profile_load(player_tag, sizeof(player_tag));
    if (!has_name) {
        name_from_mac(player_tag);
        player_profile_arcade_input(player_tag);
    } else {
        if (player_welcome_screen(player_tag, 3000)) {
            player_profile_arcade_input(player_tag);
        }
    }
    snprintf(s_name, sizeof(s_name), "%s", player_tag);

    events_init();
    match_init(s_name);
    reset_match_cursor();
    ESP_ERROR_CHECK(link_init(s_name, events_on_link));

    while (1) {
        event_t ev;
        if (events_wait(&ev, UI_PERIOD_MS)) {
            if (ev.kind == EVENT_BUTTON && ev.button.pressed) {
                on_button(ev.button.id);
            } else if (ev.kind == EVENT_LINK) {
                on_link_event(&ev);
            }
        }
        match_tick();
        draw();
    }
}
