#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define DBUF_INIT {NULL, 0}
#define CTRL_KEY(k) ((k) & 0x1f)

enum {
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_LEFT,
    ARROW_RIGHT,
};

/*--- data ---*/
struct termios orig_term;
struct termStat {
    unsigned short rows, cols;
    unsigned short curRow, curCol;
} ts;

/*--- terminal ---*/
void exitRawMode() {
    tcsetattr(0, TCSANOW, &orig_term);
}

void enterRawMode() {
    atexit(exitRawMode);
    tcgetattr(0, &orig_term);
    struct termios new_term = orig_term;
    new_term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    new_term.c_oflag &= ~(OPOST);
    new_term.c_cflag |= (CS8);
    new_term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    new_term.c_cc[VMIN] = 0;
    new_term.c_cc[VTIME] = 1;
    tcsetattr(0, TCSAFLUSH, &new_term);
}

void die(char *message) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(message);
    exit(-1);
}

/*--- Dynamic Buffer ---*/
struct dbuf {
    char *data;
    unsigned int length;
};

void dbufAppend(struct dbuf *dbuf, const char *data, unsigned int length) {
    dbuf->data = realloc(dbuf->data, dbuf->length + length);
    if (!dbuf->data) die("dbufAppend");
    memcpy(&dbuf->data[dbuf->length], data, length);
    dbuf->length += length;
}

void dbufFree(struct dbuf *dbuf) {
    free(dbuf->data);
}

/*--- input process ---*/
int inputKey() {
    char ch;
    ssize_t status;
    while (!(status = read(STDIN_FILENO, &ch, 1))) {
        if (status == -1 && errno != EAGAIN) die("read");
    }

    if (ch == '\033') {
        char nav[2];
        read(STDIN_FILENO, nav, 2);
        switch (nav[1]) {
            case 'A': return ARROW_UP;
            case 'B': return ARROW_DOWN;
            case 'C': return ARROW_RIGHT;
            case 'D': return ARROW_LEFT;
        }
    }

    return ch;
}

void processKeyPress(struct dbuf *row) {
    int ch = inputKey();
    switch(ch) {
        case '\x0D':
            if (ts.curRow < ts.rows - 1) {
                ts.curRow++;
                ts.curCol = 0;
            }
            break;
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
        case ARROW_UP:
            if (ts.curRow > 0) {
                ts.curRow--;
                ts.curCol = 0;
            }
            break;
        case ARROW_LEFT:
            if (ts.curCol > 0) ts.curCol--;
            break;
        case ARROW_RIGHT:
            if (ts.curCol < ts.cols - 1) ts.curCol++;
            break;
        case ARROW_DOWN:
            if (ts.curRow < ts.rows - 1) {
                ts.curRow++;
                ts.curCol = 0;
            }
            break;
        default:
            if (ts.curCol < ts.cols - 1) {
                char c = (char) ch;
                dbufAppend(row + ts.curRow, &c, 1);
                ts.curCol++;
            }
            break;
    }
}

/*--- screen ---*/
void getCursorPosition() {
    char buf[30] = {0};
    int ret, i, pow;
    char ch;
    ts.curCol = 0;
    ts.curRow = 0;

    write(STDOUT_FILENO, "\033[6n", 4);

    for( i = 0, ch = 0; ch != 'R'; i++ )
    {
        ret = read(STDIN_FILENO, &ch, 1);
        if ( !ret ) die("getCursorPosition");
        buf[i] = ch;
    }

    if (i < 2) die("getCursorPosition");

    for (i -= 2, pow = 1; buf[i] != ';'; i--, pow *= 10)
        ts.curCol = ts.curCol + ( buf[i] - '0' ) * pow;

    for (i-- , pow = 1; buf[i] != '['; i--, pow *= 10)
        ts.curRow = ts.curRow + ( buf[i] - '0' ) * pow;
}

void drawRows(struct dbuf *dbuf, struct dbuf *row) {
    for (int i = 0; i < ts.rows; i++) {
        dbufAppend(dbuf, "~", 1);

        dbufAppend(dbuf, "\x1b[K", 3);
        dbufAppend(dbuf, (row+i)->data, (row+i)->length);
        if (i < ts.rows - 1)
            dbufAppend(dbuf, "\r\n", 2);
    }
}

void refreshScreen(struct dbuf *row) {
    struct dbuf dbuf = DBUF_INIT;

    dbufAppend(&dbuf, "\x1b[?25l", 6);
    dbufAppend(&dbuf, "\x1b[H", 3);

    drawRows(&dbuf, row);

    char buf[30];
    snprintf(buf, sizeof(buf), "\033[%d;%dH", ts.curRow + 1, ts.curCol + 2);
    dbufAppend(&dbuf, buf, strlen(buf));

    dbufAppend(&dbuf, "\x1b[?25h", 6);

    write(STDOUT_FILENO, dbuf.data, dbuf.length);
    dbufFree(&dbuf);
}

/*--- init ---*/
void initConfig() {
    struct winsize ws;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
    ts.cols = ws.ws_col;
    ts.rows = ws.ws_row;
    ts.curCol = 0;
    ts.curRow = 0;
}

/*--- main ---*/
int main() {
    initConfig();
    enterRawMode();

    struct dbuf *row = calloc(sizeof(struct dbuf), ts.rows);

    while (1) {
        refreshScreen(row);
        processKeyPress(row);
    }
}