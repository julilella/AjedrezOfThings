#include "kiwi_runner.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "board.h"
#include "display.h"
#include "sfx.h"

#define GROUND_Y        52
#define KIWI_X          16
#define KIWI_NORMAL_Y   44
#define KIWI_NORMAL_H   8
#define KIWI_NORMAL_W   8
#define KIWI_DUCK_Y     47
#define KIWI_DUCK_H     5
#define KIWI_DUCK_W     8

#define MAX_OBSTACLES   2
#define NVS_NS_RUNNER   "runner_cfg"
#define NVS_KEY_HI      "hi_score"

typedef enum {
    OBS_CACTUS_SMALL = 0,
    OBS_CACTUS_LARGE,
    OBS_FLYING_BIRD,
    OBS_TYPE_COUNT
} obstacle_type_t;

typedef struct {
    float x;
    obstacle_type_t type;
    int w, h, y;
    bool active;
} obstacle_t;

static float s_kiwi_y = KIWI_NORMAL_Y;
static float s_kiwi_vy = 0.0f;
static bool s_is_jumping = false;
static bool s_is_ducking = false;
static int s_duck_timer = 0;

static obstacle_t s_obstacles[MAX_OBSTACLES];
static float s_speed = 1.8f;
static int s_score = 0;
static int s_high_score = 0;
static bool s_game_over = false;
static bool s_started = false;
static bool s_exit_requested = false;
static uint32_t s_frame_count = 0;

// Run Frame 1
static const uint8_t s_sprite_run1[8] = {
    0x38, // ..xxx...
    0x7C, // .xxxxx..
    0xFA, // xxxxx.x.
    0xFE, // xxxxxxx.
    0x7E, // .xxxxxx.
    0x3C, // ..xxxx..
    0x28, // ..x.x...
    0x20, // ..x.....
};

// Run Frame 2
static const uint8_t s_sprite_run2[8] = {
    0x38, // ..xxx...
    0x7C, // .xxxxx..
    0xFA, // xxxxx.x.
    0xFE, // xxxxxxx.
    0x7E, // .xxxxxx.
    0x3C, // ..xxxx..
    0x14, // ...x.x..
    0x04, // .....x..
};

// Duck Frame
static const uint8_t s_sprite_duck[5] = {
    0x3F, // ..xxxxxx
    0x7F, // .xxxxxxx
    0xFA, // xxxxx.x.
    0xFE, // xxxxxxx.
    0x28, // ..x.x...
};

