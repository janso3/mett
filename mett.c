#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED
#include <ctype.h>
#include <curses.h>
#include <locale.h>
#include <math.h>
#include <regex.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define SWAP(X, Y, T) { T SWAP = X; X = Y; Y = SWAP; }
#define BINDABLE(fn) static void fn()

typedef enum {
	MODE_NORMAL,
	MODE_INSERT,
	MODE_SELECT,
	MODE_COMMAND
} Mode;

typedef enum {
	MARKER_START,
	MARKER_MIDDLE,
	MARKER_END
} Marker;

typedef struct {
	int x, y;
} Coord;

typedef struct {
	Coord c; /* Cursor coordinate */
	Coord v0; /* Visual selection start */
	Coord v1; /* Visual selection end */
} Cursor;

typedef struct line {
	struct line *next, *prev;
	size_t length;
	wchar_t data[];
} Line;

typedef struct buffer {
	struct buffer *next, *prev;
	char *path;
	Line *lines, *curline;
	Cursor cursor;
	int starty;
	int offsetx;
} Buffer;

typedef struct {
	wchar_t *cmd;
	int key;
	void (*fn)();
	union Arg {
		struct { int x, y; };
		int i;
		void *v;
		Marker m;
	} arg;
} Action;

static int32_t min(int32_t, int32_t);
static int32_t max(int32_t, int32_t);

static void msighandler(int);

static Buffer* mnewbuf();
static void mfreebuf(Buffer*);
static void mclearbuf(Buffer*);
static int  mreadfile(Buffer*, const char*);
static void mreadstr(Buffer*, const char*);

static int  mnumlines(Buffer*);
static int  mnumvislines(Line*);
static void mupdatecursor();
static void mcmdkey(wint_t);
static void minsert(Buffer*, wint_t);
static int  mindent(Line*, int);
static void mfreeln(Line*);
static void msetln(Line*, const wchar_t*);
static void mmove(Buffer*, int, int);
static void mjump(Buffer*, Marker);
static void mselect(Buffer*, int, int, int, int);
static void mrepeat(const Action*, int);
static void mruncmd(wchar_t*);

static void mpaintstat();
static void mpaintln(Buffer*, Line*, WINDOW*, int, int, bool);
static void mpaintbuf(Buffer*, WINDOW*, bool);
static void mpaintcmd();

BINDABLE (repaint);
BINDABLE (handlemouse);
BINDABLE (quit);
BINDABLE (setmode);
BINDABLE (save);
BINDABLE (readfile);
BINDABLE (readstr);
BINDABLE (find);
BINDABLE (motion);
BINDABLE (jump);
BINDABLE (coc);
BINDABLE (pgup);
BINDABLE (pgdown);
BINDABLE (bufsel);
BINDABLE (bufdel);
BINDABLE (insert);
BINDABLE (freeln);
BINDABLE (append);
BINDABLE (newln);

/* Global variables */
static Mode mode = MODE_NORMAL;
static WINDOW *bufwin, *statuswin, *cmdwin;
static Buffer *buflist, *curbuf, *cmdbuf;
static int repcnt = 0;

/* We make all the declarations available to the user */
#include "config.h"

