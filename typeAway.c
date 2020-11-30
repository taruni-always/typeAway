#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

/*** Include statements***/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>//unistd is the POSIC Operating System API
#include <termios.h> //for terminal I/O interface
#include <ctype.h> //for iscntrl() method
#include <errno.h> //for handling errors
#include <sys/ioctl.h> //to get terminal dimensions
#include <string.h>

/*** defining our own macros***/
#define CTRL_KEY(key) ((key) & 0x1f) // ANDing with 31 i.e 1f in hexadecimal ex: 'a' - 97, 'a' & 0x1f - 1 
#define ABUF_INIT {NULL, 0}


enum keys { 
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
  int size;
  char *chars;
} editorRow;

/*** global variables ***/
struct configurations {
    int xCoord, yCoord;
    int rowOffset;
    int terminalRows, terminalCols;
    int numrows;
    editorRow *row;
    struct termios originalTerminal;
};
struct configurations editor;

/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};
void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}
void abFree(struct abuf *ab) {
    free(ab->b);
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
            int len = editor.row[fileRow].size;
            if (len > editor.terminalCols) len = editor.terminalCols;
                abAppend(ab, editor.row[fileRow].chars, len);
        }
        abAppend(ab, "\x1b[K", 3);
        if (currRow < editor.terminalRows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}
void refreshScreen() {
    struct abuf ab = ABUF_INIT;
    
    abAppend(&ab, "\x1b[?25l", 6);
    //abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);
    
    
    indicateRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", editor.yCoord + 1, editor.xCoord + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

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

void editorAppendRow(char *s, size_t len) {
    editor.row = realloc(editor.row, sizeof(editorRow) * (editor.numrows + 1));
    int at = editor.numrows;
    editor.row[at].size = len;
    editor.row[at].chars = malloc(len + 1);
    memcpy(editor.row[at].chars, s, len);
    editor.row[at].chars[len] = '\0';
    editor.numrows ++;
}

/*** file i/o ***/
void editorOpen(char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) handleError("fopen");
    char *line = NULL;
    size_t lineCapacity = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &lineCapacity, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}

/*** input ***/
void moveCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (editor.xCoord != 0) 
            editor.xCoord --;
            break;
        case ARROW_RIGHT:
            if (editor.xCoord != editor.terminalCols - 1) 
            editor.xCoord ++;
            break;
        case ARROW_UP:
            if (editor.yCoord != 0) 
            editor.yCoord --;
            break;
        case ARROW_DOWN:
            if (editor.yCoord != editor.terminalRows) 
            editor.yCoord ++;
            break;
    }
}
void processKey() {
    int c = readKey();
    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case HOME_KEY:
            editor.xCoord = 0;
            break;
        case END_KEY:
            editor.xCoord = editor.terminalCols  - 1;
            break;
        
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = editor.terminalRows;
                while (times --)
                moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            moveCursor(c);
            break;
    }
}

/*** MAIN ***/
void initEditor() {
    editor.xCoord = 0;
    editor.yCoord = 0;
    editor.rowOffset = 0; //scrolling to top row by default
    editor.numrows = 0;
    editor.row = NULL;
    if (getWindowSize(&editor.terminalRows, &editor.terminalCols) == -1) handleError(" getWindowSize");
} // initializing all the fields of configurations
int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if ( argc >= 2) editorOpen(argv[1]);
    //editorOpen();
    //enabling raw mode to process every character as they're entered
    //like entering a password
    while (1) {
        processKey();
        refreshScreen();
    }
    //tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTerminal);
    //turning raw mode off once we're done
    return 0;
}