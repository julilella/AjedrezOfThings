#pragma once

/*
 * chessboard: the 5x5 board and the rules of our chess.
 * Knows nothing about turns, players or the network: match.c does.
 *
 * The board is board[y][x]: y is the row (0 at the top of the screen,
 * white starts on rows 0-1), x is the column. Both go 0..4.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PIECE_BISHOP = 0,
    PIECE_KNIGHT,
    PIECE_PAWN,
    PIECE_ROOK,
    PIECE_KING,
    PIECE_QUEEN,
    PIECE_EMPTY,
} piece_type_t;

typedef struct {
    piece_type_t piece;
    bool is_white;
} piece_t;

typedef struct {
    bool is_white;
} player_t;

typedef struct {
    piece_t sq[5][5];
} board_t;

/* Size of a board on the wire: one byte per square. */
#define BOARD_BYTES 25

/* The letter shown for a piece: A C P T R D, ' ' for an empty square. */
char piece_to_char(piece_type_t p);

/* Random start: 5 white pieces on rows 0-1, mirrored as black on rows 3-4. */
void chessboard_init(piece_t board[5][5]);

/* Checks if moving (x_init, y_init) -> (x_end, y_end) is legal for player,
 * without modifying the board. Returns true if legal, false otherwise. */
bool board_is_legal_move(int x_init, int y_init, int x_end, int y_end,
                         const piece_t board[5][5], player_t player);

/* If moving (x_init, y_init) -> (x_end, y_end) is legal for player, makes the
 * move (capture and promotion included) and returns true. If not, leaves the
 * board as it was and returns false. */
bool board_move_and_capture(int x_init, int y_init, int x_end, int y_end,
                            piece_t board[5][5], player_t player);

/* Counts pieces of the given color currently on the board. */
int board_count_pieces(const piece_t board[5][5], bool is_white);

/* The board as BOARD_BYTES bytes, row by row (y * 5 + x): the piece in the
 * low bits, plus 0x80 if white. */
void board_to_bytes(const board_t *board, uint8_t out[BOARD_BYTES]);
void board_from_bytes(board_t *board, const uint8_t in[BOARD_BYTES]);
