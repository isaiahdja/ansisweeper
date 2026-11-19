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


#define CSI(x, t) "\x1B["#x#t
#define SGR(x) CSI(x, m)
const char * const FG_LUT[] = {
    SGR(94), /* 1 - Bright blue */
    SGR(32), /* 2 - Green */
    SGR(91), /* 3 - Bright red */
    SGR(34), /* 4 - Blue */
    SGR(31), /* 5 - Red */
    SGR(36), /* 6 - Cyan */
    SGR(37), /* 7 - White */
    SGR(97), /* 8 - Bright white */
};


typedef enum g_state {
    READY_STATE,
    ACTIVE_STATE,
    WIN_STATE,
    LOSE_STATE
} state_t;

typedef struct pos_list {
    unsigned char pos[9];
    unsigned char len;
} pos_list_t;


pos_list_t adj_pos(unsigned char pos, unsigned char incl) {
    pos_list_t adjs;
    unsigned char y, x;
    char dy, dx;

    y = pos / BOARD_DIM;
    x = pos % BOARD_DIM;

    adjs.len = 0;

    for (dy = -1; dy <= 1; dy++) {
        short ny = y + dy;
        if (ny >= 0 && ny < BOARD_DIM) {
            for (dx = -1; dx <= 1; dx++) {
                if (incl || !(dy == 0 && dx == 0)) {
                    short nx = x + dx;
                    if (nx >= 0 && nx < BOARD_DIM) {
                        unsigned char adj = ny * BOARD_DIM + nx;
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
state_t g_state;


void init() {
    unsigned short i;
    for (i = 0; i != BOARD_SIZE; i++) {
        g_board[i] = 0;
    }

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
    /* Reset terminal cursor. */
    printf(CSI(, H));

    /* Draw header. */

    printf("Minesweeper\n");
    printf("wasd : move | enter : reveal | f : flag | r : reset | q : quit\n");

    /* Draw g_board.*/
    {
        unsigned short y, x;

        for (y = 0; y < BOARD_DIM; y++) {
            for (x = 0; x < BOARD_DIM; x++) {
                unsigned char pos = y * BOARD_DIM + x;
                unsigned short val = g_board[pos];

                /* Highlight game cursor. */
                if (g_state != WIN_STATE && pos == g_c_pos) {
                    printf(SGR(7));
                }

                if (IS_REVEALED(val)) {
                    if (IS_FLAGGED(val)) {
                        if (IS_MINE(val)) {
                            /* Correct (regular) flag. */

                            printf(SGR(35;100)" F ");
                        } else {
                            /* Incorrect flag.*/

                            printf(SGR(31;40)" X ");
                        }
                    } else {
                        if (IS_MINE(val)) {
                            /* Regular mine. */

                            printf(SGR(31;40)" M ");
                        } else {
                            /* Number tile. */

                            unsigned char adj_m = ADJ_MINES(val);
                            printf(SGR(40));
                            if (adj_m == 0) {
                                printf(SGR(37)"   ");
                            } else {
                                printf("%s %u ", FG_LUT[adj_m - 1], adj_m);
                            }
                        }
                    }
                } else {
                    if (IS_FLAGGED(val)) {
                        /* Regular flag. */

                        printf(SGR(35;100)" F ");
                    } else {
                        /* Covered tile. */
                        
                        printf(SGR(37;100)"   ");
                    }
                }

                /* Reset. */
                printf(SGR(0));
            }
            
            printf("\n");
        }
    }

    /* Draw footer. */

    printf("%d / %u"CSI(0, K)"\n", g_flags, NUM_MINES);

    if (g_state == WIN_STATE) {
        printf("Win\n");
    } else if (g_state == LOSE_STATE) {
        printf("Lose\n");
    } else {
        printf(CSI(0, K)"\n");
    }
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