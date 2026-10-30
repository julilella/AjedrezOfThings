#include "buscaminas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "display.h"
#include "sfx.h"

#define COLS 9
#define ROWS 7
#define TOTAL_CELLS (COLS * ROWS)
#define TOTAL_MINES 8

#define CELL_SIZE 8
#define GRID_X 3
#define GRID_Y 4

typedef struct {
    bool has_mine;
    bool is_revealed;
    bool is_flagged;
    uint8_t count;
} cell_t;

static cell_t s_grid[ROWS][COLS];
static int s_cursor_x = 0;
static int s_cursor_y = 0;
static bool s_first_move = true;
static bool s_game_over = false;
static bool s_won = false;
static bool s_exit_requested = false;

static uint32_t s_start_time_ms = 0;
static uint32_t s_elapsed_seconds = 0;

// Corrected 3x5 font (3 columns of 5 bits, bit 0 is top row)
static const uint8_t s_mini_font[9][3] = {
    {0x00, 0x00, 0x00}, // 0: empty
    {0x00, 0x1F, 0x00}, // 1: vertical bar
    {0x1D, 0x15, 0x17}, // 2: row0=###, row1=..#, row2=###, row3=#.., row4=###
    {0x15, 0x15, 0x1F}, // 3: row0=###, row1=..#, row2=###, row3=..#, row4=###
    {0x07, 0x04, 0x1F}, // 4: row0=#.#, row1=#.#, row2=###, row3=..#, row4=..#
    {0x17, 0x15, 0x1D}, // 5: row0=###, row1=#.., row2=###, row3=..#, row4=###
    {0x1F, 0x15, 0x1D}, // 6: row0=###, row1=#.., row2=###, row3=#.#, row4=###
    {0x01, 0x01, 0x1F}, // 7: row0=###, row1=..#, row2=..#, row3=..#, row4=..#
    {0x1F, 0x15, 0x1F}, // 8: row0=###, row1=#.#, row2=###, row3=#.#, row4=###
};

static void draw_mini_digit(int x, int y, int num)
{
    if (num < 1 || num > 8) return;
    const uint8_t *glyph = s_mini_font[num];
    for (int col = 0; col < 3; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 5; row++) {
            if (bits & (1u << row)) {
                display_set_pixel(x + col, y + row, true);
            }
        }
    }
}

static void count_neighbors(void)
{
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            s_grid[r][c].count = 0;
            if (s_grid[r][c].has_mine) continue;

            int cnt = 0;
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    if (dr == 0 && dc == 0) continue;
                    int nr = r + dr;
                    int nc = c + dc;
                    // Strict edge boundary check: border cells only count valid on-board neighbors
                    if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS) {
                        if (s_grid[nr][nc].has_mine) {
                            cnt++;
                        }
                    }
                }
            }
            s_grid[r][c].count = (uint8_t)cnt;
        }
    }
}

static void place_mines(int safe_c, int safe_r)
{
    // Clear whole grid first
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            s_grid[r][c].has_mine = false;
            s_grid[r][c].count = 0;
        }
    }

    int placed = 0;
    while (placed < TOTAL_MINES) {
        int r = esp_random() % ROWS;
        int c = esp_random() % COLS;
        // Never place a mine on or around the initial safe cell
        if (r >= safe_r - 1 && r <= safe_r + 1 && c >= safe_c - 1 && c <= safe_c + 1) {
            continue;
        }
        if (!s_grid[r][c].has_mine) {
            s_grid[r][c].has_mine = true;
            placed++;
        }
    }
    count_neighbors();
}

