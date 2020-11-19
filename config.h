#define ESC 27
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
	{  COLOR_GREEN,     COLOR_BG },
	{  COLOR_YELLOW,    COLOR_BG },
};

const char manual_path[] = "readme.txt";

/* See mett.c for function declarations */
const Action buffer_actions[] = {
	/* Command      Shortcut       Function     Argument(s) */

	/* Movement */
	{  L"left",     L'h',          motion,      { .x = -1 } },
	{  L"down",     L'j',          motion,      { .y = +1 } },
	{  L"up",       L'k',          motion,      { .y = -1 } },
	{  L"right",    L'l',          motion,      { .x = +1 } },
	{  NULL,        KEY_LEFT,      motion,      { .x = -1 } },
	{  NULL,        KEY_DOWN,      motion,      { .y = +1 } },
	{  NULL,        KEY_UP,        motion,      { .y = -1 } },
	{  NULL,        KEY_RIGHT,     motion,      { .x = +1 } },
	{  NULL,        KEY_BACKSPACE, motion,      { .x = -1 } },
	{  NULL,        L'\n',         motion,      { .y = +1 } },
	{  NULL,        L' ',          motion,      { .x = +1 } },
	{  L"home",     KEY_HOME,      motion,      { .y = -(1<<30) } },
	{  L"end",      KEY_END,       motion,      { .y = +(1<<30) } },
	{  L"pgup",     KEY_PPAGE,     pgup,        {{ 0 }} },
	{  L"pgdown",   KEY_NPAGE,     pgdown,      {{ 0 }} },
	{  NULL,        L'0',          jump,        { .m = MARKER_START } },
	{  NULL,        L'&',          jump,        { .m = MARKER_MIDDLE } },
	{  NULL,        L'$',          jump,        { .m = MARKER_END } },
	{  L"coc",      L'C',          coc,         {{ 0 }} },

	/* Buffer management */
	{  L"nextb",    L'n',          bufsel,      { .i = +1 } },
	{  L"prevb",    L'p',          bufsel,      { .i = -1 } },
	{  L"bd",       0,             bufdel,      { .i = 0 } },

	/* Mode switching */
	{  NULL,        ESC,           setmode,     { .i = MODE_NORMAL } },
	{  NULL,        L'i',          setmode,     { .i = MODE_INSERT } },
	{  NULL,        L'v',          setmode,     { .i = MODE_SELECT } },
	{  NULL,        L':',          setmode,     { .i = MODE_COMMAND } },
	{  NULL,        KEY_IC,        setmode,     { .i = MODE_INSERT } },

	/* File I/O */
	{  L"write",    L'w',          save,        { .v = NULL } },
	{  L"manual",   L'?',          readfile,    { .v = (void*)manual_path } },
	{  L"help",     L'?',          readfile,    { .v = (void*)manual_path } },

	/* Buffer modification */
	{  L"bs",       L'a',          insert,      { .i = KEY_BACKSPACE } },
	{  L"del",      L'x',          insert,      { .i = KEY_DC } },
	{  L"delln",    L'z',          freeln,      {{ 0 }} },
	{  NULL,        KEY_DC,        insert,      { .i = KEY_DC } },
	{  NULL,        L'A',          append,      {{ 0 }} },
	{  NULL,        L'o',          newln,       {{ 0 }} },

	/* Misc */
	{  L"quit",     L'q',          quit,        {{ 0 }} },
	{  L"exit",     L'q',          quit,        {{ 0 }} },
	{  NULL,        KEY_MOUSE,     handlemouse, {{ 0 }} },
	{  L"repaint",  KEY_RESIZE,    repaint,     {{ 0 }} },
};

static bool use_colors = TRUE;
static bool line_numbers = TRUE;

/* Maximum line size in bytes until reallocation */
const unsigned default_linebuf_size = 128*4;

const int tab_width = 2;

/* Copy buffer to backup_path before overwriting file */
const bool backup_on_write = TRUE;
const char *backup_path = "/tmp/.mett-backup";

const int cmd_history_size = 16;
const int max_cmd_repetition = 65536;

/* Filetype-formatting hooks */
const Formatter formatters[] = {
	/* Filename */
	{ "*.c", mformat_c },
};