int main(int argc, char **argv) {
	int i;
	wint_t key;

	setlocale(LC_ALL, "");

	/* Init buffers */
	cmdbuf = mnewbuf();
	cmdbuf->offsetx = 0;
	signal(SIGHUP,  msighandler);
	signal(SIGKILL, msighandler);
	signal(SIGINT,  msighandler);
	signal(SIGTERM, msighandler);

	for (i = 1; i < argc; ++i)
		mreadfile((curbuf = mnewbuf()), argv[i]);

	if (!curbuf)
		curbuf = mnewbuf();

	/* Init curses */
	newterm(NULL, stderr, stderr);
	clear();
	noecho();
	keypad(stdscr, TRUE);
	notimeout(stdscr, FALSE);
	set_escdelay(1);
	use_default_colors();
	mousemask(BUTTON1_CLICKED | REPORT_MOUSE_POSITION, NULL);

	if (use_colors && (use_colors = has_colors())) {
		start_color();
		for (i = 1; i < NUM_COLOR_PAIRS; ++i)
			init_pair(i, color_pairs[i][0], color_pairs[i][1]);
	}

	repaint();
	for (;;) {
		get_wch(&key);
		if (key != (wint_t)ERR) {
			switch (mode) {
			case MODE_NORMAL:
				/* Special keys will cancel action sequences */
				if (key == ESC || key == '\n') {
					repcnt = 0;
				}
				mcmdkey(key);
				break;
			case MODE_SELECT:
				if (key == ESC) mode = MODE_NORMAL;
				else mcmdkey(key);
				break;
			case MODE_INSERT:
				if (key == ESC) mode = MODE_NORMAL;
				else minsert(curbuf, key);
				break;
			case MODE_COMMAND:
				if (key == ESC) {
					mode = MODE_NORMAL;
				}
				else minsert(cmdbuf, key);
				break;
			}
			repaint();
		}
	}

	return 0;
}

int32_t min(int32_t a, int32_t b) {
	return a < b ? a : b;
}

int32_t max(int32_t a, int32_t b) {
	return a > b ? a : b;
}

void msighandler(int signum) {
	switch (signum) {
	case SIGHUP:
	case SIGKILL:
	case SIGINT:
	case SIGTERM:
		quit();
		break;
	}
}

Buffer* mnewbuf() {
	/* Create new buffer and insert at start of the list */
	Buffer *next = NULL;
	if (buflist) next = buflist;
	if (!(buflist = (Buffer*)calloc(1, sizeof(Buffer)))) return NULL;
	buflist->next = next;
	/* Every buffer has at least one line */
	buflist->curline = buflist->lines = (Line*)calloc(sizeof(wchar_t), sizeof(Line)+default_linebuf_size);
	buflist->curline->length = default_linebuf_size;
	buflist->offsetx = 4;
	if (next) buflist->next->prev = buflist;
	mselect(buflist, -1, -1, -1, -1);
	return buflist;
}

void mfreebuf(Buffer *buf) {
	if (!buf) return;
	free(buf->path);
	mclearbuf(buf);
	free(buf->lines);
	if (buf->prev) buf->prev->next = buf->next;
	if (buf->next) buf->next->prev = buf->prev;
	if (curbuf == buf) curbuf = buf->next;
}

void mclearbuf(Buffer *buf) {
	Line *ln;
	if (!buf || !buf->lines) return;
	ln = buf->lines->next;
	while (ln) {
		Line *next = ln->next;
		free(ln);
		ln = next;
	}
	buf->lines->next = NULL;
	buf->cursor.c.x = buf->cursor.c.y = 0;
	//msetln(buf->lines, L"");
	buf->curline = buf->lines;
}

int mreadfile(Buffer *buf, const char *path) {
	FILE *fp = NULL;

	if (!buf || !path) return 0;
	if (path[0] == '-' && !path[1]) {
		fp = stdin;
	} else {
		fp = fopen(path, "r");
	}

	if (fp) {
		wchar_t linecnt[default_linebuf_size];
		Line *ln = buf->lines;
		while (fgetws(linecnt, default_linebuf_size, fp) == linecnt) {
			Line *curln = ln;
			int len = wcslen(linecnt);
			if (!curln) break;
			wcsncpy(ln->data, linecnt, default_linebuf_size);
			ln->data[len-1] = 0;
			if (!(ln->next = (Line*)calloc(sizeof(Line)+default_linebuf_size*sizeof(wchar_t), 1))) return 0;
			ln->length = default_linebuf_size;
			ln = ln->next;
			ln->prev = curln;
		}
	}

	buf->path = (char*)calloc(1, strlen(path)+1);
	strcpy(buf->path, path);
	if (fp) fclose(fp);

	return 1;
}

