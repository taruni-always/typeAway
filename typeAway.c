#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#define TAB_STOP 8

/*** Include statements***/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>//unistd is the POSIC Operating System API
#include <termios.h> //for terminal I/O interface
#include <ctype.h> //for iscntrl() method
#include <errno.h> //for handling errors
#include <sys/ioctl.h> //to get terminal dimensions
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>


/*** defining our own macros***/
#define CTRL_KEY(key) ((key) & 0x1f) // ANDing with 31 i.e 1f in hexadecimal ex: 'a' - 97, 'a' & 0x1f - 1 
#define ABUF_INIT {NULL, 0}


enum keys { 
    BACK_SPACE = 127,
    ARROW_LEFT = 1000, 
    ARROW_RIGHT, ARROW_UP, 
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY, 
    PAGE_UP,
    PAGE_DOWN
};

typedef struct editorRow {
  int size, rsize;
  char *chars;
  char *render;
} editorRow;

/*** global variables ***/
struct configurations {
    int xCoord, yCoord;
    int rx;
    int rowOffset, colOffset;
    int terminalRows, terminalCols;
    int numrows;
    editorRow *row;
    int dirty;// to know if the changes are saved or not
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios originalTerminal;
};
struct configurations editor;

/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};
void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab -> b, ab -> len + len);
    if (new == NULL) return;
    memcpy(&new[ab -> len], s, len);
    ab -> b = new;
    ab -> len += len;
}
void abFree(struct abuf *ab) {
    free(ab -> b);
}

/***prototypes***/
int xCoordTorx(editorRow *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j ++) {
        if (row -> chars[j] == '\t')
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx ++;
  }
  return rx;
}
void editorSetStatusMessage(const char *fmt, ...);
char *prompt(char *message);

/***output screen***/

void editorScroll() {
    editor.rx = 0;
    if (editor.yCoord < editor.numrows) {
        editor.rx = xCoordTorx(&editor.row[editor.yCoord], editor.xCoord);
    }

    if (editor.yCoord < editor.rowOffset) {
        editor.rowOffset = editor.yCoord;
    }
    if (editor.yCoord >= editor.rowOffset + editor.terminalRows) {
        editor.rowOffset = editor.yCoord - editor.terminalRows + 1;
    }
    if (editor.rx < editor.colOffset) {
        editor.colOffset = editor.rx;
    }
    if (editor.rx >= editor.colOffset + editor.terminalCols) {
        editor.colOffset = editor.rx - editor.terminalCols + 1;
    }
}

void indicateRows(struct abuf *ab) {
    for (int currRow = 0; currRow < editor.terminalRows; currRow ++) {
        int fileRow = currRow + editor.rowOffset;
        if (fileRow >= editor.numrows) {
            if (editor.numrows == 0 && currRow == editor.terminalRows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "TypeAway!!");
                if (welcomelen > editor.terminalCols) welcomelen = editor.terminalCols;
                int padding = (editor.terminalCols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }    
            else {
                abAppend(ab, "~", 1);
            }
        } 
        else {
            int len = editor.row[fileRow].rsize - editor.colOffset;
            if (len < 0) len = 0;
            if (len > editor.terminalCols) len = editor.terminalCols;
            abAppend(ab, &editor.row[fileRow].render[editor.colOffset], len);
        }
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}
void drawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);
    //abAppend(ab, "\x1b[31;3m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", editor.filename ? editor.filename : "[Unknown File]", editor.numrows, editor.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", editor.yCoord + 1, editor.numrows);
    if (len > editor.terminalCols) len = editor.terminalCols;
    abAppend(ab, status, len);
    while (len < editor.terminalCols) {
        if (editor.terminalCols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } 
        else {
            abAppend(ab, " ", 1);
            len ++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}
void setStatusMessage(const char *fmt, ...) {//variable number of arguements
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(editor.statusmsg, sizeof(editor.statusmsg), fmt, ap);
    va_end(ap);
    editor.statusmsg_time = time(NULL);
}
void drawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msgLen = strlen(editor.statusmsg);
    if ( msgLen > editor.terminalCols) msgLen = editor.terminalCols;
    if ( msgLen && time(NULL) - editor.statusmsg_time < 5) abAppend(ab, editor.statusmsg, msgLen);
}
void refreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;
    
    abAppend(&ab, "\x1b[?25l", 6); // hide cursor
    //abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);
    
    indicateRows(&ab);
    drawStatusBar(&ab);
    drawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", editor.yCoord - editor.rowOffset + 1, editor.rx - editor.colOffset + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // show cursor

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}