static void load_high_score(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS_RUNNER, NVS_READONLY, &nvs) == ESP_OK) {
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
    if (nvs_open(NVS_NS_RUNNER, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u32(nvs, NVS_KEY_HI, (uint32_t)s_high_score);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void spawn_obstacle(int idx, float start_x)
{
    obstacle_type_t type = (obstacle_type_t)(esp_random() % OBS_TYPE_COUNT);
    s_obstacles[idx].type = type;
    s_obstacles[idx].x = start_x;
    s_obstacles[idx].active = true;

    switch (type) {
    case OBS_CACTUS_SMALL:
        s_obstacles[idx].w = 4;
        s_obstacles[idx].h = 9;
        s_obstacles[idx].y = GROUND_Y - 9;
        break;
    case OBS_CACTUS_LARGE:
        s_obstacles[idx].w = 8;
        s_obstacles[idx].h = 12;
        s_obstacles[idx].y = GROUND_Y - 12;
        break;
    case OBS_FLYING_BIRD:
        s_obstacles[idx].w = 8;
        s_obstacles[idx].h = 5;
        // Flies at head height so player MUST duck
        s_obstacles[idx].y = GROUND_Y - 12;
        break;
    default:
        break;
    }
}

void kiwi_runner_init(void)
{
    load_high_score();
    s_kiwi_y = KIWI_NORMAL_Y;
    s_kiwi_vy = 0.0f;
    s_is_jumping = false;
    s_is_ducking = false;
    s_duck_timer = 0;
    s_speed = 1.8f;
    s_score = 0;
    s_game_over = false;
    s_started = false;
    s_exit_requested = false;
    s_frame_count = 0;

    spawn_obstacle(0, 130.0f);
    spawn_obstacle(1, 210.0f);
}

bool kiwi_runner_should_exit(void)
{
    return s_exit_requested;
}

static void jump(void)
{
    if (!s_is_jumping) {
        s_is_jumping = true;
        s_is_ducking = false;
        s_kiwi_vy = -3.2f;
        sfx_play(SFX_SELECT);
    }
}

static void duck(void)
{
    if (!s_is_jumping) {
        s_is_ducking = true;
        s_duck_timer = 18; // Duck for ~18 frames (~450ms)
        sfx_play(SFX_SELECT);
    }
}

void kiwi_runner_on_button(button_id_t btn)
{
    if (btn == BTN_KEY4) {
        sfx_play(SFX_SELECT);
        s_exit_requested = true;
        return;
    }

    if (s_game_over) {
        if (btn == BTN_KEY1 || btn == BTN_CENTER) {
            sfx_play(SFX_START);
            kiwi_runner_init();
        }
        return;
    }

    // Jump: B1, Center, or physical UP (BTN_DOWN)
    if (btn == BTN_KEY1 || btn == BTN_CENTER || btn == BTN_DOWN) {
        if (!s_started) {
            s_started = true;
        }
        jump();
    }
    // Duck: B2 or physical DOWN (BTN_UP)
    else if (btn == BTN_KEY2 || btn == BTN_UP) {
        if (!s_started) {
            s_started = true;
        }
        duck();
    }
}

void kiwi_runner_tick(void)
{
    if (!s_started || s_game_over) {
        return;
    }

    s_frame_count++;

    // Increment score
    if (s_frame_count % 2 == 0) {
        s_score++;
        if (s_score > s_high_score) {
            s_high_score = s_score;
        }
        if (s_score % 100 == 0) {
            sfx_play(SFX_START);
        }
        // Slowly increase speed up to 3.4f
        if (s_score % 50 == 0 && s_speed < 3.4f) {
            s_speed += 0.08f;
        }
    }

    // Duck timer
    if (s_is_ducking) {
        if (s_duck_timer > 0) {
            s_duck_timer--;
        } else {
            s_is_ducking = false;
        }
    }

    // Jump physics
    if (s_is_jumping) {
        s_kiwi_vy += 0.28f;
        s_kiwi_y += s_kiwi_vy;
        if (s_kiwi_y >= KIWI_NORMAL_Y) {
            s_kiwi_y = KIWI_NORMAL_Y;
            s_kiwi_vy = 0.0f;
            s_is_jumping = false;
        }
    } else {
        s_kiwi_y = s_is_ducking ? KIWI_DUCK_Y : KIWI_NORMAL_Y;
    }

    // Current player bounding box
    int kx = KIWI_X;
    int ky = (int)s_kiwi_y;
    int kw = s_is_ducking ? KIWI_DUCK_W : KIWI_NORMAL_W;
    int kh = s_is_ducking ? KIWI_DUCK_H : KIWI_NORMAL_H;

    // Obstacles update & collision
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        s_obstacles[i].x -= s_speed;

        // Collision check (AABB with 1px inset tolerance)
        int ox = (int)s_obstacles[i].x;
        int oy = s_obstacles[i].y;
        int ow = s_obstacles[i].w;
        int oh = s_obstacles[i].h;

        if (kx + kw - 1 > ox + 1 && kx + 1 < ox + ow - 1) {
            if (ky + kh - 1 > oy + 1 && ky + 1 < oy + oh - 1) {
                // Crash!
                s_game_over = true;
                sfx_play(SFX_LOSE);
                if (s_score >= s_high_score) {
                    s_high_score = s_score;
                    save_high_score();
                }
                return;
            }
        }

        // Recycle obstacle
        if (s_obstacles[i].x < -12) {
            int other = (i + 1) % MAX_OBSTACLES;
            float new_x = s_obstacles[other].x + 75.0f + (esp_random() % 45);
            if (new_x < 130.0f) new_x = 130.0f;
            spawn_obstacle(i, new_x);
        }
    }
}

void kiwi_runner_draw(void)
{
    display_clear();

    // Top HUD
    char buf[24];
    snprintf(buf, sizeof(buf), "SC:%04d HI:%04d", s_score, s_high_score);
    display_text(16, 2, buf, false);

    // Ground line
    display_fill_rect(0, GROUND_Y, BOARD_LCD_WIDTH, 1, true);

    // Ground pebbles
    int ground_offset = (int)(s_frame_count * s_speed) % 12;
    for (int x = -ground_offset; x < BOARD_LCD_WIDTH; x += 12) {
        if (x >= 0 && x < BOARD_LCD_WIDTH) {
            display_set_pixel(x, GROUND_Y + 3, true);
            display_set_pixel(x + 5, GROUND_Y + 7, true);
        }
    }

    // Draw Obstacles
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        int ox = (int)s_obstacles[i].x;
        if (ox >= -10 && ox < BOARD_LCD_WIDTH) {
            int oy = s_obstacles[i].y;
            int oh = s_obstacles[i].h;

            if (s_obstacles[i].type == OBS_CACTUS_SMALL) {
                display_fill_rect(ox + 1, oy, 2, oh, true);
                display_set_pixel(ox, oy + 2, true);
                display_set_pixel(ox + 3, oy + 4, true);
            } else if (s_obstacles[i].type == OBS_CACTUS_LARGE) {
                display_fill_rect(ox + 1, oy, 2, oh, true);
                display_fill_rect(ox + 5, oy + 3, 2, oh - 3, true);
                display_set_pixel(ox, oy + 4, true);
                display_set_pixel(ox + 7, oy + 6, true);
            } else if (s_obstacles[i].type == OBS_FLYING_BIRD) {
                // Flying bird wing flap
                bool wing_up = (s_frame_count / 6) % 2;
                display_fill_rect(ox + 2, oy + 2, 4, 2, true);
                display_set_pixel(ox, oy + 1, true); // beak
                if (wing_up) {
                    display_set_pixel(ox + 3, oy, true);
                    display_set_pixel(ox + 4, oy, true);
                } else {
                    display_set_pixel(ox + 3, oy + 4, true);
                    display_set_pixel(ox + 4, oy + 4, true);
                }
            }
        }
    }

    // Draw Kiwi
    int kx = KIWI_X;
    int ky = (int)s_kiwi_y;

    if (s_is_ducking) {
        for (int r = 0; r < KIWI_DUCK_H; r++) {
            uint8_t byte = s_sprite_duck[r];
            for (int c = 0; c < KIWI_DUCK_W; c++) {
                if (byte & (0x80 >> c)) {
                    display_set_pixel(kx + c, ky + r, true);
                }
            }
        }
    } else {
        const uint8_t *frame = ((s_frame_count / 4) % 2 == 0) ? s_sprite_run1 : s_sprite_run2;
        if (s_is_jumping) frame = s_sprite_run1;
        for (int r = 0; r < KIWI_NORMAL_H; r++) {
            uint8_t byte = frame[r];
            for (int c = 0; c < KIWI_NORMAL_W; c++) {
                if (byte & (0x80 >> c)) {
                    display_set_pixel(kx + c, ky + r, true);
                }
            }
        }
    }

    // Screen States
    if (!s_started) {
        display_text(26, 18, "KIWI RUNNER", true);
        display_text(8, 30, "[B1] Saltar", false);
        display_text(8, 40, "[B2] Agachar", false);
    } else if (s_game_over) {
        display_fill_rect(14, 12, 100, 42, false);
        display_draw_rect(14, 12, 100, 42);
        display_text(26, 16, "GAME OVER", true);
        snprintf(buf, sizeof(buf), "Puntos: %04d", s_score);
        display_text(22, 28, buf, false);
        snprintf(buf, sizeof(buf), "Record: %04d", s_high_score);
        display_text(22, 36, buf, false);
        display_text(22, 44, "B1:Jugar B4:Menu", false);
    }

    display_flush();
}