void mreadstr(Buffer *buf, const char *str) {
	int i, len;
	if (!buf || !str) return;
	len = strlen(str);
	for (i = 0; i < len; ++i)
		minsert(buf, str[i]);
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

int mnumvislines(Line *ln) {
	/* How many 'visual lines' will be needed
	 * in order to display the physical line? */
	int len, col;
	col = getmaxx(bufwin);
	len = wcslen(ln->data) + 4;
	return col ? (len + col - 1) / col : 1;
}

void mupdatecursor() {
	/* Place the cursor depending on the mode */
	WINDOW *win = mode == MODE_COMMAND ? cmdwin : bufwin;
	Buffer *buf = mode == MODE_COMMAND ? cmdbuf : curbuf;
	Line *ln = buf->curline;
	int i, ncols;

	/* Count number of columns until cursor */
	for (i = ncols = 0; i < buf->cursor.c.x; ++i) {
		if (ln->data[i] == L'\t') ncols += tab_width;
		else ncols += max(0, wcwidth(ln->data[i]));
	}

	wmove(win, buf->cursor.c.y - buf->starty, buf->offsetx + ncols);
	wrefresh(win);
}

void mcmdkey(wint_t key) {
	/* Number keys (other than 0) are reserved for repetition */
	if (isdigit(key) && !(key == '0' && !repcnt)) {
		repcnt = min(10 * repcnt + (key - '0'), max_cmd_repetition);
	} else {
		int i;
		for (i = 0; i < (int)(sizeof(buffer_actions) / sizeof(Action)); ++i) {
			if (key == (wint_t)buffer_actions[i].key) {
				mclearbuf(cmdbuf);
				msetln(cmdbuf->curline, buffer_actions[i].cmd);
				mjump(cmdbuf, MARKER_END);
				mrepeat(&buffer_actions[i], repcnt ? repcnt : 1);
			}
		}
		repcnt = 0;
	}
}

void minsert(Buffer *buf, wint_t key) {
	int idx, len;
	Line *ln;

	if (!buf || !(ln = buf->curline)) return;

	idx = buf->cursor.c.x;
	len = (wcslen(ln->data)+1) * sizeof(wchar_t);

	switch (key) {
	case '\b':
	case 127:
	case KEY_BACKSPACE:
		if (idx) {
			memcpy(ln->data+idx-1, ln->data+idx, len);
			buf->cursor.c.x--;
		} else if (ln->prev) {
			int plen = wcslen(ln->prev->data);
			wcscpy(ln->prev->data+plen, ln->data);
			mmove(buf, plen + buf->cursor.c.x, -1);
			buf->curline = ln->prev;
			mfreeln(ln);
		}
		break;
	case KEY_DC:
		memcpy(ln->data+idx, ln->data+idx+1, len);
		break;
	case '\n':
		{
			if (mode == MODE_COMMAND) {
				mruncmd(cmdbuf->curline->data);
			} else {
				int ox = 0;
				Line *old = ln;
		 		ln = (Line*)calloc(sizeof(wchar_t), sizeof(Line) + default_linebuf_size);
				ln->next = old->next;
				ln->prev = old;
				if (old->next) old->next->prev = ln;
				old->next = ln;

				if (auto_indent) {
					/* Indent to the last position */
					int x, mx;
					for (x = mx = 0; x < idx; ++x) {
						if (old->data[x] == L'\t') mx += tab_width;
						else if (iswspace(old->data[x])) mx++;
						else break;
					}
					ox = mindent(ln, mx);
				}

				memcpy(ln->data+ox, old->data+idx, len);
				old->data[idx] = 0;
				mjump(buf, MARKER_START);
				mmove(buf, 0, 1);
				buf->cursor.c.x = ox;
			}
		}
		break;
	default:
		{
			memcpy(ln->data+idx+1, ln->data+idx, len);
			ln->data[idx] = key;
			buf->cursor.c.x++;
		}
		break;
	}
}

int mindent(Line *ln, int n) {
	int i, tabs, spaces;

	tabs = n / tab_width;
	spaces = n % tab_width;

	for (i = 0; i < tabs; ++i)
		ln->data[i] = L'\t';
	for (; i < spaces; ++i)
		ln->data[i] = L' ';

	return tabs + spaces;
}

void mfreeln(Line *ln) {
	if (ln) {
		if (ln->prev)
			ln->prev->next = ln->next;
		if (ln->next)
			ln->next->prev = ln->prev;
		free(ln);
	}
}

void msetln(Line *ln, const wchar_t *data) {
	if (ln && data) wcscpy(ln->data, data);
}

void mmove(Buffer *buf, int x, int y) {
	int i, len;
	int row;

	row = getmaxy(bufwin);

	/* left / right */
	buf->cursor.c.x += x;

	/* up / down */
	if (y < 0) {
		for (i = 0; i < abs(y); ++i) {
			if (buf->curline->prev) {
				buf->curline = buf->curline->prev;
				buf->cursor.c.y--;
			} else break;
			if (buf->cursor.c.y < buf->starty) {
				/* Scroll the view up */
				buf->starty -= mnumvislines(buf->curline);
			}
		}
	} else {
		for (i = 0; i < y; ++i) {
			if (buf->curline->next) {
				buf->curline = buf->curline->next;
				buf->cursor.c.y++;
			} else break;
			if (buf->cursor.c.y - buf->starty >= row) {
				/* Scroll the view down */
				buf->starty += mnumvislines(buf->curline);
			}
		}
	}

	/* Restrict cursor to line content */
	len = wcslen(buf->curline->data);
	buf->cursor.c.x = max(min(buf->cursor.c.x, len), 0);

	/* Update selection end */
	if (mode == MODE_SELECT) {
		buf->cursor.v1 = (Coord){buf->cursor.c.x, buf->cursor.c.y};
	}
}

void mjump(Buffer *buf, Marker mark) {
	switch(mark) {
	case MARKER_START:
		buf->cursor.c.x = 0;
		break;
	case MARKER_MIDDLE:
		{
			Line *ln = buf->curline;
			size_t len = wcslen(ln->data);
			buf->cursor.c.x = (len/2);
		}
		break;
	case MARKER_END:
		{
			Line *ln = buf->curline;
			size_t len = wcslen(ln->data);
			buf->cursor.c.x = max(len, 0);
		}
		break;
	}
}

void mselect(Buffer *buf, int x1, int y1, int x2, int y2) {
	buf->cursor.v0 = (Coord){ x1, y1 };
	buf->cursor.v1 = (Coord){ x2, y2 };
}

void mrepeat(const Action *ac, int n) {
	int i;
	n = min(n, max_cmd_repetition);
	for (i = 0; i < n; ++i)
		ac->fn(ac);
}

int mfindchr(wchar_t *buf, int start, wchar_t c) {
	int i, len;
	len = wcslen(buf);
	for (i = start; i < len; ++i) {
		if (buf[i] == c) return i;
	}
	return -1;
}

char* mexec(const char *cmd) {
	/* Execute cmd and return stdout */
  static char buf[1024 * 64];
	int pipes[2];
  pid_t pid;

	if (pipe(pipes) == -1)
		return NULL;

	if ((pid = fork()) == -1)
		return NULL;

	if (!pid) {
		dup2(pipes[1], STDOUT_FILENO);
		close(pipes[0]);
		close(pipes[1]);
		system(cmd);
		exit(0);
	} else {
		close(pipes[1]);
		read(pipes[0], buf, sizeof(buf));
		return buf;
	}

	return NULL;
}

void mruncmd(wchar_t *buf) {
	wchar_t *cmd = NULL;
	char *arg = NULL;
	int cnt, exlen, cmdlen;
	int i;

	/* Parse decimal repetition count */
	if (!(cnt = wcstol(buf, &cmd, 10))) cnt = 1;

	/* Find length of command */
	exlen = wcslen(cmd);
	if (!exlen) return;
	if ((cmdlen = mfindchr(cmd, 0, L' ')) < 0) {
		cmdlen = exlen;
	}

	/* Parse optional argument */
	if (exlen > cmdlen) {
		const wchar_t *warg = &cmd[cmdlen+1];
		arg = (char*)malloc((exlen - cmdlen) * 4);
		wcsrtombs(arg, &warg, (exlen - cmdlen) * 4, NULL);
	}

	/* Is it a shell command? */
	if (cmd[0] == L'!') {
		/* Run command */
		const wchar_t *wcmd = cmd+1;
		char *acmd = (char*)malloc(cmdlen * 4);
		wcsrtombs(acmd, &wcmd, cmdlen * 4, NULL);
		char *out = mexec(acmd);
		free(acmd);

		/* Print command output */
		mode = MODE_INSERT;
		mreadstr(cmdbuf, "\n");
		mreadstr(cmdbuf, out);
		mode = MODE_COMMAND;
	} else {
		/* Is it a builtin command? */
		for (i = 0; i < (int)(sizeof(buffer_actions) / sizeof(Action)); ++i) {
			if (buffer_actions[i].cmd) {
				/* Check for valid command */
				if (/* Either the single-char keyboard shortcut... */
				    (cmdlen == 1 && buffer_actions[i].key == cmd[0])
				    /* ...or the full command */
				    || !wcsncmp(buffer_actions[i].cmd, cmd, cmdlen)) {
					Action ac;
					memcpy(&ac, &buffer_actions[i], sizeof(Action));
					if (arg) {
						/* Check for shell command */
						if (arg[0] == '!')
							ac.arg.v = mexec(arg+1);
						else
							ac.arg.v = arg;
					}

					/* FIXME: This is an ugly hack */
					bool indent = auto_indent;
					auto_indent = FALSE;
					mode = MODE_INSERT;
					mrepeat(&ac, cnt);
					mode = MODE_COMMAND;
					auto_indent = indent;
					/* TODO: Parse next command in chain */
				}
			}
		}
	}

	free(arg);
}

void mpaintstat() {
	Buffer *cur = buflist;
	int col, nlines, bufsize;
	char textbuf[32];
	char *bufname = "~scratch~";
	const char *modes[] = { "NORMAL", "INSERT", "SELECT", "COMMAND" };

	col = getmaxx(stdscr);
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));

	/* Background */
	whline(statuswin, ' ', col);

	/* Buffer name, buffer length */
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));

	nlines = mnumlines(curbuf);
	if (curbuf && curbuf->path) bufname = curbuf->path;
	wprintw(statuswin, "%s, %i lines", bufname, nlines);

	/* Mode, cursor pos */
	cur = mode == MODE_COMMAND ? cmdbuf : buflist;
	bufsize = snprintf(textbuf, sizeof(textbuf), "%s %d:%d", modes[mode], cur ? cur->cursor.c.y : 0, cur ? cur->cursor.c.x : 0);
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));
	mvwprintw(statuswin, 0, col - bufsize, "%s", textbuf);
	if (use_colors) wattroff(statuswin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));

	if (use_colors) wattroff(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));
	wrefresh(statuswin);
}

