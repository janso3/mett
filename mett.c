#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED
#include <assert.h>
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
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define SWAP(X, Y, T) { T SWAP = X; X = Y; Y = SWAP; }
#define BINDABLE(fn) static void fn()

enum Mode {
	MODE_NORMAL,
	MODE_INSERT,
	MODE_SELECT,
	MODE_COMMAND
};

enum Marker {
	MARKER_START,
	MARKER_MIDDLE,
	MARKER_END
};

struct Coord {
	int x, y;
};

struct Cursor {
	struct Coord c; /* Cursor coordinate */
	struct Coord v0; /* Visual selection start */
	struct Coord v1; /* Visual selection end */
};

struct Line {
	struct Line *next, *prev;
	size_t backbuf_size;
	wchar_t data[];
};

struct Buffer {
	char *path;
	struct Buffer *next;
	struct Line *curline;
	struct Cursor cursor;
	int starty;
	int offsetx;
	int numlines;
};

struct Action {
	wchar_t *cmd;
	int key;
	void (*fn)();
	union {
		struct { int x, y; };
		int i;
		void *v;
		enum Marker m;
	} arg;
};

static int32_t min(int32_t, int32_t);
static int32_t max(int32_t, int32_t);

static void msighandler(int);

static struct Buffer* mnewbuf();
static void mfreebuf(struct Buffer*);
static void mclearbuf(struct Buffer*);
static int  mreadfile(struct Buffer*, const char*);
static void mreadstr(struct Buffer*, const char*);

static struct Line* mnewline(size_t backbuf_size);
static void mfreeln(struct Buffer*, struct Line*);
static struct Line* mresizeline(struct Line*, size_t);
static struct Line* mfirstline(struct Buffer*);
static int  mnumcols(struct Line*, int );
static void mupdatecursor();
static void mcmdkey(wint_t);
static void minsert(struct Buffer*, wint_t);
static int  mindent(struct Line*, int);
static void mmove(struct Buffer*, int, int);
static void mjump(struct Buffer*, enum Marker);
static void mselect(struct Buffer*, int, int, int, int);
static void mrepeat(const struct Action*, int);
static void mruncmd(wchar_t*);

static void mpaintstat();
static void mpaintln(struct Buffer*, struct Line*, WINDOW*, int, int, bool);
static void mpaintbuf(struct Buffer*, WINDOW*, bool);
static void mpaintcmd();

BINDABLE (resize);
BINDABLE (repaint);
BINDABLE (handlemouse);
BINDABLE (quit);
BINDABLE (setmode);
BINDABLE (save);
BINDABLE (readfile);
BINDABLE (readstr);
BINDABLE (print);
BINDABLE (find);
BINDABLE (listbuffers);
BINDABLE (motion);
BINDABLE (jump);
BINDABLE (coc);
BINDABLE (pgup);
BINDABLE (pgdown);
BINDABLE (cls);
BINDABLE (bufsel);
BINDABLE (bufdel);
BINDABLE (insert);
BINDABLE (freeln);
BINDABLE (append);
BINDABLE (newln);

/* Global variables */
static enum Mode mode = MODE_NORMAL;
static WINDOW *bufwin, *statuswin, *cmdwin;
static struct Buffer *curbuf, *cmdbuf;
static int repcnt = 0;

/* We make all the declarations available to the user */
#include "config.h"

