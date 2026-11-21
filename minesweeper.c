#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <termios.h>
#include <unistd.h>


#define BOARD_DIM 16
#define BOARD_SIZE 256
#define NUM_MINES 40

#define ADJ_MINES(x) (x & 0xFF)

#define IS_MINE(x) ((x >> 8) & 0x1)
#define SET_MINE(x) x |= 0x0100

#define IS_REVEALED(x) ((x >> 9) & 0x1)
#define SET_REVEALED(x) x |= 0x0200

#define IS_FLAGGED(x) ((x >> 10) & 0x1)
#define SET_FLAGGED(x) x |= 0x0400
#define SET_UNFLAGGED(x) x &= ~0x0400

#define IS_RESERVED(x) ((x >> 11) & 0x1)
#define SET_RESERVED(x) x |= 0x0800


#define VBUF_SIZE 4096
char vbuf[VBUF_SIZE];

#define CSI(x, t) "\x1B["#x#t
#define SGR(x) CSI(x, m)

const char * const FG_LUT[] = {
    NULL,

    SGR(94), /* 1 - Bright blue */
    SGR(32), /* 2 - Green */
    SGR(91), /* 3 - Bright red */
    SGR(34), /* 4 - Blue */
    SGR(31), /* 5 - Red */
    SGR(36), /* 6 - Cyan */
    SGR(37), /* 7 - White */
    SGR(97), /* 8 - Bright white */
};


typedef struct pos_list {
    unsigned char pos[9];
    unsigned char len;
} pos_list_t;

pos_list_t adj_pos(unsigned char pos, unsigned char incl) {
    pos_list_t adjs;
    unsigned char i, j;
    char di, dj;

    i = pos / BOARD_DIM;
    j = pos % BOARD_DIM;

    adjs.len = 0;

    for (di = -1; di <= 1; di++) {
        short ni = i + di;
        if (ni >= 0 && ni < BOARD_DIM) {
            for (dj = -1; dj <= 1; dj++) {
                if (incl || !(di == 0 && dj == 0)) {
                    short nj = j + dj;
                    if (nj >= 0 && nj < BOARD_DIM) {
                        unsigned char adj = ni * BOARD_DIM + nj;
                        adjs.pos[adjs.len++] = adj;
                    }
                }
            }
        }
    }

    return adjs;
}


unsigned short g_board[BOARD_SIZE];
unsigned char g_rem;
short g_flags;
unsigned char g_c_pos;
enum {
    READY_STATE,
    ACTIVE_STATE,
    WIN_STATE,
    LOSE_STATE
} g_state;


void init() {
    memset(g_board, 0, sizeof(g_board));

    g_rem = BOARD_SIZE - NUM_MINES;
    g_flags = NUM_MINES;
    g_c_pos = 0;
    g_state = READY_STATE;
}

void generate() {
    pos_list_t c_adjs = adj_pos(g_c_pos, 1);
    unsigned char n = BOARD_SIZE - c_adjs.len;
    unsigned char i;
    for (i = 0; i < c_adjs.len; i++) {
        SET_RESERVED(g_board[c_adjs.pos[i]]);
    }

    srand(time(NULL));

    for (i = 0; i < NUM_MINES; i++) {
        unsigned char r = rand() % n;
        unsigned char m = 0;
        while (1) {
            if (!IS_RESERVED(g_board[m])) {
                if (r == 0) {
                    break;
                }
                r--;
            }
            m++;
        }

        SET_MINE(g_board[m]);
        SET_RESERVED(g_board[m]);
        n--;

        /* Increment neighbors. */
        {
            pos_list_t m_adjs = adj_pos(m, 0);
            unsigned char j;
            for (j = 0; j < m_adjs.len; j++) {
                g_board[m_adjs.pos[j]]++;
            }
        }
    }
}

void draw() {
    /*
    Reset terminal cursor.
    Clear screen. 
    */
    printf(CSI(, H)CSI(,J));

    /* Draw header. */
    printf("%d / %u"CSI(0, K)"\n", g_flags, NUM_MINES);

    /* Draw board.*/
    {
        unsigned short i, j;

        for (i = 0; i < BOARD_DIM; i++) {
            for (j = 0; j < BOARD_DIM; j++) {
                unsigned char pos = i * BOARD_DIM + j;
                unsigned short val = g_board[pos];

                /* Highlight game cursor. */
                unsigned char highlight = g_state < WIN_STATE && pos == g_c_pos;
                if (highlight) {
                    /* Invert. */
                    printf(SGR(7));
                }

                if (IS_REVEALED(val)) {
                    if (IS_FLAGGED(val)) {
                        if (IS_MINE(val)) {
                            /* Correct flag. */

                            printf(SGR(30;47)"[F]");
                        } else {
                            /* Incorrect flag.*/

                            printf(SGR(91;40)" X ");
                        }
                    } else {
                        if (IS_MINE(val)) {
                            /* Regular mine. */

                            printf(SGR(30;41)" M ");
                        } else {
                            /* Number tile. */

                            unsigned char adj_m = ADJ_MINES(val);

                            printf(SGR(40));

                            if (adj_m == 0) {
                                printf("   ");
                            } else {
                                printf("%s %u ", FG_LUT[adj_m], adj_m);
                            }
                        }
                    }
                } else {
                    if (IS_FLAGGED(val)) {
                        /* Regular flag. */
                        printf(SGR(30;47)"[F]");
                    } else {
                        /* Covered tile. */
                        printf(SGR(30;47)"[ ]");
                    }
                }

                if (highlight) {
                    /* Reset. */
                    printf(SGR(0));
                }
            }
            
            /*
            Reset.
            Move cursor to the beginning of the next line.
            */
            printf(SGR(0)CSI(,E));
        }
    }

    /* Draw footer. */

    if (g_state == WIN_STATE) {
        printf("Win");
    } else if (g_state == LOSE_STATE) {
        printf("Lose");
    }

    fflush(stdout);
}