void mpaintln(Buffer *buf, Line *ln, WINDOW *win, int y, int n, bool numbers) {
	int x, len;
	int i, j;
	int col;

	col = getmaxx(win);
	x = buf->offsetx;
	len = wcslen(ln->data);

	if (use_colors) wattron(win, COLOR_PAIR(PAIR_LINE_NUMBERS));
	if (numbers && line_numbers) mvwprintw(win, y, 0, "%d", n);
	if (use_colors) wattroff(win, COLOR_PAIR(PAIR_LINE_NUMBERS));

	for (i = 0; i < len; ++i) {
		wchar_t c = ln->data[i];
		int abs_y = y + buf->starty;
		int abs_x = x - buf->offsetx;

		/* When we hit the right edge of the screen,
		 * we wrap to the beginning of the next line */
		if (x >= col) {
			x = buf->offsetx;
			y++;
		}

		/* Highlight the current selection */
		Coord sel_start = buf->cursor.v0;
		Coord sel_end = buf->cursor.v1;
		if (sel_end.x < sel_start.x) SWAP(sel_start.x, sel_end.x, int);
		if (sel_end.y < sel_start.y) SWAP(sel_start.y, sel_end.y, int);
		if (abs_y >= sel_start.y &&
				abs_y <= sel_end.y &&
				abs_x >= sel_start.x &&
				abs_x <= sel_end.x) {
			wattron(win, COLOR_PAIR(PAIR_BUFFER_CONTENTS));
		}

		switch (c) {
			case L'\0':
			case L'\n':
			case L'\t':
			{
				cchar_t cc;
				c = tab_beginning; setcchar(&cc, &c, 0, 0, 0);
				mvwadd_wch(win, y, x++, &cc);
				c = tab_character; setcchar(&cc, &c, 0, 0, 0);
				for (j = 0; j < (int)tab_width-1; ++j) {
					mvwadd_wch(win, y, x, &cc);
					x++;
				}
			}
			break;
			default:
			{
				cchar_t cc;
				setcchar(&cc, &c, 0, 0, 0);
				mvwadd_wch(win, y, x, &cc);
				x++;
			}
			break;
		}
		wattroff(win, COLOR_PAIR(PAIR_BUFFER_CONTENTS));
	}
}