int main(int argc, char **argv) {
	int i;
	wint_t key;

	setlocale(LC_ALL, "");

	/* Init buffers */
	cmdbuf = mnewbuf();
	minsert(cmdbuf, L' ');
	cmdbuf->offsetx = 0;
	signal(SIGHUP,  msighandler);
	signal(SIGKILL, msighandler);
	signal(SIGINT,  msighandler);
	signal(SIGTERM, msighandler);

	curbuf = mnewbuf();
	for (i = 1; i < argc; ++i)
		mreadfile(curbuf, argv[i]);

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

	resize();
	repaint();

	for (;;) {
		get_wch(&key);
		if (key != (wint_t)ERR) {
			switch (mode) {
			case MODE_NORMAL:
				/* Special keys will cancel action sequences */
				if (key == ESC || key == '\n') repcnt = 0;
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
					mclearbuf(cmdbuf);
					minsert(cmdbuf, L' ');
					resize();
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

struct Buffer* mnewbuf() {
	/* Create new buffer and insert at start of the list */
	struct Buffer *next = NULL;
	if (curbuf) next = curbuf;
	if (!(curbuf = (struct Buffer*)calloc(sizeof(struct Buffer), 1))) return NULL;
	curbuf->next = next;
	curbuf->offsetx = 4;
	mselect(curbuf, -1, -1, -1, -1);
	return curbuf;
}

void mfreebuf(struct Buffer *buf) {
	free(buf->path);
	mclearbuf(buf);
}

void mclearbuf(struct Buffer *buf) {
	struct Line *firstline, *ln;
	if (!(firstline = mfirstline(buf))) return;
	ln = firstline;
	while (ln) {
		struct Line *next = ln->next;
		free(ln);
		ln = next;
	}
	buf->cursor.c.x = buf->cursor.c.y = 0;
	buf->numlines = 0;
	buf->curline = NULL;
}

int mreadfile(struct Buffer *buf, const char *path) {
	FILE *fp = NULL;

	if (path[0] == '-' && !path[1]) fp = stdin;
	else fp = fopen(path, "r");

	if (fp) {
		wchar_t c;
		int m = mode;
		bool indent = auto_indent;

		auto_indent = FALSE;
		mode = MODE_INSERT;

		while ((c = fgetwc(fp)) != EOF)
			minsert(buf, c);

		auto_indent = indent;
		mode = m;
	}

	buf->curline = mfirstline(buf);
	buf->path = (char*)calloc(strlen(path)+1, 1);
	strcpy(buf->path, path);
	if (fp) fclose(fp);

	return 1;
}

void mreadstr(struct Buffer *buf, const char *str) {
	int i, len, m = mode;
	len = strlen(str);
	mode = MODE_INSERT;
	for (i = 0; i < len; ++i)
		minsert(buf, str[i]);
	mode = m;
}

struct Line* mnewline(size_t backbuf_size) {
	struct Line *line = calloc(sizeof(struct Line) + backbuf_size, 1);
	assert(line);
	line->backbuf_size = backbuf_size;
	return line;
}

void mfreeln(struct Buffer *buf, struct Line *ln) {
	struct Line *next = NULL;

	if (!ln) return;
	if (ln->prev) {
		ln->prev->next = ln->next;
		next = ln->prev;
	}
	if (ln->next) {
		ln->next->prev = ln->prev;
		next = ln->next;
	}

	if (ln == buf->curline)
		buf->curline = next;

	free(ln);
	buf->numlines--;
}

struct Line* mresizeline(struct Line *ln, size_t size) {
	struct Line *prev = ln->prev, *next = ln->next;
	ln = realloc(ln, sizeof(struct Line) + size);
	assert(ln);
	ln->backbuf_size = size;
	if (prev)
		prev->next = ln;
	if (next)
		next->prev = ln;
	return ln;
}

struct Line* mfirstline(struct Buffer *buf) {
	struct Line *first = NULL, *ln = buf->curline;
	while (ln) {
		first = ln;
		ln = ln->prev;
	}
	return first;
}

int mnumcols(struct Line *ln, int end) {
	/* Count number of columns until cursor */
	int i, ncols;
	for (i = ncols = 0; i < end; ++i) {
		if (ln->data[i] == L'\t') ncols += tab_width;
		else ncols += max(0, wcwidth(ln->data[i]));
	}
	return ncols;
}

void mupdatecursor() {
	/* Place the cursor depending on the mode */
	WINDOW *win = mode == MODE_COMMAND ? cmdwin : bufwin;
	struct Buffer *buf = mode == MODE_COMMAND ? cmdbuf : curbuf;
	int ncols = mnumcols(buf->curline, buf->cursor.c.x);
	wmove(win, buf->cursor.c.y - buf->starty, buf->offsetx + ncols);
	wrefresh(win);
}

void mcmdkey(wint_t key) {
	/* Number keys (other than 0) are reserved for repetition */
	if (isdigit(key) && !(key == '0' && !repcnt)) {
		repcnt = min(10 * repcnt + (key - '0'), max_cmd_repetition);
	} else {
		size_t i;
		for (i = 0; i < (size_t)(sizeof(buffer_actions) / sizeof(struct Action)); ++i) {
			if (key == (wint_t)buffer_actions[i].key) {
				mclearbuf(cmdbuf);
				if (buffer_actions[i].cmd) {
					size_t j, len;
					len = wcslen(buffer_actions[i].cmd);
					for (j = 0; j < len; ++j)
						minsert(cmdbuf, buffer_actions[i].cmd[j]);
					mjump(cmdbuf, MARKER_END);
				}
				mrepeat(&buffer_actions[i], repcnt ? repcnt : 1);
			}
		}
		repcnt = 0;
	}
}

void minsert(struct Buffer *buf, wint_t key) {
	size_t idx, len = 0;
	struct Line *ln = buf->curline;

	/* Create or resize the current line if needed. */
	if (!ln) {
		ln = buf->curline = mnewline(default_linebuf_size);
		buf->numlines = 1;
	} else {
		len = wcslen(ln->data);
		if ((len+1) * sizeof(wchar_t) >= ln->backbuf_size)
			ln = buf->curline = mresizeline(ln, ln->backbuf_size * 2);
	}

	idx = min(buf->cursor.c.x, len);

	switch (key) {
	case '\b':
	case 127:
	case KEY_BACKSPACE:
		if (idx) {
			memcpy(&ln->data[idx-1], &ln->data[idx], (len - idx + 1) * sizeof(wchar_t));
			buf->cursor.c.x--;
		} else if (ln->prev) {
			int plen = wcslen(ln->prev->data);
			if (ln->prev->backbuf_size <= plen + ln->backbuf_size)
				ln->prev = mresizeline(ln->prev, ln->prev->backbuf_size + ln->backbuf_size);
			wcscpy(ln->prev->data+plen, ln->data);
			mmove(buf, plen + buf->cursor.c.x, -1);
			buf->curline = ln->prev;
			mfreeln(buf, ln);
			buf->numlines--;
		}
		break;
	case KEY_DC:
		memcpy(&ln->data[idx], &ln->data[idx+1], (len - idx + 1) * sizeof(wchar_t));
		break;
	case '\n':
		{
			int ox = 0;
			struct Line *old = ln;
			ln = mnewline(old->backbuf_size);
			ln->next = old->next;
			ln->prev = old;
			if (old->next) old->next->prev = ln;
			old->next = ln;

			if (auto_indent) {
				/* Indent to the last position */
				size_t x, mx;
				for (x = mx = 0; x < idx; ++x) {
					if (old->data[x] == L'\t') mx += tab_width;
					else if (iswspace(old->data[x])) mx++;
					else break;
				}
				ox = mindent(ln, mx);
			}

			memcpy(&ln->data[ox], &old->data[idx], (len - idx + 1) * sizeof(wchar_t));
			old->data[idx] = 0;
			mjump(buf, MARKER_START);
			mmove(buf, ox, +1);

			if (mode == MODE_COMMAND) {
				mruncmd(cmdbuf->curline->prev->data);
				resize();
			}

			buf->numlines++;
		}
		break;
	default:
		{
			if (len) memcpy(&ln->data[idx + 1], &ln->data[idx], (len - idx + 1) * sizeof(wchar_t));
			ln->data[idx] = key;
			buf->cursor.c.x++;
		}
		break;
	}
}

int mindent(struct Line *ln, int n) {
	int i, j, tabs, spaces;

	tabs = n / tab_width;
	spaces = n % tab_width;

	for (i = 0; i < tabs; ++i)
		ln->data[i] = L'\t';
	for (j = 0; j < spaces; ++j)
		ln->data[i+j] = L' ';

	return tabs + spaces;
}

void mmove(struct Buffer *buf, int x, int y) {
	int i, len;
	int row;

	if (!buf->curline) return;

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
				buf->starty--;
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
				buf->starty++;
			}
		}
	}

	/* Restrict cursor to line content */
	len = wcslen(buf->curline->data);
	buf->cursor.c.x = max(min(buf->cursor.c.x, len), 0);

	/* Update selection end */
	if (mode == MODE_SELECT) {
		buf->cursor.v1 = (struct Coord){buf->cursor.c.x, buf->cursor.c.y};
	}
}

void mjump(struct Buffer *buf, enum Marker mark) {
	switch(mark) {
	case MARKER_START:
		buf->cursor.c.x = 0;
		break;
	case MARKER_MIDDLE:
		{
			struct Line *ln = buf->curline;
			if (!ln) return;
			size_t len = wcslen(ln->data);
			buf->cursor.c.x = (len/2);
		}
		break;
	case MARKER_END:
		{
			struct Line *ln = buf->curline;
			if (!ln) return;
			size_t len = wcslen(ln->data);
			buf->cursor.c.x = max(len, 0);
		}
		break;
	}
}

void mselect(struct Buffer *buf, int x1, int y1, int x2, int y2) {
	buf->cursor.v0 = (struct Coord){ x1, y1 };
	buf->cursor.v1 = (struct Coord){ x2, y2 };
}

void mrepeat(const struct Action *ac, int n) {
	int i;
	n = min(n, max_cmd_repetition);
	for (i = 0; i < n; ++i)
		ac->fn(ac);
}

char* mexec(const char *cmd) {
	/* Execute cmd and return stdout.
	 * TODO: Get rid of the fixed buffer. */
	FILE *fp;
	static char buf[1024 * 1024 * 4];

	fp = popen(cmd, "r");
	if (!fp) return NULL;

	fread(buf, 1, sizeof(buf), fp);
	pclose(fp);
	
	return buf;
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

	for (cmdlen = 0; cmdlen < exlen; ++cmdlen) {
			if ((cmd[cmdlen] == L' ' && cmdlen) || cmd[cmdlen] == L'!') break;
	}

	/* Parse optional argument */
	if (exlen > cmdlen) {
		const wchar_t *warg = cmd + cmdlen;
		if (warg[0] == L' ') warg++;
		arg = (char*)malloc((exlen - cmdlen) * 4);
		wcsrtombs(arg, &warg, (exlen - cmdlen) * 4, NULL);
	}

	if (cmd[0] == L' ') {
			cmd++;
			cmdlen--;
	}

	/* Is it a builtin command? */
	for (i = 0; i < (int)(sizeof(buffer_actions) / sizeof(struct Action)); ++i) {
		if (buffer_actions[i].cmd) {
			/* Check for valid command */
			if (/* Either the single-char keyboard shortcut... */
			    (cmdlen == 1 && buffer_actions[i].key == cmd[0]) ||
			    /* ...or the full command */
			    ((unsigned)cmdlen == wcslen(buffer_actions[i].cmd) && !wcsncmp(buffer_actions[i].cmd, cmd, cmdlen))) {
				struct Action ac;
				memcpy(&ac, &buffer_actions[i], sizeof(struct Action));
				if (arg) {
					/* Check for shell command */
					if (arg[0] == '!') ac.arg.v = mexec(arg+1);
					else ac.arg.v = arg;
				}

				/* FIXME: This is an ugly hack */
				bool indent = auto_indent;
				auto_indent = FALSE;
				mode = MODE_INSERT;
				mrepeat(&ac, cnt);
				mode = MODE_COMMAND;
				auto_indent = indent;
				/* TODO: Parse next command in chain */
				break;
			}
		}
	}

	free(arg);
}

void mpaintstat() {
	struct Buffer *cur = curbuf;
	int col, bufsize;
	char textbuf[32];
	char *bufname = "~scratch~";
	const char *modes[] = { "NORMAL", "INSERT", "SELECT", "COMMAND" };

	col = getmaxx(stdscr);
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));

	/* Background */
	whline(statuswin, ' ', col);

	/* Buffer name, buffer length */
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));

	if (curbuf && curbuf->path) bufname = curbuf->path;
	wprintw(statuswin, "%s, %i lines", bufname, curbuf->numlines);

	/* Mode, cursor pos */
	cur = mode == MODE_COMMAND ? cmdbuf : curbuf;
	bufsize = snprintf(textbuf, sizeof(textbuf), "%s %d:%d", modes[mode], cur ? cur->cursor.c.y : 0, cur ? cur->cursor.c.x : 0);
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));
	mvwprintw(statuswin, 0, col - bufsize, "%s", textbuf);
	if (use_colors) wattroff(statuswin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));

	if (use_colors) wattroff(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));
	wrefresh(statuswin);
}

