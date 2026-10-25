#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "match.h"
#include "history.h"
#include "ai.h"
#include "sfx.h"

static const char *TAG __attribute__((unused)) = "match";

#define STATE_PERIOD_MS 300
#define GAME_OVER_AUTO_EXIT_MS 5000
#define CPU_THINK_DELAY_MS 600

enum { MSG_STATE = 1 };

typedef struct __attribute__((packed)) {
    uint8_t type;               /* MSG_STATE */
    uint8_t turn_count;
    uint8_t board[BOARD_BYTES];
    uint8_t game_over;          /* game_over_reason_t */
    char winner_name[LINK_NAME_LEN + 1];
} msg_state_t;

static match_t s_match;
static char s_my_name[LINK_NAME_LEN + 1];
static uint32_t s_sent_ms;      /* when we last sent our STATE */
static bool s_history_recorded = false;
static uint32_t s_cpu_think_start_ms = 0;

static void record_played_match_if_needed(void);

void match_init(const char *my_name)
{
    if (my_name) {
        strncpy(s_my_name, my_name, sizeof(s_my_name) - 1);
        s_my_name[sizeof(s_my_name) - 1] = '\0';
    }
}

static uint32_t now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static void send_state(void)
{
    if (s_match.mode != MATCH_MODE_ONLINE) {
        return;
    }
    msg_state_t msg = {
        .type = MSG_STATE,
        .turn_count = s_match.turn_count,
        .game_over = (uint8_t)s_match.game_over_reason,
    };
    strncpy(msg.winner_name, s_match.winner_name, sizeof(msg.winner_name) - 1);
    msg.winner_name[sizeof(msg.winner_name) - 1] = '\0';
    board_to_bytes(&s_match.board, msg.board);
    link_send(&msg, sizeof(msg));
    s_sent_ms = now_ms();
}

static void start(player_color_t color, const link_peer_t *rival)
{
    memset(&s_match, 0, sizeof(s_match));
    s_match.active = true;
    s_match.mode = MATCH_MODE_ONLINE;
    s_match.my_color = color;
    strncpy(s_match.my_name, s_my_name, sizeof(s_match.my_name) - 1);
    s_match.my_name[sizeof(s_match.my_name) - 1] = '\0';
    s_match.rival = *rival;
    s_match.my_time_s = MATCH_TIME_SECONDS;
    s_match.rival_time_s = MATCH_TIME_SECONDS;
    s_match.is_game_over = false;
    s_match.game_over_reason = GAME_OVER_NONE;
    s_match.winner_name[0] = '\0';
    s_match.game_over_since_ms = 0;
    s_history_recorded = false;
    s_cpu_think_start_ms = 0;

    /* White makes the board; black gets it from white's first STATE. */
    if (color == COLOR_WHITE) {
        chessboard_init(s_match.board.sq);
        s_match.started = true;
        send_state();
    }
    sfx_play(SFX_START);
}

esp_err_t match_start_cpu(player_color_t user_color, ai_difficulty_t difficulty)
{
    memset(&s_match, 0, sizeof(s_match));
    s_match.active = true;
    s_match.started = true;
    s_match.mode = MATCH_MODE_CPU;
    s_match.my_color = user_color;
    s_match.ai_diff = difficulty;

    strncpy(s_match.my_name, s_my_name, sizeof(s_match.my_name) - 1);
    s_match.my_name[sizeof(s_match.my_name) - 1] = '\0';

    strncpy(s_match.rival.name, "CPU", sizeof(s_match.rival.name) - 1);
    s_match.rival.name[sizeof(s_match.rival.name) - 1] = '\0';
    const uint8_t cpu_mac[6] = {0x02, 'C', 'P', 'U', 0, 0};
    memcpy(s_match.rival.mac, cpu_mac, 6);

    s_match.my_time_s = MATCH_TIME_SECONDS;
    s_match.rival_time_s = MATCH_TIME_SECONDS;
    s_match.turn_count = 0;
    s_match.is_game_over = false;
    s_match.game_over_reason = GAME_OVER_NONE;
    s_match.winner_name[0] = '\0';
    s_match.game_over_since_ms = 0;
    s_history_recorded = false;
    s_cpu_think_start_ms = 0;
    s_match.cpu_thinking = false;

    chessboard_init(s_match.board.sq);
    sfx_play(SFX_START);
    return ESP_OK;
}