void mpaintbuf(Buffer *buf, WINDOW *win, bool numbers) {
	int i, l, cp;
	int row;
	Line *ln;

	if (!buf || !bufwin) return;
	row = getmaxy(win);
	cp = buf->cursor.c.y - buf->starty;

	/* Paint from the cursor to the bottom */
	for (i = cp, l = 0, ln = buf->curline; i < row && ln; ++l, ln = ln->next) {
		mpaintln(buf, ln, win, i, l, numbers);
		i += mnumvislines(ln);
	}

	/* Paint from the cursor to the top */
	for (i = cp, l = 0, ln = buf->curline; i >= 0 && ln; ++l, ln = ln->prev) {
		mpaintln(buf, ln, win, i, l, numbers);
		i -= ln->prev ? mnumvislines(ln->prev) : 1;
	}
}

void mpaintcmd() {
	int bufsize;
	int col;
	char textbuf[32];

	col = getmaxx(cmdwin);

	if (use_colors) wattron(cmdwin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));

	/* Command */
	mpaintbuf(cmdbuf, cmdwin, false);

	/* Repetition count */
	bufsize = snprintf(textbuf, sizeof(textbuf), "%d", repcnt);
	mvwprintw(cmdwin, 0, col - bufsize, "%s", textbuf);

	if (use_colors) wattroff(cmdwin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));
}

