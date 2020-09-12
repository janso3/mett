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

typedef struct buffer_ {
	struct buffer_ *next, *prev;
	char *path;
	Line *lines, *curline;
	Cursor cursor;
	int linexoff;
	/* custom line formatting hook */
	void (*formatln)(struct buffer_*, char*, int);
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

static void mformat_c(Buffer*, char*, int);
static void mpaintstat();
static void mpaintbuf(Buffer*);
static void mpaintcmd();

/* Bindable functions */
static void repaint();
static void quit();
static void setmode(Action*);
static void save(Action*);
static void readfile(Action*);

static void motion(Action*);
static void insert(Action*);

/* Global variables */
static Mode mode = MODE_NORMAL;
static WINDOW *bufwin, *statuswin, *cmdwin;
static Buffer *buflist = NULL, *curbuf = NULL;
static Line *cmdbuf;
static int repnum = 0;

/* We make all the declarations available to the user */
#include "config.h"

int main(int argc, char **argv) {
	int i, key;
	int row, col;

	cmdbuf = (Line*)calloc(1, sizeof(Line)+80);
	cmdbuf->length = 80;

	signal(SIGINT, msighandler);
	for (i = 1; i < argc; ++i) {
		mreadbuf((curbuf = mnewbuf()), argv[i]);
	}

	initscr();
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
				/* Number keys are reserved for repetition */
				if (isdigit(key)) {
					repnum = 10 * repnum + (key - '0');
				} else {
					for (i = 0; i < (int)(sizeof(buffer_actions) / sizeof(Action)); ++i) {
						if (buflist && key == buffer_actions[i].key) {
							mrepeat((Action*)&buffer_actions[i], repnum ? repnum : 1);
							repnum = 0;
							break;
						}
					}
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

	quit();

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
	Buffer *next = NULL;
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
	if (!(fp = fopen(path, "r"))) return 0;
	while (fgets(linecnt, len, fp) == linecnt) {
		Line *curln = ln;
		strncpy(ln->data, linecnt, len-1);
		ln->data[len-1] = 0;

		ln->next = (Line*)calloc(sizeof(Line)+default_linebuf_size, 1);
		ln = ln->next;
		ln->prev = curln;
	}

	nlines = mnumlines(buf);
	buf->linexoff = line_numbers ? mnumplaces(nlines)+1 : 0;
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
	Line *ln;
	int i, row, col, ntabs;
	if (!curbuf) return;
	getmaxyx(stdscr, row, col);

	/* Count number of tabs in line */

	if (mode == MODE_COMMAND) {
		int len = strlen(cmdbuf->data);
		move(row-1, len+2);
	} else {
		move(curbuf->cursor.y - curbuf->cursor.starty + 1,
				 curbuf->cursor.x + curbuf->linexoff);
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
				/* TODO: Insert empty line */
			} break;
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
	for (i = 0; i < n; i++) {
		ac->fn(ac);
	}
}

void mruncmd(char *buf) {
	int i;
	for (i = 0; i < (int)(sizeof(buffer_actions) / sizeof(Action)); ++i) {
		char *cmd;
		long cnt;
		if (!(cnt = strtol(buf, &cmd, 10))) cnt = 1;
		if (buffer_actions[i].cmd) {
			/* Check for valid command */
			if (/* Either the single-char keyboard shortcut... */
					(strlen(cmd) == 1 && buffer_actions[i].key == cmd[0])
					/* ...or the full command */
					|| !strncmp(buffer_actions[i].cmd, cmd, 80)) {
				long j;
				mrepeat((Action*)&buffer_actions[i], cnt);
			}
		}
	}
}

void mformat_c(Buffer *buf, char *line, int xoff) {
	/* Draw a line of C-Code */
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

	nlines = mnumlines(buflist);
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
		if (i < curbuf->cursor.starty) continue;
		if (use_colors) wattron(bufwin, COLOR_PAIR(PAIR_LINE_NUMBERS));
		if (line_numbers) wprintw(bufwin, "%d\n", i);
		if (use_colors) wattroff(bufwin, COLOR_PAIR(PAIR_LINE_NUMBERS));
		if (buf->formatln) {
			buf->formatln(buf, ln->data, buf->linexoff);
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
	if (use_colors) wattron(cmdwin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));
	wprintw(cmdwin, "$");
	wprintw(cmdwin, " %s", cmdbuf->data);
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