void mpaintln(struct Buffer *buf, struct Line *ln, WINDOW *win, int y, int n, bool numbers) {
	size_t x, len;
	size_t i, j;
	size_t col;

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
		struct Coord sel_start = buf->cursor.v0;
		struct Coord sel_end = buf->cursor.v1;
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
				for (j = 0; j < tab_width-1; ++j) {
					mvwadd_wch(win, y, x, &cc);
					x++;
				}
			}
			break;
			default:
			{
				cchar_t cc;
				setcchar(&cc, &c, 0, 0, NULL);
				mvwadd_wch(win, y, x, &cc);
				x++;
			}
			break;
		}
		wattroff(win, COLOR_PAIR(PAIR_BUFFER_CONTENTS));
	}
}

void mpaintbuf(struct Buffer *buf, WINDOW *win, bool numbers) {
	int i, cp;
	int row;
	struct Line *ln;

	if (!buf->curline) return;
	
	row = getmaxy(win);
	cp = buf->cursor.c.y - buf->starty;

	/* Paint from the cursor to the bottom */
	for (i = cp, ln = buf->curline; i < row && ln; ++i, ln = ln->next)
		mpaintln(buf, ln, win, i, i - cp, numbers);

	/* Paint from the cursor to the top */
	for (i = cp, ln = buf->curline; i >= 0 && ln; --i, ln = ln->prev)
		mpaintln(buf, ln, win, i, cp - i, numbers);

	wrefresh(win);
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
	wrefresh(cmdwin);
}