void repaint() {
	int row, col, nlines;
	getmaxyx(stdscr, row, col);
	nlines = mnumlines(cmdbuf);
	delwin(statuswin);
	delwin(cmdwin);
	delwin(bufwin);
	statuswin = newwin(1, col, 0, 0);
	bufwin = newwin(row-nlines-1, col, 1, 0);
	cmdwin = newwin(nlines, col, row-nlines, 0);
	refresh();
	mpaintstat();
	mpaintcmd();
	if (always_centered) coc();
	mpaintbuf(curbuf, bufwin, true);
	mupdatecursor();
}

void handlemouse() {
	MEVENT ev;
	if (getmouse(&ev) == OK) {
		if (ev.bstate & BUTTON1_CLICKED) {
			/* Jump to mouse location */
			int x = ev.x, y = ev.y;
			wmouse_trafo(bufwin, &y, &x, FALSE);
			x -= curbuf->cursor.c.x + curbuf->offsetx + 1;
			y -= curbuf->cursor.c.y + curbuf->starty;
			mmove(curbuf, x, y);
		}
	}
}

void quit() {
	Buffer *buf;
	for (buf = buflist; buf; buf = buf->next)
		mfreebuf(buf);
	delwin(cmdwin);
	delwin(bufwin);
	delwin(statuswin);
	endwin();
	exit(0);
}

