#include "snake.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "display.h"
#include "sfx.h"

#define GRID_W          28
#define GRID_H          12
#define CELL_SIZE       4
#define GRID_OFFSET_X   8
#define GRID_OFFSET_Y   13

#define MAX_SNAKE_LEN   200
#define NVS_NS_SNAKE    "snake_cfg"
#define NVS_KEY_HI      "hi_score"

typedef enum {
    DIR_UP = 0,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} direction_t;

typedef struct {
    uint8_t x, y;
} point_t;

static point_t s_snake[MAX_SNAKE_LEN];
static int s_snake_len = 3;
static direction_t s_dir = DIR_RIGHT;
static direction_t s_next_dir = DIR_RIGHT;

static point_t s_food;
static int s_score = 0;
static int s_high_score = 0;

static bool s_game_over = false;
static bool s_exit_requested = false;
static uint32_t s_last_move_time_ms = 0;
static uint32_t s_speed_interval_ms = 130;

static void load_high_score(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS_SNAKE, NVS_READONLY, &nvs) == ESP_OK) {
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
    if (nvs_open(NVS_NS_SNAKE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u32(nvs, NVS_KEY_HI, (uint32_t)s_high_score);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void spawn_food(void)
{
    bool on_snake;
    do {
        on_snake = false;
        s_food.x = esp_random() % GRID_W;
        s_food.y = esp_random() % GRID_H;
        for (int i = 0; i < s_snake_len; i++) {
            if (s_snake[i].x == s_food.x && s_snake[i].y == s_food.y) {
                on_snake = true;
                break;
            }
        }
    } while (on_snake);
}

void snake_init(void)
{
    load_high_score();

    s_snake_len = 3;
    s_snake[0] = (point_t){8, 6}; // Head
    s_snake[1] = (point_t){7, 6};
    s_snake[2] = (point_t){6, 6};

    s_dir = DIR_RIGHT;
    s_next_dir = DIR_RIGHT;

    s_score = 0;
    s_game_over = false;
    s_exit_requested = false;
    s_speed_interval_ms = 130;
    s_last_move_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    spawn_food();
}

bool snake_should_exit(void)
{
    return s_exit_requested;
}

void snake_on_button(button_id_t btn)
{
    if (btn == BTN_KEY4) {
        sfx_play(SFX_SELECT);
        s_exit_requested = true;
        return;
    }

    if (s_game_over) {
        if (btn == BTN_KEY1 || btn == BTN_CENTER) {
            sfx_play(SFX_START);
            snake_init();
        }
        return;
    }

    // Physical UP is BTN_DOWN
    if (btn == BTN_DOWN) {
        if (s_dir != DIR_DOWN) {
            s_next_dir = DIR_UP;
            sfx_play(SFX_SELECT);
        }
    }
    // Physical DOWN is BTN_UP
    else if (btn == BTN_UP) {
        if (s_dir != DIR_UP) {
            s_next_dir = DIR_DOWN;
            sfx_play(SFX_SELECT);
        }
    }
    // Physical LEFT is BTN_RIGHT
    else if (btn == BTN_RIGHT) {
        if (s_dir != DIR_RIGHT) {
            s_next_dir = DIR_LEFT;
            sfx_play(SFX_SELECT);
        }
    }
    // Physical RIGHT is BTN_LEFT
    else if (btn == BTN_LEFT) {
        if (s_dir != DIR_LEFT) {
            s_next_dir = DIR_RIGHT;
            sfx_play(SFX_SELECT);
        }
    }
}

void snake_tick(void)
{
    if (s_game_over) {
        return;
    }

    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - s_last_move_time_ms < s_speed_interval_ms) {
        return;
    }
    s_last_move_time_ms = now;

    s_dir = s_next_dir;

    // Calculate new head position
    int new_x = s_snake[0].x;
    int new_y = s_snake[0].y;

    switch (s_dir) {
    case DIR_UP:    new_y--; break;
    case DIR_DOWN:  new_y++; break;
    case DIR_LEFT:  new_x--; break;
    case DIR_RIGHT: new_x++; break;
    }

    // Wall collision
    if (new_x < 0 || new_x >= GRID_W || new_y < 0 || new_y >= GRID_H) {
        s_game_over = true;
        sfx_play(SFX_LOSE);
        if (s_score > s_high_score) {
            s_high_score = s_score;
            save_high_score();
        }
        return;
    }

    // Self collision
    for (int i = 0; i < s_snake_len - 1; i++) {
        if (s_snake[i].x == new_x && s_snake[i].y == new_y) {
            s_game_over = true;
            sfx_play(SFX_LOSE);
            if (s_score > s_high_score) {
                s_high_score = s_score;
                save_high_score();
            }
            return;
        }
    }

    // Check if food eaten
    bool eating = (new_x == s_food.x && new_y == s_food.y);

    // Move body
    if (eating) {
        if (s_snake_len < MAX_SNAKE_LEN) {
            s_snake_len++;
        }
        s_score++;
        if (s_score > s_high_score) {
            s_high_score = s_score;
        }
        sfx_play(SFX_MOVE);
        spawn_food();

        // Speed up slightly
        if (s_speed_interval_ms > 60) {
            s_speed_interval_ms -= 2;
        }
    }

    for (int i = s_snake_len - 1; i > 0; i--) {
        s_snake[i] = s_snake[i - 1];
    }
    s_snake[0] = (point_t){(uint8_t)new_x, (uint8_t)new_y};
}

void snake_draw(void)
{
    display_clear();

    // Top HUD
    char buf[28];
    snprintf(buf, sizeof(buf), "VIBORITA SC:%02d HI:%02d", s_score, s_high_score);
    display_text(4, 2, buf, false);

    // Outer grid border
    display_draw_rect(GRID_OFFSET_X - 1, GRID_OFFSET_Y - 1,
                      GRID_W * CELL_SIZE + 2, GRID_H * CELL_SIZE + 2);

    // Draw Food (diamond pattern)
    int fx = GRID_OFFSET_X + s_food.x * CELL_SIZE;
    int fy = GRID_OFFSET_Y + s_food.y * CELL_SIZE;
    display_set_pixel(fx + 1, fy, true);
    display_set_pixel(fx, fy + 1, true);
    display_set_pixel(fx + 2, fy + 1, true);
    display_set_pixel(fx + 1, fy + 2, true);

    // Draw Snake
    for (int i = 0; i < s_snake_len; i++) {
        int sx = GRID_OFFSET_X + s_snake[i].x * CELL_SIZE;
        int sy = GRID_OFFSET_Y + s_snake[i].y * CELL_SIZE;
        if (i == 0) {
            // Head: filled 3x3 block with eye
            display_fill_rect(sx, sy, CELL_SIZE - 1, CELL_SIZE - 1, true);
        } else {
            // Body: 3x3 block with small border gap
            display_fill_rect(sx, sy, CELL_SIZE - 1, CELL_SIZE - 1, true);
        }
    }

    // Game Over Overlay
    if (s_game_over) {
        display_fill_rect(20, 18, 88, 32, false);
        display_draw_rect(20, 18, 88, 32);
        display_text(30, 22, "GAME OVER", true);
        display_text(26, 36, "B1:Otra B4:Menu", false);
    }

    display_flush();
}

