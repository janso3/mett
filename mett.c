#include <ctype.h>
#include <curses.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

typedef enum {
	MODE_NORMAL,
	MODE_WRITE,
	MODE_COMMAND
} Mode;

typedef struct {
	int x, y;
	int starty;
} Cursor;

typedef struct line_ {
	struct line_ *next, *prev;
	unsigned int length;
	char data[];
} Line;

struct formatter_;
typedef struct buffer_ {
	struct buffer_ *next, *prev;
	char *path;
	Line *lines, *curline;
	Cursor cursor;
	int linexoff;
	struct formatter_ *formatln;
} Buffer;

typedef struct {
	char *cmd;
	int key;
	void (*fn)();
	union Arg {
		struct { int x, y; };
		int i;
		void *v;
	} arg;
} Action;

typedef struct formatter_ {
	char *suffix;
	void (*linefn)(Buffer*, char*, int, int);
} Formatter;

/* Internal functions */
static void msighandler(int);
static int mnumplaces(int);

static Buffer* mnewbuf();
static void mfreebuf(Buffer*);
static int mreadbuf();
static int mnumlines(Buffer*);

static void mupdatecursor();
static void minsert(int);
static void mrepeat(Action*, int);
static void mruncmd(char*);

static void mformat_c(Buffer*, char*, int, int);
static void mpaintstat();
static void mpaintbuf(Buffer*);
static void mpaintcmd();

/* Bindable functions */
static void repaint();
static void quit();
static void setmode(Action*);
static void save(Action*);
static void readfile(Action*);

static void command(Action*);
static void motion(Action*);
static void bufsel(Action*);
static void insert(Action*);

/* Global variables */
static Mode mode = MODE_NORMAL;
static WINDOW *bufwin, *statuswin, *cmdwin;
static Buffer *buflist, *curbuf;
static Line *cmdbuf;
static int repcnt = 0;

/* We make all the declarations available to the user */
#include "config.h"

int main(int argc, char **argv) {
	int i, key;
	int row, col;

	cmdbuf = (Line*)calloc(1, sizeof(Line)+default_linebuf_size);
	cmdbuf->length = default_linebuf_size;

	signal(SIGINT, msighandler);
	for (i = 1; i < argc; ++i) {
		mreadbuf((curbuf = mnewbuf()), argv[i]);
	}

	initscr();
	clear();
	noecho();
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);
	notimeout(stdscr, TRUE);
	use_default_colors();

	if ((use_colors = has_colors())) {
		start_color();
		for (i = 1; i < NUM_COLOR_PAIRS; ++i)
			init_pair(i, color_pairs[i][0], color_pairs[i][1]);
	}

	getmaxyx(stdscr, row, col);
	statuswin = newwin(1, col, 0, 0);
	bufwin = newwin(row-2, col, 1, 0);
	cmdwin = newwin(1, col, row-1, 0);
	repaint();

	for (;;) {
		if ((key = wgetch(stdscr)) != ERR) {
			switch (mode) {
			case MODE_NORMAL:
				/* Special keys will cancel action sequences */
				if (key == ESC || key == LF) {
					repcnt = 0;
				}

				/* Number keys are reserved for repetition */
				if (isdigit(key)) {
					repcnt = MIN(10 * repcnt + (key - '0'), max_cmd_repetition);
				} else {
					for (i = 0; i < (int)(sizeof(buffer_actions) / sizeof(Action)); ++i) {
						if (buflist && key == buffer_actions[i].key)
							mrepeat((Action*)&buffer_actions[i], repcnt ? repcnt : 1);
					}
					repcnt = 0;
				}
				break;
			case MODE_WRITE:
				if (key == ESC) mode = MODE_NORMAL;
				else minsert(key);
				break;
			case MODE_COMMAND:
				if (key == ESC) mode = MODE_NORMAL;
				else minsert(key);
				break;
			}
			repaint();
		}
	}

	return 0;
}

void msighandler(int signum) {
	switch (signum) {
	case SIGHUP:
	case SIGKILL:
	case SIGINT:
		quit();
		break;
	}
}

int mnumplaces(int n) {
	/* How many visual places in a number? */
	int r = n < 0 ? 2 : 1;
	while (n > 9) {
	    n /= 10;
	    r++;
	}
	return r;
}