void resize() {
	int row, col;
	getmaxyx(stdscr, row, col);
	if (statuswin) delwin(statuswin);
	if (cmdwin) delwin(cmdwin);
	if (bufwin) delwin(bufwin);
	statuswin = newwin(1, col, 0, 0);
	bufwin = newwin(row - cmdbuf->numlines - 1, col, 1, 0);
	cmdwin = newwin(cmdbuf->numlines, col, row - cmdbuf->numlines, 0);
}

void repaint() {
	werase(bufwin);
	werase(statuswin);
	werase(cmdwin);
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
	struct Buffer *buf = curbuf;
	do mfreebuf(buf); while((buf = buf->next));
	delwin(cmdwin);
	delwin(bufwin);
	delwin(statuswin);
	endwin();
	exit(0);
}

void setmode(const struct Action *ac) {
	if ((mode = (enum Mode)ac->arg.i) == MODE_SELECT && curbuf) {
		/* Capture start of visual selection */
		int x = curbuf->cursor.c.x;
		int y = curbuf->cursor.c.y;
		mselect(curbuf, x, y, x, y);
	}
}

void save(const struct Action *ac) {
	FILE *src, *bak;
	struct Line *ln;

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
	for (ln = mfirstline(curbuf); ln; ln = ln->next) {
		fputws(ln->data, src);
		if (ln->next) fputws(L"\n", src);
	}

	fclose(src);
}

