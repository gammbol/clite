#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define DBUF_INIT {NULL, 0}

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
    perror(message);
    exit(-1);
}

/*--- Dynamic Buffer ---*/
struct dbuf {
    char *data;
    unsigned int length;
};

void dbufAppend(struct dbuf *dbuf, const char *data, unsigned int length) {
    if (!dbuf->data) {
        dbuf->data = malloc(length);
    } else {
        dbuf->data = realloc(dbuf->data, dbuf->length + length);
    }
    if (!dbuf->data) die("dbufAppend");
    for (int i = 0; i < length; i++)
        dbuf->data[i + dbuf->length] = data[i];
    dbuf->length += length;
}

void dbufFree(struct dbuf *dbuf) {
    free(dbuf->data);
    dbuf->data = NULL;
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

void processKeyPress(struct dbuf *dbuf) {
    char ch = inputKey();
    switch(ch) {
        case '\x0D':
            dbufAppend(dbuf, "\r\n", 2);
            break;
        case 'q':
            exit(0);
        default:
            dbufAppend(dbuf, &ch, 1);
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

void refreshScreen(struct dbuf *dbuf) {
    getCursorPosition();
    for (int i = 0; i < ts.rows; i++) {
        dbufAppend(dbuf, "\033[K", 3);
        dbufAppend(dbuf, "~", 1);
        if (i + 1 != ts.rows) {
            dbufAppend(dbuf, "\r\n", 2);
        }
        dbufAppend(dbuf, "\033[1B", 4);
    }
    char pos[30];
    sprintf(pos, "\033[%d;%dH", ts.curRow, ts.curCol);
    dbufAppend(dbuf, pos, strlen(pos));
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
    write(STDOUT_FILENO, "\033[2J", 4);
    while (1) {
        struct dbuf dbuf = DBUF_INIT;
        refreshScreen(&dbuf);
        processKeyPress(&dbuf);
        write(STDOUT_FILENO, dbuf.data, dbuf.length);
        dbufFree(&dbuf);
    }
}