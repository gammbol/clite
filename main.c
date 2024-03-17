#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define DBUF_INIT {NULL, 0}
#define CTRL_KEY(k) ((k) & 0x1f)

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
    cfmakeraw(&new_term);
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
char inputKey() {
    char ch;
    ssize_t status;
    while (!(status = read(STDIN_FILENO, &ch, 1))) {
        if (status == -1 && errno != EAGAIN) die("read");
    }

    return ch;
}

void processKeyPress(struct dbuf *row) {
    char ch = inputKey();
    switch(ch) {
        case '\x0D':
            if (ts.curRow < ts.rows - 1)
                ts.curRow++;
            break;
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
        default:
            dbufAppend(row + ts.curRow, &ch, 1);
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

    for( i -= 2, pow = 1; buf[i] != ';'; i--, pow *= 10)
        ts.curCol = ts.curCol + ( buf[i] - '0' ) * pow;

    for( i-- , pow = 1; buf[i] != '['; i--, pow *= 10)
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
    snprintf(buf, sizeof(buf), "\033[%d;%d", ts.curRow, ts.curCol);
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