void readfile(const struct Action *ac) {
	mreadfile((curbuf = mnewbuf()), ac->arg.v);
}

void readstr(const struct Action *ac) {
	mreadstr(curbuf, (char*)ac->arg.v);
}

void print(const struct Action *ac) {
	mreadstr(cmdbuf, (char*)ac->arg.v);
	resize();
}

void find(const struct Action *ac) {
	if (ac->arg.v) {
		char msgbuf[100];
		regex_t reg;
		struct Line *ln, *prev_ln = curbuf->curline;
		int i, x, y = 0, wrapped = 0;

		if ((i = regcomp(&reg, ac->arg.v, 0))) {
			regerror(i, &reg, msgbuf, sizeof(msgbuf));
			return;
		}

		for (ln = curbuf->curline; ln; ln = ln->next, ++y) {
			int lineoff = curbuf->cursor.c.x;
			char buf[default_linebuf_size * 4];
			const wchar_t *wdat = ln->data + lineoff * sizeof(wchar_t);
			regmatch_t match;

			wcsrtombs(buf, &wdat, sizeof(buf), NULL);
			i = regexec(&reg, buf, 1, &match, 0);
			x = match.rm_so - curbuf->cursor.c.x + lineoff;
			if (!i) {
				/* Jump to location, select match */
				int mx = x + curbuf->cursor.c.x;
				int my = y + curbuf->cursor.c.y - curbuf->starty;
				mmove(curbuf, x, y);
				mselect(curbuf, mx, my, mx + match.rm_eo - match.rm_so - 1, my);
				regfree(&reg);
				return;
			}
			if (!ln->next && !wrapped) {
				/* Wrap to beginning of buffer, but only once */
				ln = curbuf->curline = mfirstline(curbuf);
				y = 0;
				wrapped = 1;
			}
		}
		/* If we didn't find a match, do nothing */
		curbuf->curline = prev_ln;
		regfree(&reg);
	}
}

