#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#define TAB_STOP 4

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
//#include <conio.h>


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
enum highlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_TEXT (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/***Data***/

struct editorSyntax {
    char *fileType;
    char **fileMatch;
    char **keywords;
    char *singleLineCommentStart;
    int flags;
};
typedef struct editorRow {
  int size, rsize;
  char *chars;
  char *render;
  char *hl; //highlighting
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
    char *fileName;
    char statusmsg[80];
    time_t statusmsg_time;
    struct editorSyntax *syntax;
    struct termios originalTerminal;
};
struct configurations editor;

/***file types***/

char *C_HL_extensions[] = { ".c", ".h", ".c++", NULL };
char *C_HL_keywords[] = { "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", NULL };
char *TEXT_HL_extension[] = {".txt", ".docx", NULL};
char *TEXT_HL_keywords[] = {"", NULL};
struct editorSyntax HLDB[] = { // highlight database
    {
        "c/c++", 
        C_HL_extensions,
        C_HL_keywords,
        "//",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
    {
        "text",
        TEXT_HL_extension,
        TEXT_HL_keywords, 
        "note:",
        HL_HIGHLIGHT_NUMBERS
    } 
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))


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
int rxToxCoord(editorRow *row, int rx) {
  int currentRX = 0, x;
    for ( x = 0; x < row->size; x ++) {
        if (row -> chars[x] == '\t')
            currentRX += (TAB_STOP - 1) - (currentRX % TAB_STOP);
        currentRX ++;
        if (currentRX > rx) return x;
    }
    return x;
}
void editorSetStatusMessage(const char *fmt, ...);
char *prompt(char *message, void (*callback)(char *, int));
int colourCodes(int hl);

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
                int welcomelen = snprintf(welcome, sizeof(welcome), "\x1b[33m T\x1b[35my\x1b[33mP\x1b[35me Away!!\x1b[m");
                if (welcomelen > editor.terminalCols) welcomelen = editor.terminalCols;
                int padding = (editor.terminalCols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);//cyan
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }    
            else {
                abAppend(ab, "~", 1);//light blue
            }
        } 
        else {
            int len = editor.row[fileRow].rsize - editor.colOffset;
            if (len < 0) len = 0;
            if (len > editor.terminalCols) len = editor.terminalCols;
            char *c = &editor.row[fileRow].render[editor.colOffset];
            char *hl = &editor.row[fileRow].hl[editor.colOffset];
            int currentColour = -1;
            for (int j = 0; j < len; j++) {
                if (hl[j] == HL_NORMAL) {
                    if (currentColour != -1) {
                        abAppend(ab, "\x1b[39m", 5);
                        currentColour = -1;
                    }
                    abAppend(ab, &c[j], 1);
                } 
                else {
                    int colour = colourCodes(hl[j]);
                    if (colour != currentColour) {
                        currentColour = colour;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", colour);  
                        abAppend(ab, buf, clen);
                    }
                    abAppend(ab, &c[j], 1);
                }   
            }
            abAppend(ab, "\x1b[39m", 5);  
        }
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}
void drawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "\x1b[35m %.20s - %d lines %s\x1b[m", editor.fileName ? editor.fileName : "[Unknown File]", editor.numrows, editor.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", editor.syntax ? editor.syntax -> fileType : "no file type", editor.yCoord + 1, editor.numrows);    if (len > editor.terminalCols) len = editor.terminalCols;
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
void setStatusMessage( const char *fmt, ...) {//variable number of arguements
    va_list ap;
    //strcat(fmt, "\x1b[32m");
    va_start(ap, fmt);
    vsnprintf(editor.statusmsg, sizeof(editor.statusmsg), fmt, ap);
    va_end(ap);
    editor.statusmsg_time = time(NULL);
}
void drawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    //abAppend(ab, "\x1b[m", 3);
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