void reveal() {
    /* Reveal. */

    if (
        !IS_REVEALED(g_board[g_c_pos]) &&
        !IS_FLAGGED(g_board[g_c_pos])
    ) {
        if (g_state == READY_STATE) {
            generate();

            g_state = ACTIVE_STATE;
        }

        /* Check loss condition. */
        if (IS_MINE(g_board[g_c_pos])) {
            unsigned short i;
            g_state = LOSE_STATE;

            /* Reveal all tiles. */

            for (i = 0; i < BOARD_SIZE; i++) {
                SET_REVEALED(g_board[i]);
            }
        } else {
            /* Reveal. */

            unsigned char queue[BOARD_SIZE];
            unsigned char qp = 0;

            SET_REVEALED(g_board[g_c_pos]);
            g_rem--;
            queue[qp++] = g_c_pos;

            while (qp > 0) {
                unsigned char pos = queue[--qp];
                pos_list_t adjs = adj_pos(pos, 0);
                unsigned char i;

                if (ADJ_MINES(g_board[pos]) > 0) {
                    continue;
                }

                for (i = 0; i < adjs.len; i++) {
                    unsigned char adj = adjs.pos[i];
                    if (
                        !IS_REVEALED(g_board[adj]) &&
                        !IS_FLAGGED(g_board[adj])
                    ) {
                        SET_REVEALED(g_board[adj]);
                        g_rem--;
                        queue[qp++] = adj;
                    }
                }
            }

            /* Check win condition. */
            if (g_rem == 0) {
                unsigned short i;
                g_state = WIN_STATE;

                /* Flag all mines. */

                for (i = 0; i < BOARD_SIZE; i++) {
                    if (IS_MINE(g_board[i])) {
                        SET_FLAGGED(g_board[i]);
                    }
                }
                g_flags = 0;
            }
        }
    }
}

void flag() {
    if (!IS_REVEALED(g_board[g_c_pos])) {
        if (IS_FLAGGED(g_board[g_c_pos])) {
            SET_UNFLAGGED(g_board[g_c_pos]);
            g_flags++;
        } else {
            SET_FLAGGED(g_board[g_c_pos]);
            g_flags--;
        }
    }
}

int main() {
    struct termios og_term, raw_term;

    /* Initialize terminal. */

    tcgetattr(STDIN_FILENO, &og_term);

    raw_term = og_term;
    raw_term.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw_term.c_cc[VMIN] = 1;
    raw_term.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_term);

    setvbuf(stdout, vbuf, _IOFBF, VBUF_SIZE);

    /* Enable alternative buffer */
    printf(CSI(?1049, h));

    /* Hide terminal cursor. */
    printf(CSI(?25, l));

    /* Initialize game. */
    init();

    /* Game loop. */

    while (1) {
        char key;

        /* Draw game. */
        draw();

        /* Handle input. */

        if (
            read(STDIN_FILENO, &key, 1) != 1 ||
            key == 'q' ||
            key == '\x03'
        ) {
            break;
        }

        if (g_state < WIN_STATE) {
            if (key == 'w' && g_c_pos >= BOARD_DIM) {
                g_c_pos -= BOARD_DIM;
            } else if (key == 'a' && g_c_pos % BOARD_DIM > 0) {
                g_c_pos--;
            } else if (key == 's' && g_c_pos < BOARD_SIZE - BOARD_DIM) {
                g_c_pos += BOARD_DIM;
            } else if (key == 'd' && g_c_pos % BOARD_DIM < BOARD_DIM - 1) {
                g_c_pos++;
            } else if (key == '\n') {
                reveal();
            } else if (key == 'f') {
                flag();
            }
        }

        if (key == 'r') {
            init();
        }
    }

    /* Restore terminal cursor. */
    printf(CSI(?25, h));

    fflush(stdout);

    /* Disable alternative buffer. */
    printf(CSI(?1049, l));

    /* Restore terminal. */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &og_term);

    return 0;
}