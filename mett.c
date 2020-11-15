#define NCURSES_WIDECHAR 1
#include <ctype.h>
#include <curses.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ABS(a)   ((a)<0?-(a):a)

typedef enum {
	MODE_NORMAL,
	MODE_INSERT,
	MODE_SELECT,
	MODE_COMMAND
} Mode;

typedef struct {
	int x, y;
} Coord;

typedef struct {
	Coord c; /* Cursor coordinate */
	Coord v; /* Visual selection end */
	int starty; /* Y-Offset of the view */
} Cursor;

typedef struct line_ {
	struct line_ *next, *prev;
	size_t length;
	wchar_t data[];
} Line;

struct formatter_;
typedef struct buffer_ {
	struct buffer_ *next, *prev;
	char *path;
	Line *lines, *curline;
	struct formatter_ *formatln;
	Cursor cursor;
	int linexoff;
} Buffer;

typedef struct {
	wchar_t *cmd;
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
static int mreadbuf(Buffer*, const char*);
static int mnumlines(Buffer*);

static void mupdatecursor();
static void minsert(Buffer*, int);
static void mmove(Buffer*, int, int);
static void mrepeat(const Action*, int);
static void mruncmd(wchar_t*);

static void mformat_c(Buffer*, char*, int, int);
static void mpaintstat();
static void mpaintbuf(Buffer*, WINDOW*, bool);
static void mpaintcmd();

/* Bindable functions */
static void repaint();
static void quit();
static void setmode(const Action*);
static void save(const Action*);
static void readfile(const Action*);

static void command(const Action*);
static void motion(const Action*);
static void pgup();
static void pgdown();
static void bufsel(const Action*);
static void insert(const Action*);
static void deleteline();
static void newline();

/* Global variables */
static Mode mode = MODE_NORMAL;
static WINDOW *bufwin, *statuswin, *cmdwin;
static Buffer *buflist, *curbuf, *cmdbuf;
static int repcnt = 0;
static char unibuf[5];

/* We make all the declarations available to the user */
#include "config.h"

int main(int argc, char **argv) {
	int i, key;

	setlocale(LC_ALL, "");

	cmdbuf = mnewbuf();
	signal(SIGINT, msighandler);
	for (i = 1; i < argc; ++i) {
		mreadbuf((curbuf = mnewbuf()), argv[i]);
	}

	if (!curbuf) {
		curbuf = mnewbuf();
	}

	initscr();
	clear();
	noecho();
	keypad(stdscr, TRUE);
	nodelay(stdscr, FALSE);
	notimeout(stdscr, TRUE);
	use_default_colors();

	if ((use_colors = has_colors())) {
		start_color();
		for (i = 1; i < NUM_COLOR_PAIRS; ++i)
			init_pair(i, color_pairs[i][0], color_pairs[i][1]);
	}

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
						if (key == buffer_actions[i].key)
							mrepeat(&buffer_actions[i], repcnt ? repcnt : 1);
					}
					repcnt = 0;
				}
				break;
			case MODE_SELECT:
				if (key == ESC) mode = MODE_NORMAL;
				break;
			case MODE_INSERT:
				if (key == ESC) mode = MODE_NORMAL;
				else minsert(curbuf, key);
				break;
			case MODE_COMMAND:
				if (key == ESC) mode = MODE_NORMAL;
				else minsert(cmdbuf, key);
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
	/* How many decimal places in a number? */
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
	/* Every buffer has at least one line */
	buflist->curline = buflist->lines = (Line*)calloc(sizeof(Line)+default_linebuf_size*sizeof(wchar_t), 1);
	buflist->curline->length = default_linebuf_size;
	if (next) buflist->next->prev = buflist;
	return buflist;
}

void mfreebuf(Buffer *buf) {
	free(buf->path);
}

int mreadbuf(Buffer *buf, const char *path) {
	FILE *fp;
	const size_t len = default_linebuf_size;
	wchar_t linecnt[len];
	Line *ln;

	ln = buf->lines;
	if (!(fp = fopen(path, "r"))) return 0;
	while (fgetws(linecnt, len, fp) == linecnt) {
		Line *curln = ln;
		wcsncpy(ln->data, linecnt, len);
		ln->data[len-1] = 0;
		ln->next = (Line*)calloc(sizeof(Line)+len*sizeof(wchar_t), 1);
		ln->length = default_linebuf_size;
		ln = ln->next;
		ln->prev = curln;
	}

	buf->linexoff = 4; // TODO
	buf->path = (char*)calloc(1, strlen(path)+1);
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
	/* Place the cursor in the right place depending on mode */
	if (mode == MODE_COMMAND) {
		int row, col;
		getmaxyx(stdscr, row, col);
		size_t len = wcslen(cmdbuf->curline->data);
		move(row-1, len);
	} else {
		Line *ln;
		int i, ntabs;

		/* Count number of tabs until cursor */
		if (!curbuf) return;
		ln = curbuf->curline;
		for (i = ntabs = 0; i < curbuf->cursor.c.x; ++i) {
			if (ln->data[i] == L'\t') ntabs++;
		}

		move(curbuf->cursor.c.y - curbuf->cursor.starty + 1,
				 curbuf->cursor.c.x + curbuf->linexoff + ntabs * (tab_width-1));
	}
}

