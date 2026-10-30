#include <stdio.h>
#include <string.h>

#include "display.h"
#include "board.h"
#include "match.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "screens.h"
#include "history.h"


#define PEER_ROWS 4
#define BUTTON_SLOTS 4
#define BUTTON_SLOT_W (BOARD_LCD_WIDTH / BUTTON_SLOTS)
#define BUTTON_SLOT_CHARS (BUTTON_SLOT_W / DISPLAY_FONT_W)
#define BLINK_INTERVAL_MS 300
#define ALERT_TIME_MS 1500
#define UI_PERIOD_MS 10

/* Inverted bar across the top. */
static void title(const char *text)
{
    display_text(1, 1, text, true);
}

static void display_text_centered(int y, const char *str, bool invert)
{
    int len = strlen(str);
    int x = (BOARD_LCD_WIDTH - len * DISPLAY_FONT_W) / 2;
    if (x < 0) {
        x = 0;
    }
    display_text(x, y, str, invert);
}

static void display_button_options(const char **options, int len) {
    const int y = BOARD_LCD_HEIGHT - DISPLAY_FONT_H - 1;
    for (int i = 0; i < len && i < BUTTON_SLOTS; i++) {
        int x = i * BUTTON_SLOT_W;
        char label[16];
        
        /* Draw option */
        snprintf(label, sizeof(label), "%s", options[i]);
        display_text(x, y, label, false);
    }
}

void screen_welcome(const char *name)
{
    display_clear();

    display_text(34, 8, "BIENVENIDO", false);
    display_draw_rect(44, 22, 40, 18);
    display_text(55, 27, name, false);

    display_button_options((const char *[]){"ok", "editar"}, 2);
    display_flush();
}

void screen_lobby(const char *me, const link_peer_t *nearby, int n, int selected)
{
    char line[48];

    display_clear();
    snprintf(line, sizeof(line), "SALA   yo:%s", me);
    title(line);

    if (n == 0) {
        display_text(0, 16, "nadie cerca...", false);
    }

    /* First row to draw: 0 until the selection passes the last row, then
       scroll so the selected board stays on the bottom row. */
    int first = selected < PEER_ROWS ? 0 : selected - PEER_ROWS + 1;
    for (int i = first; i < n && i < first + PEER_ROWS; i++) {
        match_history_t hist;
        history_get(nearby[i].mac, &hist);
        snprintf(line, sizeof(line), "%c %s %u - %u%s", i == selected ? '>' : ' ',
                 nearby[i].name, hist.wins, hist.losses, nearby[i].busy ? " ocup" : "");
        display_text(0, 14 + (i - first) * 9, line, false);
    }
    if (first + PEER_ROWS < n) {
        display_text(120, 41, "v", false);  /* more below */
    }

    const int btn_y = BOARD_LCD_HEIGHT - DISPLAY_FONT_H - 1;
    if (n > 0) {
        display_text(0, btn_y, "B1:Ret", false);
        display_text(38, btn_y, "B2:CPU", false);
        display_text(76, btn_y, "B3:2P", false);
        display_text(106, btn_y, "B4:M", false);
    } else {
        display_text(6, btn_y, "B2:CPU", false);
        display_text(50, btn_y, "B3:2P", false);
        display_text(86, btn_y, "B4:Menu", false);
    }
    display_flush();
}

void screen_cpu_menu(ai_difficulty_t diff, int color_choice)
{
    display_clear();
    title("MODO VS CPU");

    const char *diff_names[] = {"FACIL", "MEDIO", "DIFICIL"};
    const char *color_names[] = {"BLANCAS", "NEGRAS", "AZAR"};

    char line[32];
    snprintf(line, sizeof(line), "Nivel: %s", diff_names[diff % 3]);
    display_text(4, 18, line, false);

    snprintf(line, sizeof(line), "Color: %s", color_names[color_choice % 3]);
    display_text(4, 32, line, false);

    const int btn_y = BOARD_LCD_HEIGHT - DISPLAY_FONT_H - 1;
    display_text(0, btn_y, "jugar", false);
    display_text(36, btn_y, "nivel", false);
    display_text(74, btn_y, "color", false);
    display_text(106, btn_y, "atras", false);
    display_flush();
}

void screen_inviting(const link_peer_t *peer)
{
    display_clear();
    title("INVITANDO");
    char line[24];
    snprintf(line, sizeof(line), "esperando a %s", peer->name);
    display_text(0, 16, line, false);
    display_button_options((const char *[]){"cancelar"}, 1);
    display_flush();
}

