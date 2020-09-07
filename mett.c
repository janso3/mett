#include <curses.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

typedef enum {
	MODE_MOVE,
	MODE_INSERT
} Mode;

typedef struct {
	int x, y;
	/* the first line drawn from the top */
	int startln;
} Cursor;

typedef struct line_ {
	struct line_ *next, *prev;
	int length;
	char *data;
} Line;

typedef struct buffer_ {
	struct buffer_ *next, *prev;
	char *path;
	Line *lines;
	Cursor cursor;
	int linexoff;
	/* custom line formatting hook */
	void (*formatln)(struct buffer_*, char*, int);
} Buffer;

typedef struct {
	int key;
	void (*fn)();
	struct {
		int x, y;
	} arg;
} Action;

static int numplaces(int);

static Buffer* lastbuf();
static Buffer* newbuf();
static void freebuf(Buffer*);
static int readbuf();
static int numlines(Buffer*);

static void updatecursor();

static void format_c(Buffer*, char*, int);
static void paintstat();
static void paintbuf(WINDOW*, Buffer*);
static void repaint();

static void motion();

static Mode mode;
static WINDOW *statuswin;
static Buffer *buflist;

#include "config.h"

/* How many decimal places in a number? */
int numplaces(int n) {
	int r = 1;
	if (n < 0) n = (n == INT_MIN) ? INT_MAX: -n;
	while (n > 9) {
	    n /= 10;
	    r++;
	}
	return r;
}

/* Get the buffer list head */
Buffer* lastbuf() {
	Buffer *cur = NULL, *ptr = buflist;
	while (ptr && (ptr = ptr->next)){
		cur = ptr;
	}
	return cur;
}

/* Create new buffer and insert at start of the list */
Buffer* newbuf() {
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

/* Read a file into a buffer */
int readbuf(Buffer *buf, const char *path) {
	FILE *fp;
	char *linecnt;
	size_t len = default_linebuf_size;
	int nlines;
	Line *ln = NULL;

	ln = buf->lines = (Line*)calloc(sizeof(Line), 1);
	linecnt = (char*)malloc(len);

	if (!(fp = fopen(path, "r"))) return 0;
	while (fgets(linecnt, len, fp) == linecnt) {
		Line *curln;
		size_t nb;

		nb = MAX(len, default_linebuf_size);
		ln->data = (char*)malloc(nb);
		strncpy(ln->data, linecnt, len-1);
		ln->data[len-1] = 0;

		curln = ln;
		ln->next = (Line*)calloc(sizeof(Line), 1);
		ln = ln->next;
		ln->prev = curln;
	}

	nlines = numlines(buf);
	buf->linexoff = line_numbers ? numplaces(nlines)+1 : 0;

	buf->path = (char*)calloc(1, strlen(path)+1);
	strcpy(buf->path, path);

	free(linecnt);
	fclose(fp);

	return 1;
}

/* Number of lines in a buffer */
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
	if (!buflist) return;
	move(buflist->cursor.y, buflist->cursor.x + buflist->linexoff);
}

/* Draw a line of C-Code */
void format_c(Buffer *buf, char *line, int xoff) {
}

/* Paint the status bar */
void paintstat() {
	int row, col, nlines, bufsize;
	Buffer *cur = buflist;
	char textbuf[32];
	char *bufname = "Untitled";
	const char *modes[] = { "MOVE", "INSERT" };

	getmaxyx(stdscr, row, col);
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));

	/* background */
	whline(statuswin, ' ', col);

	/* buffer name, buffer length */
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));

	nlines = numlines(buflist);
	if (buflist && buflist->path) bufname = buflist->path;
	wprintw(statuswin, "%s, %i lines", bufname, nlines);

	/* mode, cursor pos */
	bufsize = snprintf(textbuf, sizeof(textbuf), "%s %d:%d", modes[mode], cur ? cur->cursor.y : 0, cur ? cur->cursor.x : 0);
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));
	mvwprintw(statuswin, 0, col - bufsize, "%s", textbuf);
	if (use_colors) wattroff(statuswin, COLOR_PAIR(PAIR_STATUS_HIGHLIGHT));

	wrefresh(statuswin);

	if (use_colors) wattroff(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));
}

/* Paint a buffer onto a window */
void paintbuf(WINDOW *win, Buffer *buf) {
	int i, l;
	int row, col;
	Line *ln;
	Cursor *cur;

	if (!buf) return;
	getmaxyx(stdscr, row, col);
	cur = &buf->cursor;

	for (i = l = 0, ln = buf->lines; l < row-1 && ln->next; ++i, ln = ln->next) {
		if (i < cur->startln) continue;
		wmove(win, l, 0);
		if (use_colors) wattron(win, COLOR_PAIR(PAIR_LINE_NUMBERS));
		if (line_numbers) wprintw(win, "%d\n", i+1);
		if (use_colors) wattroff(win, COLOR_PAIR(PAIR_LINE_NUMBERS));
		if (buf->formatln) {
			buf->formatln(buf, ln->data, buf->linexoff);
		} else {
			mvwprintw(win, l, buf->linexoff, "%s", ln->data);
		}
		l++;
	}
}

/* Repaint the whole screen */
void repaint() {
	int row, col;
	getmaxyx(stdscr, row, col);
	delwin(statuswin);
	statuswin = newwin(1, col, row-1, 0);
	paintbuf(stdscr, buflist);
	paintstat();
	updatecursor();
	refresh();
}

void motion(Action *ac) {
	int row, col;
	getmaxyx(stdscr, row, col);

	/* left / right */
	if (ac->arg.x == -1) {
		buflist->cursor.x = MAX(buflist->cursor.x-1, 0);
	} else if (ac->arg.x == +1) {
		buflist->cursor.x = MIN(buflist->cursor.x+1, col-buflist->linexoff-1);
	}

	/* up / down */
	if (ac->arg.y == -1) {
		buflist->cursor.y = MAX(buflist->cursor.y-1, 0);
	} else if (ac->arg.y == +1) {
		buflist->cursor.y = MIN(buflist->cursor.y+1, row-2);
	}
}

int main(int argc, char **argv) {
	int i, key;
	int row, col;

	mode = MODE_MOVE;
	for (i = 1; i < argc; ++i) {
		readbuf(newbuf(), argv[i]);
	}

	initscr();
	noecho();
	nonl();
	keypad(stdscr, TRUE);
	use_default_colors();
	refresh();

	if (!has_colors()) use_colors = FALSE;
	if (use_colors) {
		start_color();
		for (i = 1; i < NUM_COLOR_PAIRS; ++i)
			init_pair(i, color_pairs[i][0], color_pairs[i][1]);
	}

	getmaxyx(stdscr, row, col);
	statuswin = newwin(1, col, row-1, 0);
	repaint();

	while ((key = wgetch(stdscr)) != ERR) {
		for (i = 0; i < sizeof(buffer_actions) / sizeof(Action); ++i) {
			if (buflist && key == buffer_actions[i].key) {
				buffer_actions[i].fn(&buffer_actions[i]);
			}
		}
		repaint();
	}

	delwin(statuswin);
	endwin();

	return 0;
}