static void flood_reveal(int start_c, int start_r)
{
    typedef struct { uint8_t c, r; } point_t;
    point_t queue[TOTAL_CELLS];
    int head = 0, tail = 0;

    queue[tail++] = (point_t){(uint8_t)start_c, (uint8_t)start_r};
    s_grid[start_r][start_c].is_revealed = true;

    while (head < tail) {
        point_t p = queue[head++];
        if (s_grid[p.r][p.c].count == 0) {
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    if (dr == 0 && dc == 0) continue;
                    int nr = p.r + dr;
                    int nc = p.c + dc;
                    if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS) {
                        if (!s_grid[nr][nc].is_revealed && !s_grid[nr][nc].is_flagged) {
                            s_grid[nr][nc].is_revealed = true;
                            if (tail < TOTAL_CELLS) {
                                queue[tail++] = (point_t){(uint8_t)nc, (uint8_t)nr};
                            }
                        }
                    }
                }
            }
        }
    }
}

static void check_win(void)
{
    int rev_count = 0;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (s_grid[r][c].is_revealed && !s_grid[r][c].has_mine) {
                rev_count++;
            }
        }
    }
    if (rev_count == (TOTAL_CELLS - TOTAL_MINES)) {
        s_won = true;
        s_game_over = true;
        for (int r = 0; r < ROWS; r++) {
            for (int c = 0; c < COLS; c++) {
                if (s_grid[r][c].has_mine) {
                    s_grid[r][c].is_flagged = true;
                }
            }
        }
        sfx_play(SFX_WIN);
    }
}

void buscaminas_init(void)
{
    memset(s_grid, 0, sizeof(s_grid));
    s_cursor_x = COLS / 2;
    s_cursor_y = ROWS / 2;
    s_first_move = true;
    s_game_over = false;
    s_won = false;
    s_exit_requested = false;
    s_start_time_ms = 0;
    s_elapsed_seconds = 0;
}

bool buscaminas_should_exit(void)
{
    return s_exit_requested;
}

void buscaminas_tick(void)
{
    if (!s_first_move && !s_game_over) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (s_start_time_ms == 0) s_start_time_ms = now;
        s_elapsed_seconds = (now - s_start_time_ms) / 1000;
        if (s_elapsed_seconds > 999) s_elapsed_seconds = 999;
    }
}

void buscaminas_on_button(button_id_t btn)
{
    // Exit to Launcher
    if (btn == BTN_KEY4) {
        sfx_play(SFX_SELECT);
        s_exit_requested = true;
        return;
    }

    // Reset game
    if (btn == BTN_KEY3) {
        sfx_play(SFX_START);
        buscaminas_init();
        return;
    }

    if (s_game_over) {
        if (btn == BTN_KEY1 || btn == BTN_CENTER) {
            sfx_play(SFX_START);
            buscaminas_init();
        }
        return;
    }

    // Joystick: Hardware inverted UP/DOWN and LEFT/RIGHT
    if (btn == BTN_DOWN) {
        // Physical UP
        if (s_cursor_y > 0) {
            s_cursor_y--;
            sfx_play(SFX_SELECT);
        }
    } else if (btn == BTN_UP) {
        // Physical DOWN
        if (s_cursor_y < ROWS - 1) {
            s_cursor_y++;
            sfx_play(SFX_SELECT);
        }
    } else if (btn == BTN_RIGHT) {
        // Physical LEFT
        if (s_cursor_x > 0) {
            s_cursor_x--;
            sfx_play(SFX_SELECT);
        }
    } else if (btn == BTN_LEFT) {
        // Physical RIGHT
        if (s_cursor_x < COLS - 1) {
            s_cursor_x++;
            sfx_play(SFX_SELECT);
        }
    }
    // Action: Reveal cell
    else if (btn == BTN_KEY1 || btn == BTN_CENTER) {
        cell_t *cur = &s_grid[s_cursor_y][s_cursor_x];
        if (cur->is_revealed || cur->is_flagged) return;

        if (s_first_move) {
            place_mines(s_cursor_x, s_cursor_y);
            s_first_move = false;
            s_start_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }

        if (cur->has_mine) {
            cur->is_revealed = true;
            s_game_over = true;
            s_won = false;
            for (int r = 0; r < ROWS; r++) {
                for (int c = 0; c < COLS; c++) {
                    if (s_grid[r][c].has_mine) {
                        s_grid[r][c].is_revealed = true;
                    }
                }
            }
            sfx_play(SFX_LOSE);
            return;
        }

        flood_reveal(s_cursor_x, s_cursor_y);
        sfx_play(SFX_MOVE);
        check_win();
    }
    // Action: Toggle Flag
    else if (btn == BTN_KEY2) {
        cell_t *cur = &s_grid[s_cursor_y][s_cursor_x];
        if (!cur->is_revealed) {
            cur->is_flagged = !cur->is_flagged;
            sfx_play(SFX_SELECT);
        }
    }
}

