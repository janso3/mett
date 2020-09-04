enum ColorPair {
	PAIR_STATUS_BAR = 1,
	PAIR_LINE_NUMBERS,
	NUM_COLOR_PAIRS
};

const short color_pairs[NUM_COLOR_PAIRS][2] = {
	/* Foreground      background */
	{ 0,               0 },
	{ COLOR_YELLOW,    COLOR_BLACK },
	{ COLOR_YELLOW,    COLOR_BLACK },
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