/*** Terminal ***/
void handleError(const char *s) {
    refreshScreen();
    perror(s);
    exit(1);
}
void turnOffFlags(struct termios raw) {
    raw.c_iflag &= ~(IXON | ICRNL | INPCK | BRKINT); //'~' complementing and then '&' ANDing the bits
    raw.c_oflag &= ~(OPOST); //'~' complementing and then '&' ANDing the bits
    raw.c_cflag |= ~(CS8); //ORing this time and not ANDing
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); //'~' complementing and then '&' ANDing the bits
    //turning off a few needed flags
}
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &editor.originalTerminal);
}
void enableRawMode() {
    struct termios raw = editor.originalTerminal;
    atexit(disableRawMode);
    if ( tcgetattr(STDIN_FILENO, &(editor.originalTerminal)) == -1) handleError("tcgetattr");
    turnOffFlags(raw);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if ( tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1 ) handleError("tcsetattr");
} // function to enable raw mode
int readKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) handleError("read");
    }
    if (c == '\x1b') { //arrow keys have the escape sequence '\x1b' at the beginning
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } 
            else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP; 
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    }
    else {
        return c;
    }
} //separate function because we 're processing it only after we read a valid key w/o errors
int getCursorPosition(int *rSize, int *cSize) {
    char buffer[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    
    while (i < sizeof(buffer) - 1) {
        if (read(STDIN_FILENO, &buffer[i], 1) != 1) break;
        if (buffer[i] == 'R') break;
        i ++;
    }
    buffer[i] = '\0';
    if (buffer[0] != '\x1b' || buffer[1] != '[') return -1;
    if (sscanf( &buffer[2], "%d;%d", rSize, cSize) != 2) return -1;
    
    return 0;
}
int getWindowSize(int *rSize, int *cSize) {
    struct winsize ws;
    if ( ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) { 
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rSize, cSize); 
    }
    else {
        *cSize = ws.ws_col; 
        *rSize = ws.ws_row; 
        return 0;
    }
}

/***manipulating row actions***/
void updateRow(editorRow *row) {
    free(row->render);
    row->render = malloc(row->size + 1);
    int index = 0, tabs = 0;
    
    for (int j = 0; j < row -> size; j ++) {
        if (row -> chars[j] == '\t') tabs ++;
    }

    free(row -> render);
    row -> render = malloc(row -> size + tabs * (TAB_STOP - 1) + 1);

    for (int j = 0; j < row -> size; j ++) {
        if (row->chars[j] == '\t') {
            row->render[index ++] = ' ';
            while (index % TAB_STOP != 0) row->render[index ++] = ' ';
        } 
        else row->render[index ++] = row-> chars[j];
    }
    row -> render[index] = '\0';
    row -> rsize = index;
}
void insertRow(int insertAt, char *s, size_t len) {
    if ( insertAt < 0 || insertAt > editor.numrows) return; 
    
    editor.row = realloc(editor.row, sizeof(editorRow) * (editor.numrows + 1));
    memmove(&editor.row[insertAt + 1], &editor.row[insertAt], sizeof(editorRow) * (editor.numrows - insertAt));
        
    editor.row[insertAt].size = len;
    editor.row[insertAt].chars = malloc(len + 1);
    memcpy(editor.row[insertAt].chars, s, len);
    editor.row[insertAt].chars[len] = '\0';

    editor.row[insertAt].rsize = 0;
    editor.row[insertAt].render = NULL;
    updateRow(&editor.row[insertAt]);
    editor.numrows ++;
    editor.dirty ++;
}
void freeRow(editorRow *row) {
    free(row -> render);
    free(row -> chars);
}
void delRow(int at) {
    if (at < 0 || at >= editor.numrows) return;
    freeRow(&editor.row[at]);
    memmove(&editor.row[at], &editor.row[at + 1], sizeof(editorRow) * (editor.numrows - at - 1));
    editor.numrows --;
    editor.dirty ++;
}
void rowInsertChar(editorRow *row, int insertAt, int c) {
    if (insertAt < 0 || insertAt > row -> size) insertAt = row -> size;
    row -> chars = realloc(row -> chars, row -> size + 2);
    memmove(&row -> chars[insertAt + 1], &row -> chars[insertAt], row -> size - insertAt + 1);
    row -> size++;
    row -> chars[insertAt] = c;
    updateRow(row);
    editor.dirty ++;
}
void rowAppendString(editorRow *row, char *s, size_t len) {
    row -> chars = realloc(row -> chars, row -> size + len + 1);
    memcpy(&row -> chars[row -> size], s, len);
    row -> size += len;
    row -> chars[row -> size] = '\0';
    updateRow(row);
    editor.dirty ++;
}
void rowDelChar(editorRow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(& row -> chars[at], &row -> chars[at + 1], row -> size - at);
    row -> size --;
    updateRow(row);
    editor.dirty ++;
}

