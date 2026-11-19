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
const char * const fg_lut[] = {
    SGR(94), /* 1 - Bright blue */
    SGR(32), /* 2 - Green */
    SGR(91), /* 3 - Bright red */
    SGR(34), /* 4 - Blue */
    SGR(31), /* 5 - Red */
    SGR(36), /* 6 - Cyan */
    SGR(37), /* 7 - White */
    SGR(97), /* 8 - Bright white */
};


typedef enum state {
    INACTIVE,
    READY,
    ACTIVE,
    WIN,
    LOSE
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


unsigned short board[BOARD_SIZE];
unsigned char rem;
short flags;
unsigned char c_pos;
state_t state;


void init() {
    unsigned short i;
    for (i = 0; i != BOARD_SIZE; i++) {
        board[i] = 0;
    }

    rem = BOARD_SIZE - NUM_MINES;
    flags = NUM_MINES;
    c_pos = 0;
    state = READY;
}

void generate() {
    pos_list_t c_adjs = adj_pos(c_pos, 1);
    unsigned char n = BOARD_SIZE - c_adjs.len;
    unsigned char i;
    for (i = 0; i < c_adjs.len; i++) {
        SET_RESERVED(board[c_adjs.pos[i]]);
    }

    srand(time(NULL));

    for (i = 0; i < NUM_MINES; i++) {
        unsigned char r = rand() % n;
        unsigned char m = 0;
        while (1) {
            if (!IS_RESERVED(board[m])) {
                if (r == 0) {
                    break;
                }
                r--;
            }
            m++;
        }

        SET_MINE(board[m]);
        SET_RESERVED(board[m]);
        n--;

        /* Increment neighbors. */
        {
            pos_list_t m_adjs = adj_pos(m, 0);
            unsigned char j;
            for (j = 0; j < m_adjs.len; j++) {
                board[m_adjs.pos[j]]++;
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

    /* Draw board.*/
    {
        unsigned short y, x;

        for (y = 0; y < BOARD_DIM; y++) {
            for (x = 0; x < BOARD_DIM; x++) {
                unsigned char pos = y * BOARD_DIM + x;
                unsigned short val = board[pos];

                /* Highlight game cursor. */
                if (state != WIN && pos == c_pos) {
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
                                printf("%s %u ", fg_lut[adj_m - 1], adj_m);
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

    printf("%d / %u"CSI(0, K)"\n", flags, NUM_MINES);

    if (state == WIN) {
        printf("Win\n");
    } else if (state == LOSE) {
        printf("Lose\n");
    } else {
        printf(CSI(0, K)"\n");
    }
}

void reveal() {
    /* Reveal. */

    if (
        !IS_REVEALED(board[c_pos]) &&
        !IS_FLAGGED(board[c_pos])
    ) {
        if (state == READY) {
            generate();

            state = ACTIVE;
        }

        /* Check loss condition. */
        if (IS_MINE(board[c_pos])) {
            unsigned short i;
            state = LOSE;

            /* Reveal all tiles. */

            for (i = 0; i < BOARD_SIZE; i++) {
                SET_REVEALED(board[i]);
            }
        } else {
            /* Reveal. */

            unsigned char queue[BOARD_SIZE];
            unsigned char qp = 0;

            SET_REVEALED(board[c_pos]);
            rem--;
            queue[qp++] = c_pos;

            while (qp > 0) {
                unsigned char pos = queue[--qp];
                pos_list_t adjs = adj_pos(pos, 0);
                unsigned char i;

                if (ADJ_MINES(board[pos]) > 0) {
                    continue;
                }

                for (i = 0; i < adjs.len; i++) {
                    unsigned char adj = adjs.pos[i];
                    if (
                        !IS_REVEALED(board[adj]) &&
                        !IS_FLAGGED(board[adj])
                    ) {
                        SET_REVEALED(board[adj]);
                        rem--;
                        queue[qp++] = adj;
                    }
                }
            }

            /* Check win condition. */
            if (rem == 0) {
                unsigned short i;
                state = WIN;

                /* Flag all mines. */

                for (i = 0; i < BOARD_SIZE; i++) {
                    if (IS_MINE(board[i])) {
                        SET_FLAGGED(board[i]);
                    }
                }
                flags = 0;
            }
        }
    }
}

void flag() {
    if (!IS_REVEALED(board[c_pos])) {
        if (IS_FLAGGED(board[c_pos])) {
            SET_UNFLAGGED(board[c_pos]);
            flags++;
        } else {
            SET_FLAGGED(board[c_pos]);
            flags--;
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

        if (state < WIN) {
            if (key == 'w' && c_pos >= BOARD_DIM) {
                c_pos -= BOARD_DIM;
            } else if (key == 'a' && c_pos % BOARD_DIM > 0) {
                c_pos--;
            } else if (key == 's' && c_pos < BOARD_SIZE - BOARD_DIM) {
                c_pos += BOARD_DIM;
            } else if (key == 'd' && c_pos % BOARD_DIM < BOARD_DIM - 1) {
                c_pos++;
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