Buffer* mnewbuf() {
	/* Create new buffer and insert at start of the list */
	Buffer *next = NULL, *head, *cur;
	if (buflist) next = buflist;
	if (!(buflist = (Buffer*)calloc(1, sizeof(Buffer)))) return NULL;
	buflist->next = next;
	if (next) buflist->next->prev = buflist;
	return buflist;
}

void mfreebuf(Buffer *buf) {
	free(buf->path);
}

int mreadbuf(Buffer *buf, const char *path) {
	FILE *fp;
	const size_t len = default_linebuf_size;
	char linecnt[len];
	int nlines;
	Line *ln = NULL;

	ln = buf->curline = buf->lines = (Line*)calloc(sizeof(Line)+default_linebuf_size, 1);
	ln->length = default_linebuf_size;
	if (!(fp = fopen(path, "r"))) return 0;
	while (fgets(linecnt, len, fp) == linecnt) {
		Line *curln = ln;
		strncpy(ln->data, linecnt, len-1);
		ln->data[len-1] = 0;

		ln->next = (Line*)calloc(sizeof(Line)+default_linebuf_size, 1);
		ln->length = default_linebuf_size;
		ln = ln->next;
		ln->prev = curln;
	}

	nlines = mnumlines(buf);
	buf->linexoff = line_numbers ? mnumplaces(nlines)+1 : 0;
	buf->path = (char*)calloc(1, strlen(path)+1);
	//buf->formatln = mformat_c;
	strcpy(buf->path, path);
	fclose(fp);

	return 1;
}

int mnumlines(Buffer *buf) {
	Line *ln;
	int n = 0;
	if (!buf) return 0;
	ln = buf->lines;
	while (ln) {
		ln = ln->next;
		n++;
	}
	return n;
}

void mupdatecursor() {
	int row, col;
	if (!curbuf) return;
	getmaxyx(stdscr, row, col);

	if (mode == MODE_COMMAND) {
		int len = strlen(cmdbuf->data);
		move(row-1, len+2);
	} else {
		Line *ln;
		int i, ntabs;

		/* Count number of tabs until cursor */
		ln = curbuf->curline;
		for (i = ntabs = 0; i < curbuf->cursor.x; ++i) {
			if (ln->data[i] == '\t') ntabs++;
		}

		move(curbuf->cursor.y - curbuf->cursor.starty + 1,
				 curbuf->cursor.x + curbuf->linexoff + ntabs * (tab_width-1));
	}
}

void minsert(int key) {
	if (mode == MODE_COMMAND) {
		/* Insert into command-line */
		int len = strlen(cmdbuf->data);
		switch (key) {
		case KEY_BACKSPACE:
			cmdbuf->data[len-1] = 0;
			break;
		/* TODO: cursor movement */
		case KEY_LEFT:
			break;
		case KEY_RIGHT:
			break;
		case '\n':
			mode = MODE_NORMAL;
			mruncmd(cmdbuf->data);
			memset(cmdbuf->data, 0, cmdbuf->length);
			break;
		default:
			cmdbuf->data[len] = (char)key;
			break;
		}
	} else {
		/* Insert into current buffer */
		int idx, len;
		Line *ln;
		if (!curbuf || !(ln = curbuf->curline)) return;
		idx = curbuf->cursor.x;
		len = strlen(ln->data)+1;
		switch (key) {
		case KEY_BACKSPACE:
			if (idx) {
				memcpy(ln->data+idx-1, ln->data+idx, len);
				curbuf->cursor.x--;
			}
			break;
		case DEL:
			memcpy(ln->data+idx, ln->data+idx+1, len);
			break;
		case '\n':
			{
				Line *old = ln;
			 	ln = curbuf->curline = (Line*)calloc(1, sizeof(Line)+default_linebuf_size);
				ln->data[0] = '\n';
				ln->next = old->next;
				ln->prev = old;
				old->next = ln;
				curbuf->cursor.y++;
			}
			break;
		default:
			memcpy(ln->data+idx+1, ln->data+idx, len);
			ln->data[idx] = key;
			curbuf->cursor.x++;
			break;
		}
	}
}

void mrepeat(Action *ac, int n) {
	int i;
	n = MIN(n, max_cmd_repetition);
	for (i = 0; i < n; ++i) {
		ac->fn(ac);
	}
}