/*** editor operations ***/
void editorInsertChar(int c) {
    if (editor.yCoord == editor.numrows)
        insertRow(editor.numrows, "", 0);
    rowInsertChar(&editor.row[editor.yCoord], editor.xCoord, c);
    editor.xCoord ++;
}
void editorInsertNewline() {
    if (editor.xCoord == 0) {
        insertRow(editor.yCoord, "", 0);
    } 
    else {
        editorRow * row = &editor.row[editor.yCoord];
        insertRow(editor.yCoord + 1, &row -> chars[editor.xCoord], row -> size - editor.xCoord);
        row = &editor.row[editor.yCoord];
        row -> size = editor.xCoord;
        row -> chars[row -> size] = '\0';
        updateRow(row);
    }
    editor.yCoord ++;
    editor.xCoord = 0;
}
void editorDelChar() {
    if (editor.yCoord == editor.numrows) return;
    if (editor.xCoord == 0 && editor.yCoord == 0) return;

    editorRow * row = &editor.row[editor.yCoord];
    if (editor.xCoord > 0) {
        rowDelChar(row, editor.xCoord - 1);
        editor.xCoord --;
    }
    else {
        editor.xCoord = editor.row[editor.yCoord - 1].size;
        rowAppendString(&editor.row[editor.yCoord - 1], row -> chars, row -> size);
        delRow(editor.yCoord);
        editor.yCoord --;
    }
}