esp_err_t match_start_pass_and_play(void)
{
    memset(&s_match, 0, sizeof(s_match));
    s_match.active = true;
    s_match.started = true;
    s_match.mode = MATCH_MODE_PASS_AND_PLAY;
    s_match.my_color = COLOR_WHITE;

    strncpy(s_match.my_name, "BLANCAS", sizeof(s_match.my_name) - 1);
    s_match.my_name[sizeof(s_match.my_name) - 1] = '\0';

    strncpy(s_match.rival.name, "NEGRAS", sizeof(s_match.rival.name) - 1);
    s_match.rival.name[sizeof(s_match.rival.name) - 1] = '\0';
    const uint8_t p2_mac[6] = {0x02, '2', 'P', 0, 0, 0};
    memcpy(s_match.rival.mac, p2_mac, 6);

    s_match.my_time_s = MATCH_TIME_SECONDS;
    s_match.rival_time_s = MATCH_TIME_SECONDS;
    s_match.turn_count = 0;
    s_match.is_game_over = false;
    s_match.game_over_reason = GAME_OVER_NONE;
    s_match.winner_name[0] = '\0';
    s_match.game_over_since_ms = 0;
    s_history_recorded = false;
    s_cpu_think_start_ms = 0;
    s_match.cpu_thinking = false;

    chessboard_init(s_match.board.sq);
    sfx_play(SFX_START);
    return ESP_OK;
}

bool match_is_active(void)
{
    return s_match.active;
}

bool match_current_turn_is_white(void)
{
    return (s_match.turn_count % 2 == 0);
}

/* The rival's state: take its board if it's newer than ours. */
static void on_state(const msg_state_t *msg)
{
    if (s_match.mode != MATCH_MODE_ONLINE || s_match.is_game_over) {
        return;
    }

    if (msg->turn_count > s_match.turn_count || !s_match.started) {
        if (s_match.started && msg->turn_count > s_match.turn_count) {
            int old_count = board_count_pieces(s_match.board.sq, true) + board_count_pieces(s_match.board.sq, false);
            board_from_bytes(&s_match.board, msg->board);
            int new_count = board_count_pieces(s_match.board.sq, true) + board_count_pieces(s_match.board.sq, false);
            if (new_count < old_count) {
                sfx_play(SFX_CAPTURE);
            } else {
                sfx_play(SFX_MOVE);
            }
        } else {
            board_from_bytes(&s_match.board, msg->board);
        }
        s_match.turn_count = msg->turn_count;
        s_match.started = true;
    }

    if (msg->game_over != GAME_OVER_NONE) {
        s_match.is_game_over = true;
        s_match.game_over_reason = (game_over_reason_t)msg->game_over;
        strncpy(s_match.winner_name, msg->winner_name, sizeof(s_match.winner_name) - 1);
        s_match.winner_name[sizeof(s_match.winner_name) - 1] = '\0';
        s_match.game_over_since_ms = now_ms();
        record_played_match_if_needed();
    }
}

