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
		char *path;
	} arg;
} Action;

static void sighandler(int);
static int numplaces(int);

static Buffer* newbuf();
static void freebuf(Buffer*);
static int readbuf();
static int numlines(Buffer*);

static void updatecursor();
static void insert(int);
static void runcmd(char*);

static void format_c(Buffer*, char*, int);
static void paintstat();
static void paintbuf(Buffer*);
static void paintcmd();
static void repaint();

static void quit();
static void setmode(Action*);
static void seek(Action*);
static void save(Action*);

static void motion(Action*);

static Mode mode = MODE_NORMAL;
static WINDOW *bufwin, *statuswin, *cmdwin;
static Buffer *buflist = NULL, *curbuf = NULL;
static char cmdbuf[80];

/* We make all the declarations available to the user */
#include "config.h"

int main(int argc, char **argv) {
	int i, key;
	int row, col;

	signal(SIGINT, sighandler);
	for (i = 1; i < argc; ++i) {
		readbuf((curbuf = newbuf()), argv[i]);
	}

	initscr();
	noecho();
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);
	notimeout(stdscr, TRUE);
	use_default_colors();
	clear();
	refresh();

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
				for (i = 0; i < (int)(sizeof(buffer_actions) / sizeof(Action)); ++i) {
					if (buflist && key == buffer_actions[i].key)
						buffer_actions[i].fn(&buffer_actions[i]);
				}
				break;
			case MODE_WRITE:
				if (key == ESC) mode = MODE_NORMAL;
				else insert(key);
				break;
			case MODE_COMMAND:
				if (key == ESC) mode = MODE_NORMAL;
				else insert(key);
				break;
			}
			repaint();
		}
	}

	delwin(cmdwin);
	delwin(bufwin);
	delwin(statuswin);
	endwin();

	return 0;
}

void sighandler(int signum) {
	switch (signum) {
	case SIGHUP:
	case SIGKILL:
	case SIGINT:
		quit();
		break;
	}
}

int numplaces(int n) {
	/* How many visual places in a number? */
	int r = n < 0 ? 2 : 1;
	while (n > 9) {
	    n /= 10;
	    r++;
	}
	return r;
}

Buffer* newbuf() {
	/* Create new buffer and insert at start of the list */
	Buffer *next = NULL;
	if (buflist) next = buflist;
	if (!(buflist = (Buffer*)calloc(1, sizeof(Buffer)))) return NULL;
	buflist->next = next;
	if (next) buflist->next->prev = buflist;
	return buflist;
}

void freebuf(Buffer *buf) {
	free(buf->path);
}

int readbuf(Buffer *buf, const char *path) {
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

	nlines = numlines(buf);
	buf->linexoff = line_numbers ? numplaces(nlines)+1 : 0;
	buf->path = (char*)calloc(1, strlen(path)+1);
	strcpy(buf->path, path);
	fclose(fp);

	return 1;
}

int numlines(Buffer *buf) {
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

void updatecursor() {
	int row, col;
	if (!curbuf) return;
	getmaxyx(stdscr, row, col);
	if (mode == MODE_COMMAND) {
		int len = strlen(cmdbuf);
		move(row-1, len+2);
	} else {
		move(curbuf->cursor.y - curbuf->cursor.starty + 1, curbuf->cursor.x + curbuf->linexoff);
	}
}

void insert(int key) {
	if (mode == MODE_COMMAND) {
		/* Insert into command-line */
		int len = strlen(cmdbuf);
		switch (key) {
		case KEY_BACKSPACE:
			cmdbuf[len-1] = 0;
			break;
		case '\n':
			mode = MODE_NORMAL;
			runcmd(cmdbuf);
			memset(cmdbuf, 0, sizeof(cmdbuf));
			break;
		default:
			cmdbuf[len] = (char)key;
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
		case '\n':
			break;
		default:
			memcpy(ln->data+idx+1, ln->data+idx, len);
			ln->data[idx] = key;
			curbuf->cursor.x++;
			break;
		}
	}
}

void runcmd(char *buf) {
	int i;
	for (i = 0; i < (int)(sizeof(buffer_actions) / sizeof(Action)); ++i) {
		char *end;
		long cnt;
		if (!(cnt = strtol(buf, &end, 10))) cnt = 1;
		if (buffer_actions[i].cmd && !strncmp(buffer_actions[i].cmd, end, 80)) {
			for (; cnt; cnt--) buffer_actions[i].fn(&buffer_actions[i]);
		}
	}
}

void format_c(Buffer *buf, char *line, int xoff) {
	/* Draw a line of C-Code */
}

void paintstat() {
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

	nlines = numlines(buflist);
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

void paintbuf(Buffer *buf) {
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

void paintcmd() {
	if (use_colors) wattron(cmdwin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));
	wprintw(cmdwin, "$");
	if (use_colors) wattroff(cmdwin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));
	wprintw(cmdwin, " %s", cmdbuf);
	wrefresh(cmdwin);
}

void repaint() {
	int row, col;
	getmaxyx(stdscr, row, col);
	delwin(statuswin);
	delwin(cmdwin);
	delwin(bufwin);
	clear();
	refresh();
	statuswin = newwin(1, col, 0, 0);
	bufwin = newwin(row-2, col, 1, 0);
	cmdwin = newwin(1, col, row-1, 0);
	paintbuf(buflist);
	paintstat();
	paintcmd();
	updatecursor();
	refresh();
}

void quit() {
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
		if (curbuf->cursor.y - curbuf->cursor.starty >= row-1) {
			/* Scroll the view down */
			curbuf->cursor.starty++;
		}
		curbuf->cursor.y++;
		if (curbuf->curline->next) curbuf->curline = curbuf->curline->next;
	}
}

void seek(Action *ac) {
}

void save(Action *ac) {
	FILE *src, *bak;
	Line *ln;

	if (!ac->arg.path && !curbuf->path) {
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

	if (!(src = fopen(ac->arg.path ? ac->arg.path : curbuf->path, "w+"))) return;
	for (ln = curbuf->lines; ln && ln->next; ln = ln->next) {
		fputs(ln->data, src);
	}

	fclose(src);
}