void minsert(Buffer *buf, int key) {
	int i, idx, len;
	wchar_t wc;
	Line *ln;

	if (!buf || !(ln = buf->curline)) return;

	/* TODO: handle unicode */
	wc = key;

	idx = buf->cursor.c.x;
	len = wcslen(ln->data)+1;

	switch (wc) {
	case KEY_BACKSPACE:
		if (idx) {
			memcpy(ln->data+idx-1, ln->data+idx, len * sizeof(wchar_t));
			buf->cursor.c.x--;
		}
		break;
	case KEY_DC:
		memcpy(ln->data+idx, ln->data+idx+1, len * sizeof(wchar_t));
		break;
	case '\n':
		{
			if (mode == MODE_COMMAND) {
				mruncmd(cmdbuf->curline->data);
			} else {
				Line *old = ln;
		 		ln = buf->curline = (Line*)calloc(1, sizeof(Line)+default_linebuf_size);
				ln->data[0] = L'\n';
				ln->next = old->next;
				ln->prev = old;
				old->next = ln;
				mmove(curbuf, 0, 1);
			}
		}
		break;
	default:
		{
			memcpy(ln->data+idx+1, ln->data+idx, len * sizeof(wchar_t));
			ln->data[idx] = wc;
			buf->cursor.c.x++;
		}
		break;
	}
}

void mmove(Buffer *buf, int x, int y) {
	int i, len;
	int row, col;
	getmaxyx(bufwin, row, col);

	/* left / right */
	if (x == -1) {
		buf->cursor.c.x = MAX(buf->cursor.c.x-1, 0);
	} else if (x == +1) {
		buf->cursor.c.x = MIN(buf->cursor.c.x+1, col-buf->linexoff-1);
	} else {
		buf->cursor.c.x = x;
	}

	/* up / down */
	if (y < 0) {
		for (i = 0; i < ABS(y); ++i) {
			if (buf->curline->prev) {
				buf->curline = buf->curline->prev;
				buf->cursor.c.y--;
			} else break;
			if (buf->cursor.c.y < buf->cursor.starty) {
				/* Scroll the view up */
				buf->cursor.starty--;
			}
		}
	} else {
		for (i = 0; i < y; ++i) {
			if (buf->curline->next) {
				buf->curline = buf->curline->next;
				buf->cursor.c.y++;
			} else break;
			if (buf->cursor.c.y - buf->cursor.starty >= row) {
				/* Scroll the view down */
				buf->cursor.starty++;
			}
		}
	}

	/* Restrict cursor to line content */
	len = wcslen(buf->curline->data)-1;
	buf->cursor.c.x = MAX(MIN(buf->cursor.c.x, len), 0);
}

void mrepeat(const Action *ac, int n) {
	int i;
	n = MIN(n, max_cmd_repetition);
	for (i = 0; i < n; ++i) {
		ac->fn(ac);
	}
}

void mruncmd(wchar_t *buf) {
	/* TODO
	 * Every command can be written either as its keyboard shortcut or
	 * as the full string. You can chain commands one after the other.
	 * Every command can be prefixed with a decimal repetition count.
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
		wchar_t *cmd = NULL;
		long cnt, cmdlen;
		if (!(cnt = wcstol(buf, &cmd, 10))) cnt = 1;
		cmdlen = wcslen(cmd);
		if (buffer_actions[i].cmd) {
			/* Check for valid command */
			if (/* Either the single-char keyboard shortcut... */
					(cmdlen == 1 && buffer_actions[i].key == cmd[0])
					/* ...or the full command */
					|| !wcsncmp(buffer_actions[i].cmd, cmd, cmdbuf->curline->length)) {
				long j;
				mrepeat(&buffer_actions[i], cnt);
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
	Buffer *cur = buflist;
	int row, col, nlines, bufsize;
	char textbuf[32];
	char *bufname = "~scratch~";
	const char *modes[] = { "NORMAL", "INSERT", "SELECT", "COMMAND" };

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
	bufsize = snprintf(textbuf, sizeof(textbuf), "%s %d:%d", modes[mode], cur ? cur->cursor.c.y : 0, cur ? cur->cursor.c.x : 0);
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));
	mvwprintw(statuswin, 0, col - bufsize, "%s", textbuf);
	if (use_colors) wattroff(statuswin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));

	wrefresh(statuswin);

	if (use_colors) wattroff(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));
}

