#include "arkanoid.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "board.h"
#include "display.h"
#include "sfx.h"

#define PLAY_X_MIN      3
#define PLAY_X_MAX      97
#define PLAY_Y_MIN      2
#define PLAY_Y_MAX      62

#define PADDLE_W        16
#define PADDLE_H        3
#define PADDLE_Y        57

#define BRICK_ROWS      3
#define BRICK_COLS      8
#define TOTAL_BRICKS    (BRICK_ROWS * BRICK_COLS)
#define BRICK_W         10
#define BRICK_H         4

#define BALL_SPEED      1.6f
#define NVS_NS_ARK      "arkanoid_cfg"
#define NVS_KEY_HI      "hi_score"

typedef struct {
    bool active;
    uint8_t x, y;
} brick_t;

static brick_t s_bricks[TOTAL_BRICKS];
static int s_bricks_left = TOTAL_BRICKS;

static float s_paddle_x = 42.0f;
static float s_ball_x = 49.0f;
static float s_ball_y = 54.0f;
static float s_ball_vx = 1.0f;
static float s_ball_vy = -1.2f;
static bool s_ball_launched = false;

static int s_lives = 3;
static int s_score = 0;
static int s_high_score = 0;

static bool s_game_over = false;
static bool s_won = false;
static bool s_exit_requested = false;

static void load_high_score(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS_ARK, NVS_READONLY, &nvs) == ESP_OK) {
        uint32_t val = 0;
        if (nvs_get_u32(nvs, NVS_KEY_HI, &val) == ESP_OK) {
            s_high_score = (int)val;
        }
        nvs_close(nvs);
    }
}

static void save_high_score(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS_ARK, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u32(nvs, NVS_KEY_HI, (uint32_t)s_high_score);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void reset_ball_on_paddle(void)
{
    s_ball_launched = false;
    s_ball_x = s_paddle_x + (PADDLE_W / 2) - 1;
    s_ball_y = PADDLE_Y - 3;
    s_ball_vx = 0.9f;
    s_ball_vy = -1.3f;
}

void arkanoid_init(void)
{
    load_high_score();

    s_paddle_x = 42.0f;
    s_lives = 3;
    s_score = 0;
    s_game_over = false;
    s_won = false;
    s_exit_requested = false;

    // Initialize bricks
    s_bricks_left = TOTAL_BRICKS;
    int idx = 0;
    int start_x = 4;
    int start_y = 6;
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            s_bricks[idx].active = true;
            s_bricks[idx].x = start_x + c * (BRICK_W + 2);
            s_bricks[idx].y = start_y + r * (BRICK_H + 2);
            idx++;
        }
    }

    reset_ball_on_paddle();
}

bool arkanoid_should_exit(void)
{
    return s_exit_requested;
}

void arkanoid_on_button(button_id_t btn)
{
    if (btn == BTN_KEY4) {
        sfx_play(SFX_SELECT);
        s_exit_requested = true;
        return;
    }

    if (s_game_over || s_won) {
        if (btn == BTN_KEY1 || btn == BTN_CENTER) {
            sfx_play(SFX_START);
            arkanoid_init();
        }
        return;
    }

    // Launch ball: B1 or Center
    if (!s_ball_launched && (btn == BTN_KEY1 || btn == BTN_CENTER)) {
        s_ball_launched = true;
        sfx_play(SFX_START);
    }

    // Paddle move Left: physical LEFT is BTN_RIGHT
    if (btn == BTN_RIGHT) {
        s_paddle_x -= 5.0f;
        if (s_paddle_x < PLAY_X_MIN) s_paddle_x = PLAY_X_MIN;
        if (!s_ball_launched) {
            s_ball_x = s_paddle_x + (PADDLE_W / 2) - 1;
        }
    }
    // Paddle move Right: physical RIGHT is BTN_LEFT
    else if (btn == BTN_LEFT) {
        s_paddle_x += 5.0f;
        if (s_paddle_x + PADDLE_W > PLAY_X_MAX) s_paddle_x = PLAY_X_MAX - PADDLE_W;
        if (!s_ball_launched) {
            s_ball_x = s_paddle_x + (PADDLE_W / 2) - 1;
        }
    }
}

