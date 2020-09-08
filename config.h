/*
 * Edit this file to change settings and recompile
 * afterwards. Note that some combinations may be
 * unstable and can make the editor unusable.
 *
 * Feel free to add your own function definitions here!
 */

#define ESC 27

enum ColorPair {
	PAIR_STATUS_BAR = 1,
	PAIR_STATUS_HIGHLIGHT,
	PAIR_LINE_NUMBERS,
	NUM_COLOR_PAIRS
};

const short color_pairs[NUM_COLOR_PAIRS][2] = {
	/* Foreground       Background */
	{  0,               0 },
	{  COLOR_YELLOW,    COLOR_BLACK },
	{  COLOR_GREEN,     COLOR_BLACK },
	{  COLOR_YELLOW,    COLOR_BLACK },
};

/* See mett.c for function declarations */
const Action buffer_actions[] = {
	/* Key         Function    Argument */
	{  'h',        motion,     { .x = -1 } },
	{  'j',        motion,     { .y = +1 } },
	{  'k',        motion,     { .y = -1 } },
	{  'l',        motion,     { .x = +1 } },
	{  ESC,        setmode,    { .i = MODE_NORMAL } },
	{  'i',        setmode,    { .i = MODE_WRITE } },
	{  'c',        setmode,    { .i = MODE_COMMAND } },
	{  's',        write,      { .path = NULL } },
	{  KEY_RESIZE, repaint,    { 0 } },
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

/* Copy buffer to backup_path before overwriting file */
const bool backup_on_write = TRUE;
const char *backup_path = "/tmp/.mett-backup";
