// Functions shared between the PSP port's own C files.
#ifndef MICROPY_INCLUDED_PSP_PSP_PORT_H
#define MICROPY_INCLUDED_PSP_PSP_PORT_H

// Each module's part of putting things back after a script.
void psp_audio_reset(void);     // stop every sound (modaudio.c)
void psp_buttons_reset(void);   // fresh pressed()/released() state, default dead zone (modpsp.c)
void psp_network_reset(void);   // disconnect Wi-Fi (modnetwork.c)

// Undo what a script left behind: take the screen back from pspdisplay (its
// last frame stays showing), stop sounds, disconnect Wi-Fi and reset the
// button state. The launcher calls it after each script, and the REPL before
// a soft reset frees the heap these may still point into.
void psp_end_script(void);

// The text console on the screen (psp_console.c): 68 columns by 34 rows.
#include <stddef.h>
#include <stdint.h>

#define PSP_CONSOLE_COLUMNS 68
#define PSP_CONSOLE_ROWS 34

// Sets up the display and shows a clear console.
void psp_console_init(void);
// Back to a clear, white-on-black console with the cursor at the top left.
void psp_console_reset(void);
// Text and ANSI codes, as print() writes them.
void psp_console_write(const char *str, size_t len);
// One 8x8 character at pixel (x, y) in a 0xAABBGGRR colour, with no
// background, for the launcher's own drawing.
void psp_console_draw_char(int x, int y, uint32_t pixel, uint8_t ch);
// For the tests: a cell's character and colours (0xRRGGBB), and the cursor.
void psp_console_cell(int col, int row, char *ch, uint32_t *fg, uint32_t *bg);
void psp_console_cursor(int *col, int *row);

#endif // MICROPY_INCLUDED_PSP_PSP_PORT_H