void setmode(const Action *ac) {
	if ((mode = (Mode)ac->arg.i) == MODE_SELECT && curbuf) {
		/* Capture start of visual selection */
		int x = curbuf->cursor.c.x;
		int y = curbuf->cursor.c.y;
		mselect(curbuf, x, y, x, y);
	}
}

void save(const Action *ac) {
	FILE *src, *bak;
	Line *ln;

	if (!ac->arg.v && !curbuf->path) return;
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
	for (ln = curbuf->lines; ln; ln = ln->next) {
		fputws(ln->data, src);
		if (ln->next) fputws(L"\n", src);
	}

	fclose(src);
}

void readfile(const Action *ac) {
	mreadfile((curbuf = mnewbuf()), ac->arg.v);
}

void readstr(const Action *ac) {
	mreadstr(curbuf, (char*)ac->arg.v);
}

void find(const Action *ac) {
	if (ac->arg.v) {
		char msgbuf[100];
		regex_t reg;
		Line *ln, *prev_ln = curbuf->curline;
		int i, x, y = 0, wrapped = 0;

		if ((i = regcomp(&reg, ac->arg.v, 0))) {
			regerror(i, &reg, msgbuf, sizeof(msgbuf));
			return;
		}

		for (ln = curbuf->curline; ln; ln = ln->next, ++y) {
			int offset = curbuf->cursor.c.x * sizeof(wchar_t);
			char buf[default_linebuf_size * 4];
			const wchar_t *wdat = ln->data + offset;
			regmatch_t match;

			wcsrtombs(buf, &wdat, sizeof(buf), NULL);
			i = regexec(&reg, buf, 1, &match, 0);
			x = match.rm_so - curbuf->cursor.c.x + offset;
			if (!i) {
				/* Jump to location, select match */
				mmove(curbuf, x, y);
				//mselect(curbuf, x, y, match.rm_eo - 1, y);
				regfree(&reg);
				return;
			}
			if (!ln->next && !wrapped) {
				/* Wrap to beginning of buffer, but only once */
				ln = curbuf->curline = curbuf->lines;
				y = 0;
				wrapped = 1;
			}
		}
		/* If we didn't find a match, do nothing */
		curbuf->curline = prev_ln;
		regfree(&reg);
	}
}

void motion(const Action *ac) {
	mmove(curbuf, ac->arg.x, ac->arg.y);
}

void jump(const Action *ac) {
	mjump(curbuf, ac->arg.m);
}

void coc() {
	/* Center on cursor */
	int row = getmaxy(bufwin);
	curbuf->starty = -(row / 2 - curbuf->cursor.c.y);
}

void pgup() {
	int row = getmaxy(bufwin);
	mmove(curbuf, 0, -row);
}

void pgdown() {
	int row = getmaxy(bufwin);
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

void bufdel(const Action *ac) {
	if (!ac->arg.i) {
		mfreebuf(curbuf);
	}
}

void insert(const Action *ac) {
	minsert(curbuf, ac->arg.i);
}

void freeln() {
	Line *ln = curbuf->curline, *next = ln->next ? ln->next : ln->prev;
	if (next) {
		curbuf->curline = next;
		mfreeln(ln);
		if (ln == curbuf->lines) {
			curbuf->lines = next;
		}
	}
}

void append() {
	mjump(curbuf, MARKER_END);
	mode = MODE_INSERT;
}

void newln() {
	mjump(curbuf, MARKER_END);
	minsert(curbuf, L'\n');
	mode = MODE_INSERT;
}