void mruncmd(char *buf) {
	/* TODO
	 * Every command can be written either as their keyboard shortcut or
	 * as the full string. You can chain commands one after the other.
	 * Every command can be prefixed with a decimal repetition counter.
	 * Whitespace is ignored.
	 *
	 * For example, "10down 5lw  q" will:
	 * 1) Move down 10 lines
	 * 2) Move right 5 places
	 * 3) Write the buffer
	 * 4) Quit the editor
	 */
	int i;
	for (i = 0; i < (int)(sizeof(buffer_actions) / sizeof(Action)); ++i) {
		char *cmd = NULL;
		long cnt, cmdlen;
		if (!(cnt = strtol(buf, &cmd, 10))) cnt = 1;
		cmdlen = strlen(cmd);
		if (buffer_actions[i].cmd) {
			/* Check for valid command */
			if (/* Either the single-char keyboard shortcut... */
					(cmdlen == 1 && buffer_actions[i].key == cmd[0])
					/* ...or the full command */
					|| !strncmp(buffer_actions[i].cmd, cmd, cmdbuf->length)) {
				long j;
				mrepeat((Action*)&buffer_actions[i], cnt);
			}
		}
	}
}

void mformat_c(Buffer *buf, char *line, int xoff, int n) {
	const char *keywords[] = {
		"auto", "break", "case", "char", "const", "continue", "default",
		"do", "double", "else", "enum", "extern", "float", "for",
		"goto", "if", "inline", "int", "long", "register", "restrict",
		"return", "short", "signed", "sizeof", "static", "struct", "switch",
		"typedef", "union", "unsigned", "void", "volatile", "while",
		"_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex",
		"_Generic", "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
		"asm", "fortran"
	};

	const char *tokens[] = {
		"if", "elif", "else", "endif", "defined", "ifdef", "ifndef", "define",
		"undef", "include", "line", "error", "pragma", "_Pragma"
	};

	int i, j, xpos = xoff, len = strlen(line);

	/* TODO
	 * Find valid keywords and tokens before
	 * printing since we need clear start/end
	 * points to use colors.
	 */

	for (i = 0; i < len; ++i) {
		const char c = line[i];
		switch (c) {
		case '\t':
			for (j = 0; j < tab_width; ++j) {
				mvwaddch(bufwin, n, xpos, ' ');
				xpos++;
			}
			break;
		default:
			mvwaddch(bufwin, n, xpos, c);
			xpos++;
			break;
		}
	}
}

void mpaintstat() {
	int row, col, nlines, bufsize;
	Buffer *cur = buflist;
	char textbuf[32];
	char *bufname = "Untitled";
	const char *modes[] = { "NORMAL", "WRITE", "COMMAND" };

	getmaxyx(stdscr, row, col);
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));

	/* Background */
	whline(statuswin, ' ', col);

	/* Buffer name, buffer length */
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));

	nlines = mnumlines(curbuf);
	if (curbuf && curbuf->path) bufname = curbuf->path;
	wprintw(statuswin, "%s, %i lines", bufname, nlines);

	/* Mode, cursor pos */
	bufsize = snprintf(textbuf, sizeof(textbuf), "%s %d:%d", modes[mode], cur ? cur->cursor.y : 0, cur ? cur->cursor.x : 0);
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));
	mvwprintw(statuswin, 0, col - bufsize, "%s", textbuf);
	if (use_colors) wattroff(statuswin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));

	wrefresh(statuswin);

	if (use_colors) wattroff(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));
}

void mpaintbuf(Buffer *buf) {
	int i, l;
	int row, col;
	Line *ln;

	if (!buf) return;
	getmaxyx(bufwin, row, col);
	ln = buf->lines;

	for (i = l = 0; l < row && ln->next; ++i, ln = ln->next) {
		/* TODO: Use curline to speed up painting large files */
		if (i < curbuf->cursor.starty) continue;
		if (use_colors) wattron(bufwin, COLOR_PAIR(PAIR_LINE_NUMBERS));
		if (line_numbers) wprintw(bufwin, "%d\n", i);
		if (use_colors) wattroff(bufwin, COLOR_PAIR(PAIR_LINE_NUMBERS));
		if (buf->formatln) {
			//buf->formatln(buf, ln->data, buf->linexoff, l);
		} else {
			/* Default line formatting */
			int i, j, xpos = buf->linexoff, len = strlen(ln->data);
			for (i = 0; i < len; ++i) {
				const char c = ln->data[i];
				switch (c) {
				case '\t':
					for (j = 0; j < tab_width; ++j) {
						mvwaddch(bufwin, l, xpos, ' ');
						xpos++;
					}
					break;
				default:
					mvwaddch(bufwin, l, xpos, c);
					xpos++;
					break;
				}
			}
		}
		l++;
	}

	wrefresh(bufwin);
}