/*** file i/o ***/
void editorOpen(char *filename) {
    free(editor.filename);
    editor.filename = strdup(filename);
    
    FILE *fp = fopen(filename, "r");
    if (!fp) handleError("fopen");
    
    char *line = NULL;
    size_t lineCapacity = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &lineCapacity, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;
        insertRow(editor.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    editor.dirty = 0;
}
char *rowsToString(int *bufferLen) {
    int totalLen = 0;
    for (int j = 0; j < editor.numrows; j ++)
        totalLen += editor.row[j].size + 1;
    *bufferLen = totalLen;

    char *buf = malloc(totalLen);
    char *p = buf;
    for (int j = 0; j < editor.numrows; j++) {
        memcpy(p, editor.row[j].chars, editor.row[j].size);
        p += editor.row[j].size;
        *p = '\n';
        p ++;
    }
    return buf;
}
void editorSave() {
    if (editor.filename == NULL) {
        editor.filename = prompt("Save as: %s (ESC to cancel)");
        if (editor.filename == NULL) {
            setStatusMessage("Save aborted");
            return;
        }
    }

    int len;
    char *buf = rowsToString(&len);
    
    int fd = open(editor.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                editor.dirty = 0;
                setStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    setStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** input ***/
char *prompt(char *message) {
    size_t bufferSize = 128;
    char *buffer = malloc(bufferSize);

    size_t bufferLen = 0;
    buffer[0] = '\0';

    while (1) {
        setStatusMessage(message, buffer);
        refreshScreen();

        int c = readKey();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACK_SPACE) {
            if (bufferLen != 0) buffer[-- bufferLen] = '\0';
        }
        else if (c == '\x1b') {
            setStatusMessage("");
            free(buffer);
            return NULL;
        } 
        else if (c == '\r') {
            if ( bufferLen != 0) {
                setStatusMessage("");
                return buffer;
            }
            else if (!iscntrl(c) && c < 128) {
                if ( bufferLen == bufferSize - 1) {
                    bufferSize *= 2;
                    buffer = realloc(buffer, bufferSize);
                }
                buffer[bufferLen ++] = c;
                buffer[bufferLen] = '\0';
            }
        }
    }
}

void moveCursor(int key) {
    editorRow *row = (editor.yCoord >= editor.numrows) ? NULL : &editor.row[editor.yCoord];

    switch (key) {
        case ARROW_LEFT:
            if (editor.xCoord != 0) 
            editor.xCoord --;
            else if (editor.yCoord > 0) {
                editor.yCoord --;
                editor.xCoord = editor.row[editor.yCoord].size;
            }
            break;
        case ARROW_RIGHT:
            if ( row && editor.xCoord < row -> size)
            editor.xCoord ++;
            else if (row && editor.xCoord == row -> size) {
                editor.yCoord ++;
                editor.xCoord = 0;
            }
            break;
        case ARROW_UP:
            if (editor.yCoord != 0) 
            editor.yCoord --;
            break;
        case ARROW_DOWN:
            if (editor.yCoord < editor.numrows) 
            editor.yCoord ++;
            break;
    }
    row = (editor.yCoord >= editor.numrows) ? NULL : &editor.row[editor.yCoord];
    int rowlen = row ? row -> size : 0;
    if (editor.xCoord > rowlen) {
        editor.xCoord = rowlen;
    }
}
void processKey() {
    static int quit_times = 1;
    int c = readKey();

    switch (c) {
        case '\r': 
            editorInsertNewline();
            break;
        case CTRL_KEY('q'):
        if (editor.dirty && quit_times) {
            setStatusMessage("WARNING!! This file contains unsaved changes. Press Ctrl+Q again to exit");
            quit_times --;
            return;
        }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case HOME_KEY:
            editor.xCoord = 0;
            break;
        case END_KEY:
            if (editor.yCoord < editor.numrows)
                editor.xCoord = editor.row[editor.yCoord].size;
            break;
        case BACK_SPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if ( c == DEL_KEY) moveCursor(ARROW_RIGHT);
            editorDelChar();
            break;
        /*if (editor.yCoord < editor.numrows)
            editor.xCoord = editor.row[editor.yCoord].size ;
            break;*/
        
        case PAGE_UP:
        case PAGE_DOWN:
            if (c == PAGE_UP)
                editor.yCoord = editor.rowOffset;
            else if (c == PAGE_DOWN)
                editor.yCoord = editor.rowOffset + editor.terminalRows - 1;
            if (editor.yCoord > editor.numrows) editor.yCoord = editor.numrows;
            int times = editor.terminalRows;
            while (times --)
            moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);        
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            moveCursor(c);
            break;
        case CTRL_KEY('l'):
        case '\x1b':
            break;
        default :
            editorInsertChar(c);
            break;
    }
    quit_times = 1;
}

/*** MAIN ***/
void initEditor() {
    editor.xCoord = editor.yCoord = 0;
    editor.rx = 0;
    editor.rowOffset = editor.colOffset = 0;
    editor.numrows = 0;
    editor.dirty = 0;
    editor.row = NULL;
    editor.filename = NULL;
    editor.statusmsg[0] = '\0';
    editor.statusmsg_time = 0;

    if (getWindowSize(&editor.terminalRows, &editor.terminalCols) == -1) handleError(" getWindowSize");
    editor.terminalRows -= 2; // one for status bar and one for message
} // initializing all the fields of configurations
int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if ( argc >= 2) editorOpen(argv[1]);
    //editorOpen();
    //enabling raw mode to process every character as they're entered
    //like entering a password
    setStatusMessage("[Ctrl+Q=quit|Ctrl+S=save]");
    while (1) {
        refreshScreen();
        processKey();
    }
    //tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTerminal);
    //turning raw mode off once we're done
    return 0;
}