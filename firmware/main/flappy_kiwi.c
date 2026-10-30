#include "flappy_kiwi.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "board.h"
#include "display.h"
#include "sfx.h"

#define BIRD_X          24
#define BIRD_W          8
#define BIRD_H          6
#define GROUND_Y        58

#define PIPE_COUNT      2
#define PIPE_WIDTH      10
#define PIPE_GAP_H      22
#define PIPE_SPACING    70

#define NVS_NS_FLAPPY   "flappy_cfg"
#define NVS_KEY_HI      "hi_score"

typedef struct {
    float x;
    int gap_y;
    bool scored;
} pipe_t;

static float s_bird_y = 26.0f;
static float s_bird_vy = 0.0f;
static pipe_t s_pipes[PIPE_COUNT];
static int s_score = 0;
static int s_high_score = 0;
static bool s_game_over = false;
static bool s_started = false;
static bool s_exit_requested = false;
static uint32_t s_ground_scroll = 0;

static const uint8_t s_kiwi_bitmap[6] = {
    0x38, // 00111000
    0xFE, // 11111110
    0xFA, // 11111010 (eye)
    0xFC, // 11111100
    0x48, // 01001000 (wing)
    0x28, // 00101000 (feet)
};

static void load_high_score(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS_FLAPPY, NVS_READONLY, &nvs) == ESP_OK) {
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
    if (nvs_open(NVS_NS_FLAPPY, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u32(nvs, NVS_KEY_HI, (uint32_t)s_high_score);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void reset_pipes(void)
{
    for (int i = 0; i < PIPE_COUNT; i++) {
        s_pipes[i].x = 128 + i * PIPE_SPACING;
        s_pipes[i].gap_y = 8 + (esp_random() % (GROUND_Y - PIPE_GAP_H - 14));
        s_pipes[i].scored = false;
    }
}

void flappy_kiwi_init(void)
{
    load_high_score();
    s_bird_y = 24.0f;
    s_bird_vy = 0.0f;
    s_score = 0;
    s_game_over = false;
    s_started = false;
    s_exit_requested = false;
    s_ground_scroll = 0;
    reset_pipes();
}

bool flappy_kiwi_should_exit(void)
{
    return s_exit_requested;
}

static void flap(void)
{
    s_bird_vy = -2.3f;
    sfx_play(SFX_SELECT);
}

void flappy_kiwi_on_button(button_id_t btn)
{
    if (btn == BTN_KEY4) {
        sfx_play(SFX_SELECT);
        s_exit_requested = true;
        return;
    }

    if (s_game_over) {
        if (btn == BTN_KEY1 || btn == BTN_CENTER) {
            sfx_play(SFX_START);
            flappy_kiwi_init();
        }
        return;
    }

    // Flap with B1, Center, or physical UP (which arrives as BTN_DOWN)
    if (btn == BTN_KEY1 || btn == BTN_CENTER || btn == BTN_DOWN) {
        if (!s_started) {
            s_started = true;
        }
        flap();
    }
}

void flappy_kiwi_tick(void)
{
    if (!s_started || s_game_over) {
        return;
    }

    // Bird physics
    s_bird_vy += 0.22f; // Gravity
    if (s_bird_vy > 3.2f) s_bird_vy = 3.2f;
    s_bird_y += s_bird_vy;

    // Ground & ceiling collision
    if (s_bird_y + BIRD_H >= GROUND_Y) {
        s_bird_y = GROUND_Y - BIRD_H;
        s_game_over = true;
        sfx_play(SFX_LOSE);
        if (s_score > s_high_score) {
            s_high_score = s_score;
            save_high_score();
        }
        return;
    }
    if (s_bird_y < 0) {
        s_bird_y = 0;
        s_bird_vy = 0;
    }

    s_ground_scroll++;

    // Pipes update
    for (int i = 0; i < PIPE_COUNT; i++) {
        s_pipes[i].x -= 1.2f; // Scroll speed

        // Check if passed for scoring
        if (!s_pipes[i].scored && (s_pipes[i].x + PIPE_WIDTH < BIRD_X)) {
            s_pipes[i].scored = true;
            s_score++;
            sfx_play(SFX_MOVE);
            if (s_score > s_high_score) {
                s_high_score = s_score;
            }
        }

        // Check collision
        float px = s_pipes[i].x;
        if (px < BIRD_X + BIRD_W && px + PIPE_WIDTH > BIRD_X) {
            if (s_bird_y < s_pipes[i].gap_y || s_bird_y + BIRD_H > s_pipes[i].gap_y + PIPE_GAP_H) {
                s_game_over = true;
                sfx_play(SFX_LOSE);
                if (s_score >= s_high_score) {
                    s_high_score = s_score;
                    save_high_score();
                }
                return;
            }
        }

        // Recycle pipe
        if (s_pipes[i].x < -PIPE_WIDTH) {
            // Find the other pipe's x
            int other = (i + 1) % PIPE_COUNT;
            s_pipes[i].x = s_pipes[other].x + PIPE_SPACING;
            s_pipes[i].gap_y = 8 + (esp_random() % (GROUND_Y - PIPE_GAP_H - 14));
            s_pipes[i].scored = false;
        }
    }
}

void flappy_kiwi_draw(void)
{
    display_clear();

    // Draw Pipes
    for (int i = 0; i < PIPE_COUNT; i++) {
        int px = (int)s_pipes[i].x;
        if (px >= -PIPE_WIDTH && px < BOARD_LCD_WIDTH) {
            int gap_y = s_pipes[i].gap_y;

            // Top pipe
            display_draw_rect(px, 0, PIPE_WIDTH, gap_y);
            display_fill_rect(px - 1, gap_y - 3, PIPE_WIDTH + 2, 3, true);

            // Bottom pipe
            int bot_y = gap_y + PIPE_GAP_H;
            display_fill_rect(px - 1, bot_y, PIPE_WIDTH + 2, 3, true);
            display_draw_rect(px, bot_y + 3, PIPE_WIDTH, GROUND_Y - (bot_y + 3));
        }
    }

    // Draw Ground
    display_fill_rect(0, GROUND_Y, BOARD_LCD_WIDTH, 1, true);
    for (int x = 0; x < BOARD_LCD_WIDTH; x += 4) {
        int dot_x = (x - (int)(s_ground_scroll % 4) + BOARD_LCD_WIDTH) % BOARD_LCD_WIDTH;
        display_set_pixel(dot_x, GROUND_Y + 3, true);
    }

    // Draw Bird
    int by = (int)s_bird_y;
    for (int row = 0; row < BIRD_H; row++) {
        uint8_t byte = s_kiwi_bitmap[row];
        for (int col = 0; col < BIRD_W; col++) {
            if (byte & (0x80 >> col)) {
                display_set_pixel(BIRD_X + col, by + row, true);
            }
        }
    }

    // Draw Score
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", s_score);
    display_text(60, 2, buf, false);

    if (!s_started) {
        display_text(16, 22, "FLAPPY KIWI", true);
        display_text(8, 38, "[B1] Aletear", false);
        display_text(8, 48, "[B4] Menu", false);
    } else if (s_game_over) {
        display_fill_rect(14, 12, 100, 42, false);
        display_draw_rect(14, 12, 100, 42);
        display_text(26, 16, "GAME OVER", true);
        snprintf(buf, sizeof(buf), "Puntos: %02d", s_score);
        display_text(22, 28, buf, false);
        snprintf(buf, sizeof(buf), "Record: %02d", s_high_score);
        display_text(22, 36, buf, false);
        display_text(22, 44, "B1:Jugar B4:Menu", false);
    }

    display_flush();
}

