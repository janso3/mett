#define ESC 27
#define DEL 127
#define COLOR_BG -1

enum ColorPair {
	PAIR_STATUS_BAR = 1,
	PAIR_STATUS_HIGHLIGHT,
	PAIR_LINE_NUMBERS,
	NUM_COLOR_PAIRS
};

const short color_pairs[NUM_COLOR_PAIRS][2] = {
	/* Foreground       Background */
	{  0,               0 },
	{  COLOR_YELLOW,    COLOR_BG },
	{  COLOR_GREEN,     COLOR_BLACK },
	{  COLOR_YELLOW,    COLOR_BG },
};

const char manual_path[] = "readme.txt";

/*
 * See mett.c for function declarations.
 * Because of how we abstract arguments, you may have to
 * create wrappers for functions which take any parameters
 * other than Action*.
 */
const Action buffer_actions[] = {
	/* Command     Shortcut      Function    Argument(s) */
	{  "left",     'h',          motion,     { .x = -1 } },
	{  "down",     'j',          motion,     { .y = +1 } },
	{  "up",       'k',          motion,     { .y = -1 } },
	{  "right",    'l',          motion,     { .x = +1 } },
	{  NULL,       ESC,          setmode,    { .i = MODE_NORMAL } },
	{  NULL,       'i',          setmode,    { .i = MODE_WRITE } },
	{  NULL,       'c',          setmode,    { .i = MODE_COMMAND } },
	{  "save",     's',          save,       { .v = NULL } },
	{  "bs",       'y',          insert,     { .i = KEY_BACKSPACE } },
	{  "del",      'x',          insert,     { .i = DEL } },
	{  "manual",   '?',          readfile,   { .v = (void*)manual_path } },
	{  "help",     '?',          readfile,   { .v = (void*)manual_path } },
	{  "quit",     'q',          quit,       {{ 0 }} },
	{  "exit",     'q',          quit,       {{ 0 }} },
	{  "repaint",  KEY_RESIZE,   repaint,    {{ 0 }} },
};

static bool use_colors = TRUE;
static bool line_numbers = TRUE;

/*
 * When disabled, the cursor will stop at
 * the end of the line content. Otherwise,
 * you can move it freely about the buffer.
 */
static bool floating_cursor = TRUE;

/* Maximum line size in bytes until reallocation */
const unsigned default_linebuf_size = 256;

const int tab_width = 2;

/* Copy buffer to backup_path before overwriting file */
const bool backup_on_write = TRUE;
const char *backup_path = "/tmp/.mett-backup";

const int cmd_history_size = 16;
const int max_cmd_repetition = 65536;