/*** syntax highlighting ***/
int isSeparator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}
int colourCodes(int hl) {
    switch (hl) {
        case HL_COMMENT: return 36; //cyan
        case HL_KEYWORD1: return 33; //Brown
        case HL_KEYWORD2: return 32; // Green
        case HL_NUMBER: return 31; //red
        case HL_MATCH: return 32; //green // when we found the search results
        case HL_STRING: return 34; //Blue
        default: return 37;
    }
}
void updateSyntax(editorRow *row) {
    row -> hl = realloc(row -> hl, row -> rsize);
    memset(row -> hl, HL_NORMAL, row -> rsize);

    if (editor.syntax == NULL) return;

    char **keywords = editor.syntax -> keywords;

    char *scs = editor.syntax -> singleLineCommentStart;
    int scsLen = scs ? strlen(scs) : 0;

    int prevSeperator = 1;
    int inString = 0;

    int i = 0;
    while (i < row->rsize) {
        char c = row -> render[i];
        char prevhl = (1 > 0) ? row -> hl[i - 1] : HL_NORMAL;
        
        if (scsLen && !inString) {
            if (!strncmp(&row->render[i], scs, scsLen)) {
                memset(&row->hl[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }

        if (editor.syntax -> flags & HL_HIGHLIGHT_STRINGS) {
            if (inString) {
                row -> hl[i] = HL_STRING;
            if (c == '\\' && i + 1 < row -> rsize) {
                row -> hl[i + 1] = HL_STRING;
                i += 2;
                continue;
            }
            if (c == inString) inString = 0;
                i ++;
                prevSeperator = 1;
                continue;
            } 
            else {
                if (c == '"' || c == '\'') {
                    inString = c;
                    row -> hl[i] = HL_STRING;
                    i ++;
                    continue;
                }
            }
        }

        if (editor.syntax -> flags & HL_HIGHLIGHT_NUMBERS) {
            if ( (isdigit(c) && (prevSeperator || prevhl == HL_NUMBER)) || (c == '.' && prevhl == HL_NUMBER)) { //decimal numbers also
                row -> hl[i] = HL_NUMBER;
                i ++;
                prevSeperator = 0;
                continue;
            }
        }

        if (prevSeperator) {
            int j;
            for ( j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int keword2 = keywords[j][klen - 1] == '|';
                
                if (keword2) klen --;
                if (!strncmp(&row->render[i], keywords[j], klen) && isSeparator(row->render[i + klen])) {
                    memset(&row->hl[i], keword2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prevSeperator = 0;
                continue;
            }
        }
        prevSeperator = isSeparator(c);
        i ++;
    }
}
void selectSyntaxHighlight() {
    editor.syntax = NULL;
    if (editor.fileName == NULL) return;
    
    char *ext = strrchr(editor.fileName, '.');
    
    for (int entry = 0; entry < HLDB_ENTRIES; entry ++) {
        struct editorSyntax *s = &HLDB[entry];
        int i = 0;
        while (s -> fileMatch[i]) {
            int isExtension = (s -> fileMatch[i][0] == '.');
            if ((isExtension && ext && !strcmp(ext, s -> fileMatch[i])) || (!isExtension && strstr(editor.fileName, s -> fileMatch[i]))) {
                editor.syntax = s;
                
                for ( int fileRow = 0; fileRow < editor.numrows; fileRow ++) {
                    updateSyntax(&editor.row[fileRow]);
                }

                return;
            }
            i++;
        }
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
    updateSyntax(row);
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
    editor.row[insertAt].hl = NULL;
    updateRow(&editor.row[insertAt]);
    editor.numrows ++;
    editor.dirty ++;
}
void freeRow(editorRow *row) {
    free(row -> render);
    free(row -> chars);
    free(row -> hl);
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
void editorOpen(char *fileName) {
    free(editor.fileName);
    editor.fileName = strdup(fileName);
    
    selectSyntaxHighlight();

    FILE *fp = fopen(fileName, "r");
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
    if (editor.fileName == NULL) {
        editor.fileName = prompt("\x1b[34mSave as: %s (ESC to cancel)", NULL);
        if (editor.fileName == NULL) {
            setStatusMessage("\x1b[36m Save aborted\x1b[m");
            return;
        }
        selectSyntaxHighlight();
    }

    int len;
    char *buf = rowsToString(&len);
    
    int fd = open(editor.fileName, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                editor.dirty = 0;
                setStatusMessage("\x1b[32m %d bytes written to disk\x1b[m", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    setStatusMessage("\x1b[31m Can't save! I/O error: %s\x1b[m", strerror(errno));
}

/**Find**/

void editorFindCallback(char *sequence, int key) { //for incremental search
    static int last_match = -1;
    static int direction = 1;

    static int saved_hl_line;
    static char *saved_hl = NULL;
    if (saved_hl) {
        memcpy(editor.row[saved_hl_line].hl, saved_hl, editor.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }
    if ( key == '\r' || key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    }
    else if ( key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    }
    else {
        last_match = -1;
        direction = 1;
    }


    if (last_match == -1) direction = 1;
    int current = last_match;

    for ( int i = 0; i < editor.numrows; i++) {
        current += direction;
        if ( current == -1) current = editor.numrows - 1;
        else if (current == editor.numrows) current = 0;

        editorRow *row = &editor.row[current];
        char *match = strstr(row -> render, sequence);
        if (match) {
            last_match = current;
            editor.yCoord = current;
            editor.xCoord = rxToxCoord(row, match - row -> render);
            editor.rowOffset = editor.numrows;

            saved_hl_line = current;
            saved_hl = malloc(row -> rsize);
            memcpy(saved_hl, row -> hl, row -> rsize);
            memset(&row -> hl[match - row -> render], HL_MATCH, strlen(sequence));
            break;
        }
    }
}
void editorFind() {
    int saved_cx = editor.xCoord;
    int saved_cy = editor.yCoord;
    int saved_colOff = editor.colOffset;
    int saved_rowOff = editor.rowOffset;

    char *sequence = prompt("\x1b[32mSearch: %s (Arrows to navigate | Enter to search | ESC to cancel)\x1b[m", editorFindCallback);
    if (sequence) free(sequence);
    else {
        editor.xCoord = saved_cx;
        editor.yCoord = saved_cy;
        editor.rowOffset = saved_rowOff;
        editor.colOffset = saved_colOff;
    }
}

/*** input ***/
char *prompt(char *message, void (*callback)(char *, int)) {
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
            if (callback) callback(buffer, c);
            free(buffer);
            return NULL;
        } 
        else if (c == '\r') {
            if ( bufferLen != 0) {
                setStatusMessage("");
                if (callback) callback(buffer, c);
                return buffer;
            }
        }
        else if (!iscntrl(c) && c < 128) {
            if ( bufferLen == bufferSize - 1) {
                bufferSize *= 2;
                buffer = realloc(buffer, bufferSize);
            }
            buffer[bufferLen ++] = c;
            buffer[bufferLen] = '\0';
        }
        if (callback) callback(buffer, c);
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
            setStatusMessage("\x1b[31m WARNING!! This file contains unsaved changes. Press Ctrl+Q again to exit\x1b[m");
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
        case CTRL_KEY('f'):
            editorFind();
            break;
        case BACK_SPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if ( c == DEL_KEY) moveCursor(ARROW_RIGHT);
            editorDelChar();
            break;
        
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
    editor.fileName = NULL;
    editor.statusmsg[0] = '\0';
    editor.statusmsg_time = 0;
    editor.syntax = NULL;

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
    setStatusMessage("\x1b[34m [Ctrl+Q = quit | Ctrl+S = save | Ctrl+F = find]\x1b[m");
    while (1) {
        refreshScreen();
        processKey();
    }
    //tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTerminal);
    //turning raw mode off once we're done
    return 0;
}