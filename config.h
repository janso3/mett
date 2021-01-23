#define VERSION_STRING ">Mett v0.1\n"
#define ESC 27
#define CTRL(x) ((x) & 0x1F)
#define COLOR_BG -1

enum ColorPair {
	PAIR_STATUS_BAR = 1,
	PAIR_STATUS_HIGHLIGHT,
	PAIR_LINE_NUMBERS,
	PAIR_BUFFER_CONTENTS,
	NUM_COLOR_PAIRS
};

const short color_pairs[NUM_COLOR_PAIRS][2] = {
	/* Foreground       Background */
	{  0,               0 },
	{  COLOR_YELLOW,    COLOR_BG },
	{  COLOR_GREEN,     COLOR_BG },
	{  COLOR_YELLOW,    COLOR_BG },
	{  0,               COLOR_WHITE }
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
	{  NULL,        CTRL('d'),     motion,      { .y = +20 } },
	{  NULL,        CTRL('u'),     motion,      { .y = -20 } },
	{  L"left",     KEY_LEFT,      motion,      { .x = -1 } },
	{  L"down",     KEY_DOWN,      motion,      { .y = +1 } },
	{  L"up",       KEY_UP,        motion,      { .y = -1 } },
	{  L"right",    KEY_RIGHT,     motion,      { .x = +1 } },
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
	{  L"bn",       CTRL('n'),     bufsel,      { .i = +1 } },
	{  L"bp",       CTRL('p'),     bufsel,      { .i = -1 } },
	{  L"bd",       CTRL('x'),     bufdel,      { .i = 0 } },
	{  L"cls",      0,             cls,         {{ 0 }} },
	{  L"edit",     L'e',          readfile,    {{ 0 }} },
	{  L"read",     L'r',          readstr,     {{ 0 }} },
	{  L"find",     L'f',          find,        {{ 0 }} },
	{  L"lsb",      0,             listbuffers, {{ 0 }} },

	/* Mode switching */
	{  NULL,        ESC,           setmode,     { .i = MODE_NORMAL } },
	{  NULL,        L'i',          setmode,     { .i = MODE_INSERT } },
	{  NULL,        L'v',          setmode,     { .i = MODE_SELECT } },
	{  NULL,        L':',          setmode,     { .i = MODE_COMMAND } },
	{  NULL,        KEY_IC,        setmode,     { .i = MODE_INSERT } },

	/* File I/O */
	{  L"write",    CTRL('w'),     save,        { .v = NULL } },
	{  L"manual",   L'?',          readfile,    { .v = (void*)manual_path } },
	{  L"help",     L'?',          readfile,    { .v = (void*)manual_path } },

	/* Buffer modification */
	{  L"bs",       0,             insert,      { .i = KEY_BACKSPACE } },
	{  L"del",      'x',           insert,      { .i = KEY_DC } },
	{  L"delln",    'Z',           freeln,      {{ 0 }} },
	{  L"del",      KEY_DC,        insert,      { .i = KEY_DC } },
	{  L"append",   L'A',          append,      {{ 0 }} },
	{  L"newln",    L'o',          newln,       {{ 0 }} },

	/* Misc */
	{  L"print",    L'p',          print,       {{ 0 }} },
	{  L"about",    0,             print,       { .v = (void*)VERSION_STRING } },
	{  L"quit",     L'q',          quit,        {{ 0 }} },
	{  L"exit",     0,             quit,        {{ 0 }} },
	{  NULL,        KEY_MOUSE,     handlemouse, {{ 0 }} },
	{  L"resize",   KEY_RESIZE,    resize,      {{ 0 }} },
};

static bool use_colors = true;
static bool line_numbers = true;
static bool auto_indent = true;

/* Always have the cursor at the center of the screen */
static bool always_centered = false;

/* Maximum line size in bytes until reallocation */
static const unsigned default_linebuf_size = 128*4;

static const unsigned tab_width = 4;

/* These control the tab visualisation */
static const wchar_t tab_beginning = L'→';
static const wchar_t tab_character = L' ';

/* Copy buffer to backup_path before overwriting file */
static const bool backup_on_write = true;
static const char *backup_path = "/tmp/.mett-backup";

/* Maximum number of times a command can be repeated */
static const unsigned max_cmd_repetition = 65536;