void buscaminas_draw(void)
{
    display_clear();

    // Outer grid boundary framing the entire board
    display_draw_rect(GRID_X - 1, GRID_Y - 1, COLS * CELL_SIZE + 2, ROWS * CELL_SIZE + 2);

    // Draw Grid Cells
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int px = GRID_X + c * CELL_SIZE;
            int py = GRID_Y + r * CELL_SIZE;
            const cell_t *cell = &s_grid[r][c];
            bool is_cursor = (c == s_cursor_x && r == s_cursor_y);

            if (!cell->is_revealed) {
                // Unrevealed cell: border
                display_draw_rect(px, py, CELL_SIZE, CELL_SIZE);

                if (cell->is_flagged) {
                    // Flag icon (clean pole + banner)
                    display_set_pixel(px + 3, py + 1, true);
                    display_set_pixel(px + 3, py + 2, true);
                    display_set_pixel(px + 3, py + 3, true);
                    display_set_pixel(px + 3, py + 4, true);
                    display_set_pixel(px + 3, py + 5, true);
                    display_fill_rect(px + 4, py + 1, 2, 3, true);
                }
            } else {
                // Corner dots to keep cell boundaries visible when revealed
                display_set_pixel(px, py, true);
                display_set_pixel(px + CELL_SIZE - 1, py, true);
                display_set_pixel(px, py + CELL_SIZE - 1, true);
                display_set_pixel(px + CELL_SIZE - 1, py + CELL_SIZE - 1, true);

                if (cell->has_mine) {
                    // Mine icon with spikes
                    display_fill_rect(px + 2, py + 2, 4, 4, true);
                    display_set_pixel(px + 1, py + 3, true);
                    display_set_pixel(px + 6, py + 3, true);
                    display_set_pixel(px + 3, py + 1, true);
                    display_set_pixel(px + 3, py + 6, true);
                } else if (cell->count > 0) {
                    draw_mini_digit(px + 3, py + 2, cell->count);
                }
            }

            // Draw Cursor: double-line inner border so it never bleeds out
            if (is_cursor) {
                display_draw_rect(px, py, CELL_SIZE, CELL_SIZE);
                display_draw_rect(px + 1, py + 1, CELL_SIZE - 2, CELL_SIZE - 2);
            }
        }
    }

    // Side Info Panel (Only Minas & Tiempo - no button clutter!)
    char buf[16];

    int flags = 0;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (s_grid[r][c].is_flagged) flags++;
        }
    }
    int remaining = TOTAL_MINES - flags;

    if (s_game_over) {
        if (s_won) {
            display_text(80, 8, "VICTORIA", true);
        } else {
            display_text(84, 8, "BOOM!", true);
        }
        snprintf(buf, sizeof(buf), "T:%03lu s", (unsigned long)s_elapsed_seconds);
        display_text(80, 24, buf, false);
        display_text(80, 40, "B1: Otra", false);
        display_text(80, 52, "B4: Menu", false);
    } else {
        display_text(82, 8, "MINAS", false);
        snprintf(buf, sizeof(buf), "  %02d", remaining < 0 ? 0 : remaining);
        display_text(82, 18, buf, true);

        display_text(82, 34, "TIEMPO", false);
        snprintf(buf, sizeof(buf), " %03lu", (unsigned long)s_elapsed_seconds);
        display_text(82, 44, buf, false);
    }

    display_flush();
}