esp_err_t match_accept(void)
{
    link_peer_t rival;
    if (link_get_state(&rival) != LINK_INVITED || link_accept() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    start(COLOR_BLACK, &rival);
    return ESP_OK;
}

static void record_played_match_if_needed(void)
{
    if (s_match.active && s_match.is_game_over && !s_history_recorded) {
        s_history_recorded = true;
        if (s_match.mode == MATCH_MODE_PASS_AND_PLAY) {
            bool white_won = (strcmp(s_match.winner_name, "BLANCAS") == 0);
            history_record(s_match.rival.mac, white_won);
            sfx_play(SFX_WIN);
        } else {
            bool i_won = (s_match.winner_name[0] != '\0' &&
                          strcmp(s_match.winner_name, s_match.my_name) == 0);
            history_record(s_match.rival.mac, i_won);
            sfx_play(i_won ? SFX_WIN : SFX_LOSE);
        }
    }
}

void match_quit(void)
{
    record_played_match_if_needed();
    s_match.active = false;
    if (s_match.mode == MATCH_MODE_ONLINE) {
        link_hangup();
    }
}

void match_on_link_event(link_event_t ev, const link_peer_t *peer, const uint8_t *data, size_t len)
{
    switch (ev) {
    case LINK_EVT_CONNECTED:
        start(COLOR_WHITE, peer); 
        break;
    case LINK_EVT_DISCONNECTED:
        record_played_match_if_needed();
        s_match.active = false;
        break;
    case LINK_EVT_DATA:
        if (s_match.active && s_match.mode == MATCH_MODE_ONLINE && len >= 27 && data[0] == MSG_STATE) {
            msg_state_t msg = {0};
            if (len >= sizeof(msg_state_t)) {
                memcpy(&msg, data, sizeof(msg_state_t));
            } else {
                msg.type = data[0];
                msg.turn_count = data[1];
                memcpy(msg.board, &data[2], BOARD_BYTES);
                msg.game_over = GAME_OVER_NONE;
            }
            on_state(&msg);
        }
        break;
    case LINK_EVT_INVITED:
        break; 
    }
}

static void match_update_timer(void)
{
    static uint32_t last_tick = 0;

    if (!s_match.active || !s_match.started || s_match.is_game_over) {
        last_tick = 0;
        return;
    }

    uint32_t now = now_ms();
    if (last_tick == 0) {
        last_tick = now;
        return;
    }

    if (now - last_tick < 1000) {
        return;
    }

    uint32_t elapsed_s = (now - last_tick) / 1000;
    last_tick += elapsed_s * 1000;

    if (s_match.mode == MATCH_MODE_PASS_AND_PLAY) {
        const bool white_turn = (s_match.turn_count % 2 == 0);
        if (white_turn) {
            if (s_match.my_time_s > (int32_t)elapsed_s) {
                s_match.my_time_s -= elapsed_s;
                if (s_match.my_time_s <= 10 && s_match.my_time_s > 0) {
                    sfx_play(s_match.my_time_s <= 3 ? SFX_PANIC_ALERT : SFX_PANIC_TICK);
                }
            } else {
                s_match.my_time_s = 0;
                s_match.is_game_over = true;
                s_match.game_over_reason = GAME_OVER_TIMEOUT;
                s_match.game_over_since_ms = now_ms();
                strncpy(s_match.winner_name, "NEGRAS", sizeof(s_match.winner_name) - 1);
                s_match.winner_name[sizeof(s_match.winner_name) - 1] = '\0';
                record_played_match_if_needed();
            }
        } else {
            if (s_match.rival_time_s > (int32_t)elapsed_s) {
                s_match.rival_time_s -= elapsed_s;
                if (s_match.rival_time_s <= 10 && s_match.rival_time_s > 0) {
                    sfx_play(s_match.rival_time_s <= 3 ? SFX_PANIC_ALERT : SFX_PANIC_TICK);
                }
            } else {
                s_match.rival_time_s = 0;
                s_match.is_game_over = true;
                s_match.game_over_reason = GAME_OVER_TIMEOUT;
                s_match.game_over_since_ms = now_ms();
                strncpy(s_match.winner_name, "BLANCAS", sizeof(s_match.winner_name) - 1);
                s_match.winner_name[sizeof(s_match.winner_name) - 1] = '\0';
                record_played_match_if_needed();
            }
        }
    } else {
        if (!match_my_turn()) {
            return;
        }

        if (s_match.my_time_s > (int32_t)elapsed_s) {
            s_match.my_time_s -= elapsed_s;
            if (s_match.my_time_s <= 10 && s_match.my_time_s > 0) {
                sfx_play(s_match.my_time_s <= 3 ? SFX_PANIC_ALERT : SFX_PANIC_TICK);
            }
        } else {
            s_match.my_time_s = 0;
            s_match.is_game_over = true;
            s_match.game_over_reason = GAME_OVER_TIMEOUT;
            s_match.game_over_since_ms = now_ms();
            strncpy(s_match.winner_name, s_match.rival.name, sizeof(s_match.winner_name) - 1);
            s_match.winner_name[sizeof(s_match.winner_name) - 1] = '\0';
            record_played_match_if_needed();
            if (s_match.mode == MATCH_MODE_ONLINE) {
                send_state();
            }
        }
    }
}

void match_tick(void)
{
    if (!s_match.active) {
        return;
    }

    if (s_match.is_game_over) {
        record_played_match_if_needed();
        if (s_match.game_over_since_ms > 0 && (now_ms() - s_match.game_over_since_ms >= GAME_OVER_AUTO_EXIT_MS)) {
            match_quit();
            return;
        }
    } else {
        match_update_timer();
    }

    /* CPU turn logic */
    if (s_match.mode == MATCH_MODE_CPU && !s_match.is_game_over) {
        bool cpu_turn = !match_my_turn();
        if (cpu_turn) {
            s_match.cpu_thinking = true;
            if (s_cpu_think_start_ms == 0) {
                s_cpu_think_start_ms = now_ms();
            }
            if (now_ms() - s_cpu_think_start_ms >= CPU_THINK_DELAY_MS) {
                s_cpu_think_start_ms = 0;
                s_match.cpu_thinking = false;
                bool cpu_is_white = (s_match.my_color != COLOR_WHITE);
                int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

                if (ai_find_best_move(&s_match.board, cpu_is_white, s_match.ai_diff, &x0, &y0, &x1, &y1)) {
                    player_t cpu_player = {.is_white = cpu_is_white};
                    bool is_capture = (s_match.board.sq[y1][x1].piece != PIECE_EMPTY);
                    board_move_and_capture(x0, y0, x1, y1, s_match.board.sq, cpu_player);
                    s_match.turn_count++;

                    if (is_capture) {
                        sfx_play(SFX_CAPTURE);
                    } else {
                        sfx_play(SFX_MOVE);
                    }

                    bool human_is_white = (s_match.my_color == COLOR_WHITE);
                    if (board_count_pieces(s_match.board.sq, human_is_white) == 0) {
                        s_match.is_game_over = true;
                        s_match.game_over_reason = GAME_OVER_NO_PIECES;
                        s_match.game_over_since_ms = now_ms();
                        strncpy(s_match.winner_name, s_match.rival.name, sizeof(s_match.winner_name) - 1);
                        s_match.winner_name[sizeof(s_match.winner_name) - 1] = '\0';
                        record_played_match_if_needed();
                    }
                } else {
                    // CPU has no legal moves left
                    s_match.is_game_over = true;
                    s_match.game_over_reason = GAME_OVER_NO_PIECES;
                    s_match.game_over_since_ms = now_ms();
                    strncpy(s_match.winner_name, s_match.my_name, sizeof(s_match.winner_name) - 1);
                    s_match.winner_name[sizeof(s_match.winner_name) - 1] = '\0';
                    record_played_match_if_needed();
                }
            }
        } else {
            s_cpu_think_start_ms = 0;
            s_match.cpu_thinking = false;
        }
    }

    if (s_match.mode == MATCH_MODE_ONLINE && s_match.started && now_ms() - s_sent_ms >= STATE_PERIOD_MS) {
        send_state();
    }
}

bool match_my_turn(void)
{
    if (!s_match.active || !s_match.started || s_match.is_game_over) {
        return false;
    }
    if (s_match.mode == MATCH_MODE_PASS_AND_PLAY) {
        return true;
    }
    const bool white_to_move = (s_match.turn_count % 2 == 0);
    return white_to_move == (s_match.my_color == COLOR_WHITE);
}

bool match_move(int x0, int y0, int x1, int y1)
{
    if (s_match.is_game_over || !match_my_turn()) {
        return false;
    }

    bool mover_is_white;
    if (s_match.mode == MATCH_MODE_PASS_AND_PLAY) {
        mover_is_white = (s_match.turn_count % 2 == 0);
    } else {
        mover_is_white = (s_match.my_color == COLOR_WHITE);
    }

    const player_t current_player = {.is_white = mover_is_white};
    bool is_capture = (s_match.board.sq[y1][x1].piece != PIECE_EMPTY);

    if (!board_move_and_capture(x0, y0, x1, y1, s_match.board.sq, current_player)) {
        sfx_play(SFX_INVALID);
        return false;
    }
    s_match.turn_count++;

    if (is_capture) {
        sfx_play(SFX_CAPTURE);
    } else {
        sfx_play(SFX_MOVE);
    }

    /* Check if rival has no pieces left */
    const bool opponent_is_white = !mover_is_white;
    if (board_count_pieces(s_match.board.sq, opponent_is_white) == 0) {
        s_match.is_game_over = true;
        s_match.game_over_reason = GAME_OVER_NO_PIECES;
        s_match.game_over_since_ms = now_ms();
        if (s_match.mode == MATCH_MODE_PASS_AND_PLAY) {
            strncpy(s_match.winner_name, mover_is_white ? "BLANCAS" : "NEGRAS", sizeof(s_match.winner_name) - 1);
        } else {
            strncpy(s_match.winner_name, s_match.my_name, sizeof(s_match.winner_name) - 1);
        }
        s_match.winner_name[sizeof(s_match.winner_name) - 1] = '\0';
        record_played_match_if_needed();
    }

    if (s_match.mode == MATCH_MODE_ONLINE) {
        send_state();
    }
    return true;
}

const match_t *match_get(void)
{
    return &s_match;
}