void arkanoid_tick(void)
{
    if (s_game_over || s_won || !s_ball_launched) {
        return;
    }

    // Move ball
    s_ball_x += s_ball_vx;
    s_ball_y += s_ball_vy;

    // Wall bounces
    if (s_ball_x <= PLAY_X_MIN) {
        s_ball_x = PLAY_X_MIN;
        s_ball_vx = -s_ball_vx;
        sfx_play(SFX_SELECT);
    } else if (s_ball_x + 2 >= PLAY_X_MAX) {
        s_ball_x = PLAY_X_MAX - 2;
        s_ball_vx = -s_ball_vx;
        sfx_play(SFX_SELECT);
    }

    if (s_ball_y <= PLAY_Y_MIN) {
        s_ball_y = PLAY_Y_MIN;
        s_ball_vy = -s_ball_vy;
        sfx_play(SFX_SELECT);
    }

    // Paddle bounce
    if (s_ball_y + 2 >= PADDLE_Y && s_ball_y <= PADDLE_Y + PADDLE_H) {
        if (s_ball_x + 2 >= s_paddle_x && s_ball_x <= s_paddle_x + PADDLE_W) {
            s_ball_y = PADDLE_Y - 2;
            s_ball_vy = -fabsf(s_ball_vy);

            // Deflect angle based on where it hit the paddle
            float hit_pos = (s_ball_x + 1.0f) - (s_paddle_x + (PADDLE_W / 2.0f));
            s_ball_vx = hit_pos * 0.18f;

            // Clamp max horizontal speed
            if (s_ball_vx > 1.8f) s_ball_vx = 1.8f;
            if (s_ball_vx < -1.8f) s_ball_vx = -1.8f;

            sfx_play(SFX_SELECT);
        }
    }

    // Brick collisions
    for (int i = 0; i < TOTAL_BRICKS; i++) {
        if (!s_bricks[i].active) continue;

        int bx = s_bricks[i].x;
        int by = s_bricks[i].y;

        if (s_ball_x + 2 >= bx && s_ball_x <= bx + BRICK_W &&
            s_ball_y + 2 >= by && s_ball_y <= by + BRICK_H) {

            s_bricks[i].active = false;
            s_bricks_left--;
            s_score += 10;
            if (s_score > s_high_score) {
                s_high_score = s_score;
            }

            s_ball_vy = -s_ball_vy;
            sfx_play(SFX_MOVE);

            // Win check
            if (s_bricks_left == 0) {
                s_won = true;
                sfx_play(SFX_WIN);
                if (s_score >= s_high_score) {
                    s_high_score = s_score;
                    save_high_score();
                }
            }
            break;
        }
    }

    // Bottom loss
    if (s_ball_y > PLAY_Y_MAX) {
        s_lives--;
        sfx_play(SFX_INVALID);

        if (s_lives <= 0) {
            s_game_over = true;
            sfx_play(SFX_LOSE);
            if (s_score >= s_high_score) {
                s_high_score = s_score;
                save_high_score();
            }
        } else {
            reset_ball_on_paddle();
        }
    }
}

void arkanoid_draw(void)
{
    display_clear();

    // Playfield outer border (left, top, right)
    for (int y = PLAY_Y_MIN; y <= PLAY_Y_MAX; y++) {
        display_set_pixel(PLAY_X_MIN - 1, y, true);
        display_set_pixel(PLAY_X_MAX, y, true);
    }
    for (int x = PLAY_X_MIN - 1; x <= PLAY_X_MAX; x++) {
        display_set_pixel(x, PLAY_Y_MIN - 1, true);
    }

    // Draw Bricks
    for (int i = 0; i < TOTAL_BRICKS; i++) {
        if (s_bricks[i].active) {
            display_fill_rect(s_bricks[i].x, s_bricks[i].y, BRICK_W, BRICK_H, true);
            // 1px inner highlight for 3D brick look
            display_set_pixel(s_bricks[i].x + BRICK_W - 1, s_bricks[i].y, false);
            display_set_pixel(s_bricks[i].x, s_bricks[i].y + BRICK_H - 1, false);
        }
    }

    // Draw Paddle
    display_fill_rect((int)s_paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H, true);

    // Draw Ball
    display_fill_rect((int)s_ball_x, (int)s_ball_y, 2, 2, true);

    // Side Info Panel (x = 101..127)
    char buf[16];
    display_text(102, 4, "SCORE", false);
    snprintf(buf, sizeof(buf), "%03d", s_score);
    display_text(102, 14, buf, false);

    display_text(102, 28, "HI", false);
    snprintf(buf, sizeof(buf), "%03d", s_high_score);
    display_text(102, 38, buf, false);

    // Lives icons
    display_text(102, 50, "V:", false);
    for (int i = 0; i < s_lives; i++) {
        display_fill_rect(114 + i * 4, 52, 3, 4, true);
    }

    // Overlays
    if (!s_ball_launched && !s_game_over && !s_won) {
        display_text(26, 36, "B1: Lanzar", false);
    } else if (s_game_over) {
        display_fill_rect(18, 20, 68, 26, false);
        display_draw_rect(18, 20, 68, 26);
        display_text(24, 24, "FIN JUEGO", true);
        display_text(22, 34, "B1:Otra B4:M", false);
    } else if (s_won) {
        display_fill_rect(18, 20, 68, 26, false);
        display_draw_rect(18, 20, 68, 26);
        display_text(24, 24, "VICTORIA!", true);
        display_text(22, 34, "B1:Otra B4:M", false);
    }

    display_flush();
}