void mpaintcmd() {
	int bufsize;
	int row, col;
	char textbuf[32];

	getmaxyx(cmdwin, row, col);

	if (use_colors) wattron(cmdwin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));

	/* Command */
	wprintw(cmdwin, "$");
	wprintw(cmdwin, " %s", cmdbuf->data);

	/* Repetition count */
	bufsize = snprintf(textbuf, sizeof(textbuf), "%d", repcnt);
	mvwprintw(cmdwin, 0, col - bufsize, "%s", textbuf);

	if (use_colors) wattroff(cmdwin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));

	wrefresh(cmdwin);
}

void repaint() {
	int row, col;
	getmaxyx(stdscr, row, col);
	delwin(statuswin);
	delwin(cmdwin);
	delwin(bufwin);
	refresh();
	statuswin = newwin(1, col, 0, 0);
	bufwin = newwin(row-2, col, 1, 0);
	cmdwin = newwin(1, col, row-1, 0);
	mpaintbuf(curbuf);
	mpaintstat();
	mpaintcmd();
	mupdatecursor();
}

void quit() {
	free(cmdbuf);
	delwin(cmdwin);
	delwin(bufwin);
	delwin(statuswin);
	endwin();
	exit(0);
}

void setmode(Action *ac) {
	mode = (Mode)ac->arg.i;
}

void command(Action *ac) {
	if (ac->arg.v) mruncmd((char*)ac->arg.v);
}

void motion(Action *ac) {
	int row, col;
	getmaxyx(bufwin, row, col);

	/* left / right */
	if (ac->arg.x == -1) {
		curbuf->cursor.x = MAX(curbuf->cursor.x-1, 0);
	} else if (ac->arg.x == +1) {
		curbuf->cursor.x = MIN(curbuf->cursor.x+1, col-curbuf->linexoff-1);
	}

	/* up / down */
	if (ac->arg.y == -1 && curbuf->cursor.y) {
		if (curbuf->cursor.y <= curbuf->cursor.starty) {
			/* Scroll the view up */
			curbuf->cursor.starty--;
		}
		curbuf->cursor.y--;
		if (curbuf->curline->prev) curbuf->curline = curbuf->curline->prev;
	} else if (ac->arg.y == +1) {
		if (curbuf->curline->next) {
			curbuf->curline = curbuf->curline->next;
			curbuf->cursor.y++;
		}
		if (curbuf->cursor.y - curbuf->cursor.starty >= row) {
			/* Scroll the view down */
			curbuf->cursor.starty++;
		}
	}
}

void bufsel(Action *ac) {
	/* TODO: Forward/backward multiple buffers */
	if (ac->arg.i < 0) {
		if (curbuf->prev) curbuf = curbuf->prev;
	} else if (ac->arg.i > 0) {
		if (curbuf->next) curbuf = curbuf->next;
	}
}

void insert(Action *ac) {
	minsert(ac->arg.i);
}

void save(Action *ac) {
	FILE *src, *bak;
	Line *ln;

	if (!ac->arg.v && !curbuf->path) {
		/* TODO: Let the user enter the path */
		return;
	}

	if (backup_on_write
			&& (bak = fopen(backup_path, "w+"))
			&& (src = fopen(curbuf->path, "r"))) {
		char c;
		while((c = getc(src)) != EOF)
			fputc(c, bak);

		fclose(bak);
		fclose(src);
	}

	if (!(src = fopen(ac->arg.v ? ac->arg.v : curbuf->path, "w+"))) return;
	for (ln = curbuf->lines; ln && ln->next; ln = ln->next) {
		fputs(ln->data, src);
	}

	fclose(src);
}

void readfile(Action *ac) {
	if (ac->arg.v) {
		mreadbuf((curbuf = mnewbuf()), ac->arg.v);
	}
}
