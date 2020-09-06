#include <curses.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "config.h"

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

typedef struct {
	int x, y;
	/* the first line drawn from the top */
	int startln;
} Cursor;

typedef struct line_ {
	struct line_ *next;
	struct line_ *prev;
	int length;
	char *data;
} Line;

typedef struct buffer_ {
	struct buffer_ *next; struct buffer_ *prev;
	char *path;
	Line *lines;
	Cursor cursor;
	/* custom line formatting hook */
	void (*formatln)(struct buffer_*, char*);
} Buffer;

static void die(int);
static int numplaces(int);

static Buffer* lastbuf();
static Buffer* newbuf();
static void freebuf(Buffer*);
static int readbuf();
static int numlines(Buffer*);

static void updatecursor();
static void repaint();

static void paintbuf(WINDOW*, Buffer*);
static void paintstat();

static WINDOW *statuswin;
static Buffer *buflist;

void die(int err) {
	endwin();
	exit(err);
}

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
	buflist->next->prev = buflist;
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
	ssize_t read;
	Line *ln = NULL;

	ln = buf->lines = (Line*)calloc(sizeof(Line), 1);
	linecnt = (char*)malloc(len);

	if (!(fp = fopen(path, "r"))) return 0;
	while ((read = getline(&linecnt, &len, fp)) != -1) {
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

/* Update the cursor position */
void updatecursor() {
	//cursor.x = 0;
	//cursor.y = 10;
}

/* Paint the status bar */
void paintstat() {
	int row, col, nlines, bufsize;
	char textbuf[32];
	char *bufname = "Untitled";

	getmaxyx(stdscr, row, col);
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));

	/* background */
	whline(statuswin, ' ', col);

	/* buffer name, buffer length */
	nlines = numlines(buflist);
	if (buflist && buflist->path) bufname = buflist->path;
	wprintw(statuswin, "%s, %i lines", bufname, nlines);

	/* cursor pos */
	bufsize = snprintf(textbuf, sizeof(textbuf), "%d:%d", 0, 0/*cursor.y, cursor.x*/);
	mvwprintw(statuswin, 0, col - bufsize, "%s", textbuf);
	wrefresh(statuswin);

	if (use_colors) wattroff(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));
}

/* Paint a buffer onto a window */
void paintbuf(WINDOW *win, Buffer *buf) {
	int i, l;
	int row, col;
	int nlines, xmargin;
	Line *ln;
	Cursor *cur = &buf->cursor;

	getmaxyx(stdscr, row, col);
	nlines = numlines(buf);
	return;
	xmargin = line_numbers ? numplaces(nlines)+1 : 0;

	for (i = l = 0, ln = buf->lines; l < row-1 && ln->next; ++i, ln = ln->next) {
		if (i < cur->startln) continue;
		wmove(win, l, 0);
		if (use_colors) wattron(win, COLOR_PAIR(PAIR_LINE_NUMBERS));
		if (line_numbers) wprintw(win, "%d\n", i+1);
		if (use_colors) wattroff(win, COLOR_PAIR(PAIR_LINE_NUMBERS));
		mvwprintw(win, l, xmargin, "%s", ln->data);
		l++;
	}
}

/* Repaint the whole screen */
void repaint() {
	int row, col;
	clear();
	refresh();
	getmaxyx(stdscr, row, col);
	delwin(statuswin);
	statuswin = newwin(1, col, row-1, 0);
	paintbuf(stdscr, buflist);
	paintstat();
	refresh();
}

int main(int argc, char **argv) {
	int i, key;
	int row, col;

	for (i = 1; i < argc; ++i) {
		readbuf(newbuf(), argv[i]);
	}

	printf("%s\n", buflist->path);
	return;

	initscr();
	noecho();
	nonl();
	keypad(stdscr, TRUE);
	use_default_colors();
	refresh();

	if (!has_colors()) use_colors = FALSE;
	if (use_colors) {
		int i;
		start_color();
		for (i = 1; i < NUM_COLOR_PAIRS; ++i)
			init_pair(i, color_pairs[i][0], color_pairs[i][1]);
	}

	getmaxyx(stdscr, row, col);
	statuswin = newwin(1, col, row-1, 0);
	repaint();

	while ((key = wgetch(stdscr)) != ERR) {
		switch (key) {
			case KEY_RESIZE:
				repaint();
				break;
		}
	}

	delwin(statuswin);
	endwin();

	return 0;
}
