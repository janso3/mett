#define ESC 27
#define DEL 127
#define LF '\n'
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
	/* Command      Shortcut      Function    Argument(s) */
	{  L"left",     L'h',         motion,     { .x = -1 } },
	{  L"down",     L'j',         motion,     { .y = +1 } },
	{  L"up",       L'k',         motion,     { .y = -1 } },
	{  L"right",    L'l',         motion,     { .x = +1 } },
	{  L"nextb",    L'x',         bufsel,     { .i = +1 } },
	{  L"prevb",    L'y',         bufsel,     { .i = -1 } },
	{  NULL,       	ESC,          setmode,    { .i = MODE_NORMAL } },
	{  NULL,       	L'i',         setmode,    { .i = MODE_WRITE } },
	{  NULL,       	L':',         setmode,    { .i = MODE_COMMAND } },
	{  NULL,       	L'v',         setmode,    { .i = MODE_SELECT } },
	{  L"write",    L'w',         save,       { .v = NULL } },
	{  L"bs",       L'a',         insert,     { .i = KEY_BACKSPACE } },
	{  L"del",      L's',         insert,     { .i = DEL } },
	{  L"ret",      L'n',         insert,     { .i = '\n' } },
	{  L"manual",   L'?',         readfile,   { .v = (void*)manual_path } },
	{  L"help",     L'?',         readfile,   { .v = (void*)manual_path } },
	{  L"quit",     L'q',         quit,       {{ 0 }} },
	{  L"exit",     L'q',         quit,       {{ 0 }} },
	{  L"repaint",  KEY_RESIZE,   repaint,    {{ 0 }} },
};

static bool use_colors = TRUE;
static bool line_numbers = TRUE;

/* Maximum line size in bytes until reallocation */
const unsigned default_linebuf_size = 256;

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