void screen_invited(const link_peer_t *peer)
{
    display_clear();
    title("DESAFIO");
    char line[24];
    snprintf(line, sizeof(line), "te reta %s", peer->name);
    display_text(0, 16, line, false);
    display_button_options((const char *[]){"si", "no"}, 2);
    display_flush();
}

void screen_connected(const link_peer_t *peer)
{
    display_clear();
    title("CONECTADO");
    char line[24];
    snprintf(line, sizeof(line), "conectado a %s", peer->name);
    display_text(0, 16, line, false);
    display_button_options((const char *[]){"salir"}, 1);
    display_flush();
}

/* One board square: outlined if empty or white, filled if black. */
static void draw_key(int x, int y, int size, bool pressed)
{
    if (pressed) {
        display_fill_rect(x, y, size, size, true);
    } else {
        display_fill_rect(x, y, size, size, false);
        display_draw_rect(x, y, size, size);
    }
}

int invalid_move_alert_time = 0;

void screen_match(const match_t *match, int cursor_x, int cursor_y, int picked_x, int picked_y, bool picked, bool is_invalid_move)
{
    char line[24];
    const bool blink_on = (xTaskGetTickCount() * portTICK_PERIOD_MS / BLINK_INTERVAL_MS) % 2;

    display_clear();

    if (match->is_game_over) {
        if (match->mode == MATCH_MODE_PASS_AND_PLAY) {
            display_text_centered(8, "Fin de juego!", true);
            snprintf(line, sizeof(line), "Gana: %s", match->winner_name);
            display_text_centered(24, line, false);
            if (match->game_over_reason == GAME_OVER_TIMEOUT) {
                display_text_centered(36, "por tiempo", false);
            } else if (match->game_over_reason == GAME_OVER_NO_PIECES) {
                display_text_centered(36, "por captura", false);
            }
        } else {
            const bool i_won = (match->winner_name[0] != '\0' &&
                                strcmp(match->winner_name, match->my_name) == 0);

            if (i_won) {
                display_text_centered(8, "Ganaste!", true);
                if (match->game_over_reason == GAME_OVER_TIMEOUT) {
                    snprintf(line, sizeof(line), "%s se quedo", match->rival.name);
                    display_text_centered(24, line, false);
                    display_text_centered(36, "sin tiempo", false);
                } else if (match->game_over_reason == GAME_OVER_NO_PIECES) {
                    display_text_centered(24, "Comiste todas", false);
                    display_text_centered(36, "las piezas", false);
                } else {
                    snprintf(line, sizeof(line), "Gana: %s", match->winner_name);
                    display_text_centered(28, line, false);
                }
            } else {
                display_text_centered(8, "Perdiste!", true);
                if (match->game_over_reason == GAME_OVER_TIMEOUT) {
                    display_text_centered(24, "Te quedaste", false);
                    display_text_centered(36, "sin tiempo", false);
                } else if (match->game_over_reason == GAME_OVER_NO_PIECES) {
                    display_text_centered(24, "Te quedaste", false);
                    display_text_centered(36, "sin piezas", false);
                } else {
                    snprintf(line, sizeof(line), "Gana: %s", match->winner_name);
                    display_text_centered(28, line, false);
                }
            }
        }

        display_text_centered(52, "B4: volver", false);
        display_flush();
        return;
    }

    if (is_invalid_move) {
        invalid_move_alert_time = ALERT_TIME_MS;
    }

    const bool mover_is_white = (match->mode == MATCH_MODE_PASS_AND_PLAY) ?
                                (match->turn_count % 2 == 0) : (match->my_color == COLOR_WHITE);
    const player_t current_mover = {.is_white = mover_is_white};

    // Board: squares of 11 px every 12 px, from (0, 2). The cursor
    // square is drawn without a border.
    int y_pos = 2;
    for (int y = 0; y < 5; y++) {
        int x_pos = 0;
        for (int x = 0; x < 5; x++) {
            const piece_t square = match->board.sq[y][x];
            const bool is_cursor = x == cursor_x && y == cursor_y;
            const bool is_legal_dest = picked && board_is_legal_move(picked_x, picked_y, x, y, match->board.sq, current_mover);
            char txt[2] = {piece_to_char(square.piece), '\0'};

            if (square.piece == PIECE_EMPTY || square.is_white) {
                if (!is_cursor) {
                    if (picked && x == picked_x && y == picked_y) {
                        if (blink_on) {
                            draw_key(x_pos, y_pos, 11, false);
                        }
                    } else {
                        draw_key(x_pos, y_pos, 11, false);
                    }
                }
                if (square.piece != PIECE_EMPTY) {
                    display_text_piece(x_pos + 3, y_pos + 2, txt, false);
                }
            } else {
                if (!is_cursor) {
                    if (picked && x == picked_x && y == picked_y) {
                        if (blink_on) {
                            draw_key(x_pos, y_pos, 11, true);
                        }
                    } else {
                        draw_key(x_pos, y_pos, 11, true);
                    }
                }
                display_text_piece(x_pos + 3, y_pos + 2, txt, true);
            }

            /* Highlight legal destination squares when a piece is picked */
            if (is_legal_dest) {
                if (square.piece == PIECE_EMPTY) {
                    // Center dot on empty square
                    display_fill_rect(x_pos + 4, y_pos + 4, 3, 3, true);
                } else {
                    // Corner targeting brackets for capturable pieces
                    const bool col = square.is_white ? true : false;
                    // Top-left
                    display_set_pixel(x_pos + 2, y_pos + 2, col);
                    display_set_pixel(x_pos + 3, y_pos + 2, col);
                    display_set_pixel(x_pos + 2, y_pos + 3, col);
                    // Top-right
                    display_set_pixel(x_pos + 8, y_pos + 2, col);
                    display_set_pixel(x_pos + 7, y_pos + 2, col);
                    display_set_pixel(x_pos + 8, y_pos + 3, col);
                    // Bottom-left
                    display_set_pixel(x_pos + 2, y_pos + 8, col);
                    display_set_pixel(x_pos + 3, y_pos + 8, col);
                    display_set_pixel(x_pos + 2, y_pos + 7, col);
                    // Bottom-right
                    display_set_pixel(x_pos + 8, y_pos + 8, col);
                    display_set_pixel(x_pos + 7, y_pos + 8, col);
                    display_set_pixel(x_pos + 8, y_pos + 7, col);
                }
            }

            x_pos += 12;
        }
        y_pos += 12;
    }

    bool is_panic = false;
    const bool panic_blink = ((xTaskGetTickCount() * portTICK_PERIOD_MS / 200) % 2 == 0);

    // Right of the board: who we play, our color, whose turn, timer, and keys.
    if (match->mode == MATCH_MODE_PASS_AND_PLAY) {
        bool white_turn = (match->turn_count % 2 == 0);
        display_text(62, 6, "1vs1 Local", false);
        display_text(62, 16, white_turn ? "T: BLANCAS" : "T: NEGRAS", true);
        int cur_time = white_turn ? match->my_time_s : match->rival_time_s;
        int m = cur_time / 60;
        int s = cur_time % 60;
        is_panic = (cur_time <= 10 && cur_time > 0);
        if (is_panic) {
            snprintf(line, sizeof(line), "! %02d:%02d !", m, s);
            display_text(62, 26, line, panic_blink);
        } else {
            snprintf(line, sizeof(line), "%02d:%02d", m, s);
            display_text(62, 26, line, false);
        }
    } else {
        snprintf(line, sizeof(line), "vs %s", match->rival.name);
        display_text(62, 6, line, false);
        display_text(62, 16, match->my_color == COLOR_WHITE ? "tu: blancas" : "tu: negras", false);
        if (!match->started) {
            display_text(62, 26, "espera...", false);
        } else if (match->cpu_thinking) {
            display_text(62, 26, "CPU piensa", true);
        } else {
            int m = match->my_time_s / 60;
            int s = match->my_time_s % 60;
            is_panic = (match_my_turn() && match->my_time_s <= 10 && match->my_time_s > 0);
            if (is_panic) {
                snprintf(line, sizeof(line), "!TU %02d:%02d!", m, s);
                display_text(62, 26, line, panic_blink);
            } else {
                snprintf(line, sizeof(line), "%s %02d:%02d", match_my_turn() ? "TU" : "RIV", m, s);
                display_text(62, 26, line, match_my_turn());
            }
        }
    }
    if (invalid_move_alert_time > 0) {
        display_text(62, 36, "Movimiento", false);
        display_text(62, 46, "Invalido", false);
        invalid_move_alert_time -= UI_PERIOD_MS;
    } else if (is_panic) {
        display_text(62, 36, "! APURO !", panic_blink);
        display_text(62, 46, picked ? "B1 mover" : "B1 elegir", false);
    } else {
        invalid_move_alert_time = 0;
        display_text(62, 36, picked ? "B1 mover" : "B1 elegir", false);
        display_text(62, 46, "B2 atras", false);
    }
    display_text(62, 56, "B4 salir", false);
    display_flush();
}
