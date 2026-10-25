#include <stdlib.h>

#include "esp_random.h"

#include "chessboard.h"

static const char s_piece_char[] = {
    [PIECE_BISHOP] = 'A',
    [PIECE_KNIGHT] = 'C',
    [PIECE_PAWN]   = 'P',
    [PIECE_ROOK]   = 'T',
    [PIECE_KING]   = 'R',
    [PIECE_QUEEN]  = 'D',
    [PIECE_EMPTY]  = ' ',
};

char piece_to_char(piece_type_t p)
{
    if (p < 0 || p > PIECE_EMPTY) {
        return '?';
    }
    return s_piece_char[p];
}

static bool is_path_clear(int x_init, int y_init, int x_end, int y_end, const piece_t board[5][5])
{
    int dx = (x_end > x_init) - (x_end < x_init);   // -1, 0, or 1
    int dy = (y_end > y_init) - (y_end < y_init);

    int x = x_init + dx;
    int y = y_init + dy;
    while (x != x_end || y != y_end) {
        if (board[y][x].piece != PIECE_EMPTY) {
            return false;
        }
        x += dx;
        y += dy;
    }
    return true;
}

void chessboard_init(piece_t board[5][5])
{
    int pieces_to_init = 5;

    // 1. Clear the board
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            board[y][x] = (piece_t){.piece = PIECE_EMPTY, .is_white = false};
        }
    }

    // 2. List of 10 possible squares for white (rows 0 and 1)
    int squares[10];
    for (int i = 0; i < 10; i++) {
        squares[i] = i;          // square i -> row i / 5, col i % 5
    }

    // 3. Shuffle randomly (Fisher-Yates) to pick positions without repetition
    for (int i = 9; i > 0; i--) {
        int j = esp_random() % (i + 1);
        int tmp = squares[i];
        squares[i] = squares[j];
        squares[j] = tmp;
    }

    // 4. Place each white piece and its black mirror
    for (int i = 0; i < pieces_to_init; i++) {
        piece_type_t type = esp_random() % PIECE_EMPTY;  // Bishop to Queen, never Empty

        int y = squares[i] / 5;
        int x = squares[i] % 5;

        board[y][x]     = (piece_t){.piece = type, .is_white = true};   // white
        board[4 - y][x] = (piece_t){.piece = type, .is_white = false};  // black, mirrored
    }
}

bool board_is_legal_move(int x_init, int y_init, int x_end, int y_end,
                         const piece_t board[5][5], player_t player)
{
    if (x_init < 0 || x_init >= 5 || y_init < 0 || y_init >= 5 ||
        x_end < 0 || x_end >= 5 || y_end < 0 || y_end >= 5) {
        return false;
    }
    if (x_init == x_end && y_init == y_end) {
        return false;
    }

    piece_t piece_to_move = board[y_init][x_init];
    piece_t piece_to_capture = board[y_end][x_end];
    bool dst_empty = piece_to_capture.piece == PIECE_EMPTY;

    if (piece_to_move.piece == PIECE_EMPTY || piece_to_move.is_white != player.is_white) {
        return false;
    }
    if (!dst_empty && piece_to_capture.is_white == player.is_white) {
        return false;
    }

    int dx = x_end - x_init;
    int dy = y_end - y_init;
    int adx = abs(dx);
    int ady = abs(dy);
    bool valid = false;

    switch (piece_to_move.piece) {
    case PIECE_ROOK:     // straight line
        valid = (dx == 0 || dy == 0) && is_path_clear(x_init, y_init, x_end, y_end, board);
        break;

    case PIECE_BISHOP:   // diagonal
        valid = (adx == ady) && is_path_clear(x_init, y_init, x_end, y_end, board);
        break;

    case PIECE_QUEEN:    // rook + bishop
        valid = (dx == 0 || dy == 0 || adx == ady) && is_path_clear(x_init, y_init, x_end, y_end, board);
        break;

    case PIECE_KNIGHT:   // L-shape, can jump over pieces
        valid = (adx == 1 && ady == 2) || (adx == 2 && ady == 1);
        break;

    case PIECE_KING:     // one square in any direction
        valid = adx <= 1 && ady <= 1;
        break;

    case PIECE_PAWN: {
        // White moves toward increasing y (starts at top, row 0).
        int forward = piece_to_move.is_white ? 1 : -1;
        if (dx == 0 && dy == forward && dst_empty) {
            valid = true;                       // advance one square
        } else if (adx == 1 && dy == forward && !dst_empty) {
            valid = true;                       // capture diagonally
        }
        break;
    }

    default:
        break;
    }

    return valid;
}

bool board_move_and_capture(int x_init, int y_init, int x_end, int y_end,
                            piece_t board[5][5], player_t player)
{
    if (!board_is_legal_move(x_init, y_init, x_end, y_end, board, player)) {
        return false;
    }

    piece_t piece_to_move = board[y_init][x_init];

    // Execute move
    board[y_end][x_end] = piece_to_move;
    board[y_init][x_init] = (piece_t){.piece = PIECE_EMPTY, .is_white = false};

    // Promotion: pawn reaching the last rank is promoted to queen
    if (piece_to_move.piece == PIECE_PAWN && (y_end == 0 || y_end == 4)) {
        board[y_end][x_end].piece = PIECE_QUEEN;
    }

    return true;
}

int board_count_pieces(const piece_t board[5][5], bool is_white)
{
    int count = 0;
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            if (board[y][x].piece != PIECE_EMPTY && board[y][x].is_white == is_white) {
                count++;
            }
        }
    }
    return count;
}

#define WHITE_BIT 0x80

void board_to_bytes(const board_t *board, uint8_t out[BOARD_BYTES])
{
    for (int i = 0; i < BOARD_BYTES; i++) {
        const piece_t p = board->sq[i / 5][i % 5];
        out[i] = p.piece | (p.is_white ? WHITE_BIT : 0);
    }
}

void board_from_bytes(board_t *board, const uint8_t in[BOARD_BYTES])
{
    for (int i = 0; i < BOARD_BYTES; i++) {
        const uint8_t piece = in[i] & ~WHITE_BIT;
        board->sq[i / 5][i % 5] = (piece_t){
            .piece = piece <= PIECE_EMPTY ? piece : PIECE_EMPTY,  /* garbage becomes an empty square */
            .is_white = in[i] & WHITE_BIT,
        };
    }
}