void listbuffers() {
	struct Buffer *buf = curbuf;
	int i = 0;
	do {
		char str[64];
		snprintf(
			str,
			sizeof(str),
			buf == curbuf ? "*%d %s\n" : " %d %s\n",
			i, buf->path);
		mreadstr(cmdbuf, str);
		i++;
	} while((buf = buf->next));
}

void motion(const struct Action *ac) {
	mmove(curbuf, ac->arg.x, ac->arg.y);
}

void jump(const struct Action *ac) {
	mjump(curbuf, ac->arg.m);
}

void coc() {
	/* Center on cursor */
	int row = getmaxy(bufwin);
	curbuf->starty = -(row / 2 - curbuf->cursor.c.y);
}

void pgup() {
	int row = getmaxy(bufwin)-1;
	mmove(curbuf, 0, -row);
}

void pgdown() {
	int row = getmaxy(bufwin)-1;
	mmove(curbuf, 0, +row);
}

void cls() {
	mclearbuf(cmdbuf);
	resize();
}

void bufsel(const struct Action *ac) {
	/* TODO: Forward/backward multiple buffers */
	/*if (ac->arg.i < 0) {
		if (curbuf->prev) curbuf = curbuf->prev;
	} else if (ac->arg.i > 0) {
		if (curbuf->next) curbuf = curbuf->next;
	}*/
}

void bufdel(const struct Action *ac) {
	if (!ac->arg.i) {
		mfreebuf(curbuf);
		resize();
	}
}

void insert(const struct Action *ac) {
	minsert(curbuf, ac->arg.i);
}

void freeln() {
	mfreeln(curbuf, curbuf->curline);
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