void mpaintln(Buffer *buf, Line *ln, WINDOW *win, int y, int n, bool numbers) {
	int i, j;
	if (use_colors) wattron(win, COLOR_PAIR(PAIR_LINE_NUMBERS));
	if (numbers && line_numbers) mvwprintw(win, y, 0, "%d", n);
	if (use_colors) wattroff(win, COLOR_PAIR(PAIR_LINE_NUMBERS));
	if (buf->formatln) {
		//buf->formatln(buf, ln->data, buf->linexoff, l);
	} else {
		/* Default line formatting */
		int x = buf->linexoff, len = wcslen(ln->data);
		for (i = 0; i < len; ++i) {
			const wchar_t c = ln->data[i];
			switch (c) {
			case L'\t':
			{
				for (j = 0; j < tab_width; ++j) {
					cchar_t cc;
					setcchar(&cc, &c, 0, 0, 0);
					mvwadd_wch(win, y, x, &cc);
					x++;
				}
			} break;
			default:
			{
				cchar_t cc;
				setcchar(&cc, &c, 0, 0, 0);
				mvwadd_wch(win, y, x, &cc);
				x++;
			} break;
			}
		}
	}
}

void mpaintbuf(Buffer *buf, WINDOW *win, bool numbers) {
	int i, l, cp;
	int row, col;
	Line *ln;

	if (!buf || !bufwin) return;
	getmaxyx(win, row, col);
	cp = buf->cursor.c.y - buf->cursor.starty;

	/* Paint from the cursor to the bottom */
	for (i = cp, l = 0, ln = buf->curline; i < row && ln; ++i, ++l, ln = ln->next)
		mpaintln(buf, ln, win, i, l, numbers);

	/* Paint from the cursor to the top */
	for (i = cp, l = 0, ln = buf->curline; i>=0 && ln; --i, ++l, ln = ln->prev)
		mpaintln(buf, ln, win, i, l, numbers);

	wrefresh(win);
}

void mpaintcmd() {
	int bufsize;
	int row, col;
	char textbuf[32];

	getmaxyx(cmdwin, row, col);

	if (use_colors) wattron(cmdwin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));

	/* Command */
	mpaintbuf(cmdbuf, cmdwin, false);

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
	mpaintbuf(curbuf, bufwin, true);
	mpaintstat();
	mpaintcmd();
	mupdatecursor();
}

void quit() {
	delwin(cmdwin);
	delwin(bufwin);
	delwin(statuswin);
	endwin();
	exit(0);
}

void setmode(const Action *ac) {
	if ((mode = (Mode)ac->arg.i) == MODE_SELECT && curbuf) {
		/* Capture start of visual selection */
		curbuf->cursor.v.x = curbuf->cursor.c.x;
		curbuf->cursor.v.y = curbuf->cursor.c.y;
	}
}

void save(const Action *ac) {
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
		fputws(ln->data, src);
	}

	fclose(src);
}

void readfile(const Action *ac) {
	if (ac->arg.v) mreadbuf((curbuf = mnewbuf()), ac->arg.v);
}

void command(const Action *ac) {
	if (ac->arg.v) mruncmd((wchar_t*)ac->arg.v);
}

void motion(const Action *ac) {
	mmove(curbuf, ac->arg.x, ac->arg.y);
}

void pgup() {
	int row, col;
	getmaxyx(bufwin, row, col);
	mmove(curbuf, 0, -row);
}

void pgdown() {
	int row, col;
	getmaxyx(bufwin, row, col);
	mmove(curbuf, 0, +row);
}

void bufsel(const Action *ac) {
	/* TODO: Forward/backward multiple buffers */
	if (ac->arg.i < 0) {
		if (curbuf->prev) curbuf = curbuf->prev;
	} else if (ac->arg.i > 0) {
		if (curbuf->next) curbuf = curbuf->next;
	}
}

void insert(const Action *ac) {
	minsert(curbuf, ac->arg.i);
}

void deleteline() {
	Line *ln = curbuf->curline;
	if (!ln) return;
	if (ln->prev)
		ln->prev->next = ln->next;
	if (ln->next)
		ln->next->prev = ln->prev;
	curbuf->curline = ln->prev;
	free(ln);
}

void newline() {
	minsert(curbuf, L'\n');
	mode = MODE_INSERT;
}
