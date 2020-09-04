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
	struct buffer_ *next;
	struct buffer_ *prev;
	char *name;
	Line *lines;
	Cursor cursor;
	/* custom line formatting hook */
	void (*formatln)(struct buffer_*, char*);
} Buffer;

static void die(int);
static int numplaces(int);

static Buffer *allocbuf();
static Buffer *lastbuf();
static int newbuf();
static int readbuf();
static int numlines(Buffer*);

static void handleresize();
static void updatecursor();

static void paintbuffer(WINDOW*, Buffer*);
static void paintstatus();

static WINDOW *statuswin;
static Buffer *bufferlist;

void die(int err) {
	endwin();
	exit(err);
}

int numplaces(int n) {
	int r = 1;
	if (n < 0) n = (n == INT_MIN) ? INT_MAX: -n;
	while (n > 9) {
	    n /= 10;
	    r++;
	}
	return r;
}

Buffer *allocbuf() {
	return (Buffer*)calloc(0, sizeof(Buffer));
}

Buffer *lastbuf() {
	Buffer *cur, *ptr = bufferlist;
	do {
		cur = ptr;
	} while ((ptr = ptr->next));
	return cur;
}

int newbuf() {
	Buffer *last;
	last = lastbuf();
	if (!last) return 0;
	if (!(last->next = allocbuf())) return 0;
	last->next->prev = last;
	return 1;
}

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

	free(linecnt);
	fclose(fp);

	return 1;
}

int numlines(Buffer *buf) {
	Line *ln = buf->lines;
	int n = 0;
	for (;;) {
		ln = ln->next;
		if (!ln) return n;
		n++;
	}
	return -1;
}

void updatecursor() {
	//cursor.x = 0;
	//cursor.y = 10;
}

void paintstatus() {
	int row, col, bufsize;
	char textbuf[32];

	getmaxyx(stdscr, row, col);
	if (use_colors) wattron(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));
	/* background */
	whline(statuswin, ' ', col);
	/* cursor pos */
	bufsize = snprintf(textbuf, sizeof(textbuf), "%d:%d", 0, 0/*cursor.y, cursor.x*/);
	mvwprintw(statuswin, 0, col - bufsize, "%s", textbuf);
	wrefresh(statuswin);
	if (use_colors) wattroff(statuswin, COLOR_PAIR(PAIR_STATUS_BAR));
}

void paintbuffer(WINDOW *win, Buffer *buf) {
	int i, l;
	int row, col;
	int nlines, xmargin;
	Line *ln;
	Cursor *cur = &buf->cursor;

	cur->startln = 0;

	getmaxyx(stdscr, row, col);
	nlines = numlines(buf);
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

void handleresize() {
	int row, col;

	clear();
	refresh();
	getmaxyx(stdscr, row, col);
	delwin(statuswin);
	statuswin = newwin(1, col, row-1, 0);
	paintstatus();
}

int main(int argc, char **argv) {
	int key, row, col;

	initscr();
	noecho();
	nonl();
	keypad(stdscr, TRUE);
	use_default_colors();

	if (!has_colors()) use_colors = FALSE;
	if (use_colors) {
		int i;
		start_color();
		for (i = 1; i < NUM_COLOR_PAIRS; i++)
			init_pair(i, color_pairs[i][0], color_pairs[i][1]);
	}

	refresh();
	getmaxyx(stdscr, row, col);
	statuswin = newwin(1, col, row-1, 0);
	paintstatus();

	bufferlist = allocbuf();
	readbuf(bufferlist, "mett.c");
	updatecursor();

	paintbuffer(stdscr, bufferlist);
	refresh();

	while ((key = wgetch(stdscr)) != ERR) {
		switch (key) {
			case 'J':
				die(0);
				break;
			case KEY_RESIZE:
				handleresize();
				break;
		}
	}

	delwin(statuswin);
	endwin();

	return 0;
}
