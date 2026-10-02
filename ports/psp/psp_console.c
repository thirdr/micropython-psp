// The text console on the PSP's screen, where print() goes.
//
// 68 columns by 34 rows of 7x8 characters, drawn straight into the start of
// VRAM in the 8x8 font from pspsdk's libpspdebug. Text wraps at the right
// edge and the screen scrolls up at the bottom. It acts on the ANSI codes
// that most programs use, so print() can clear the screen, place text and
// colour it, on the PSP as in a terminal on a computer:
//
//   ESC[2J ESC[J ESC[1J     clear the screen, from the cursor, or up to it
//   ESC[K ESC[1K ESC[2K     clear the line, likewise
//   ESC[row;colH  ESC[nG    move the cursor (1 is the first row or column)
//   ESC[nA B C D            move up, down, right, left
//   ESC[s ESC[u ESC7 ESC8   save and restore the cursor
//   ESC[...m                0 reset, 1 bright, 7 reverse, 30-37 and 90-97
//                           foreground, 40-47 and 100-107 background,
//                           39 and 49 the defaults, 38;5;n and 48;5;n for
//                           n under 16
//   ESCc                    reset the console
//
// Other codes and control characters are dropped. A copy of the characters
// and colours on screen lets the tests check what's there.
#include <stdbool.h>
#include <string.h>

#include <pspdisplay.h>
#include <pspdmac.h>
#include <pspge.h>

#include "psp_port.h"

#define COLUMNS PSP_CONSOLE_COLUMNS
#define ROWS PSP_CONSOLE_ROWS
#define CELL_WIDTH 7
#define CELL_HEIGHT 8
// The display reads rows 512 pixels apart.
#define VRAM_STRIDE 512
#define CELL_ROW_BYTES (VRAM_STRIDE * CELL_HEIGHT * 4)

// White on black, as the screen was before colours.
#define DEFAULT_FG 15
#define DEFAULT_BG 0

// libpspdebug's font: 8 bytes a character, top row first, the leftmost pixel
// in the top bit. Printable ASCII never uses the 8th column, so a 7 pixel
// cell loses nothing.
extern unsigned char msx[];

// The 16 ANSI colours as xterm draws them, 0xRRGGBB: 0-7 normal, 8-15
// bright.
static const uint32_t palette[16] = {
    0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd, 0xe5e5e5,
    0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff,
};

// What's on screen: each cell's character, and its colours as palette
// indexes, the foreground in the low 4 bits.
static uint8_t chars[ROWS][COLUMNS];
static uint8_t attrs[ROWS][COLUMNS];

static int col, row, saved_col, saved_row;
// Set after writing in the last column. The wrap waits for the next
// character, as in a terminal, so a full line then "\n" leaves no blank one.
static bool wrap_pending;
static uint8_t fg, bg;
static bool bright, reverse;

// Parsing ANSI codes, which can arrive split over several writes.
#define MAX_PARAMS 8
static enum { TEXT, ESC, CSI } state;
static int params[MAX_PARAMS], n_params;
static bool ignore_code;
// Continuation bytes still to skip in a UTF-8 character.
static int utf8_skip;

// The start of VRAM, where the console draws: through the uncached mirror
// for the CPU, so writes reach the display without a cache flush.
static uint8_t *vram(void) {
    return (uint8_t *)sceGeEdramGetAddr();
}

static uint32_t *vram_uncached(void) {
    return (uint32_t *)(0x40000000 | (uint32_t)vram());
}

// 0xRRGGBB to the framebuffer's 8888 layout, 0xAABBGGRR.
static uint32_t to_pixel(uint32_t rgb) {
    return 0xff000000 | ((rgb & 0xff) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 0xff);
}

static void draw_cell(int c, int r) {
    const uint8_t *glyph = &msx[chars[r][c] * 8];
    uint32_t on = to_pixel(palette[attrs[r][c] & 15]);
    uint32_t off = to_pixel(palette[attrs[r][c] >> 4]);
    uint32_t *p = vram_uncached() + r * CELL_HEIGHT * VRAM_STRIDE + c * CELL_WIDTH;
    for (int y = 0; y < CELL_HEIGHT; y++, p += VRAM_STRIDE) {
        uint8_t bits = glyph[y];
        for (int x = 0; x < CELL_WIDTH; x++) {
            p[x] = (bits & (0x80 >> x)) ? on : off;
        }
    }
}

static uint8_t current_attr(void) {
    uint8_t f = (bright && fg < 8) ? fg + 8 : fg;
    return reverse ? (f << 4) | bg : (bg << 4) | f;
}

// Clears cells from..to-1 of a row, in the current background colour.
static void clear_cells(int r, int from, int to) {
    uint8_t attr = (bg << 4) | DEFAULT_FG;
    for (int c = from; c < to; c++) {
        chars[r][c] = ' ';
        attrs[r][c] = attr;
        draw_cell(c, r);
    }
}

// Everything up a row, with the bottom one cleared. The rows go up one at a
// time, so no copy overlaps the one it reads from.
static void scroll(void) {
    for (int r = 0; r < ROWS - 1; r++) {
        sceDmacMemcpy(vram() + r * CELL_ROW_BYTES, vram() + (r + 1) * CELL_ROW_BYTES, CELL_ROW_BYTES);
    }
    memmove(chars[0], chars[1], sizeof(chars) - sizeof(chars[0]));
    memmove(attrs[0], attrs[1], sizeof(attrs) - sizeof(attrs[0]));
    clear_cells(ROWS - 1, 0, COLUMNS);
}

static void line_feed(void) {
    if (row == ROWS - 1) {
        scroll();
    } else {
        row++;
    }
}

static void move_to(int c, int r) {
    col = c < 0 ? 0 : c >= COLUMNS ? COLUMNS - 1 : c;
    row = r < 0 ? 0 : r >= ROWS ? ROWS - 1 : r;
    wrap_pending = false;
}

static void put_char(uint8_t ch) {
    if (wrap_pending) {
        col = 0;
        line_feed();
        wrap_pending = false;
    }
    chars[row][col] = ch;
    attrs[row][col] = current_attr();
    draw_cell(col, row);
    if (col == COLUMNS - 1) {
        wrap_pending = true;
    } else {
        col++;
    }
}

static void reset_colours(void) {
    fg = DEFAULT_FG;
    bg = DEFAULT_BG;
    bright = false;
    reverse = false;
}

void psp_console_reset(void) {
    reset_colours();
    for (int r = 0; r < ROWS; r++) {
        clear_cells(r, 0, COLUMNS);
    }
    move_to(0, 0);
    saved_col = saved_row = 0;
    state = TEXT;
    utf8_skip = 0;
}

void psp_console_init(void) {
    sceDisplaySetMode(0, 480, 272);
    sceDisplaySetFrameBuf(vram(), VRAM_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
    psp_console_reset();
}

static int param(int i, int otherwise) {
    return (i < n_params && params[i] > 0) ? params[i] : otherwise;
}

static void set_colours(void) {
    if (n_params == 0) {
        reset_colours();
        return;
    }
    for (int i = 0; i < n_params; i++) {
        int p = params[i];
        if (p == 0) {
            reset_colours();
        } else if (p == 1) {
            bright = true;
        } else if (p == 22) {
            bright = false;
        } else if (p == 7) {
            reverse = true;
        } else if (p == 27) {
            reverse = false;
        } else if (p >= 30 && p <= 37) {
            fg = p - 30;
        } else if (p == 39) {
            fg = DEFAULT_FG;
        } else if (p >= 40 && p <= 47) {
            bg = p - 40;
        } else if (p == 49) {
            bg = DEFAULT_BG;
        } else if (p >= 90 && p <= 97) {
            fg = p - 90 + 8;
        } else if (p >= 100 && p <= 107) {
            bg = p - 100 + 8;
        } else if (p == 38 || p == 48) {
            // 256 colours (5;n) or RGB (2;r;g;b): the first 16 of the 256
            // are ours, and the rest are skipped over.
            if (i + 2 < n_params && params[i + 1] == 5) {
                if (params[i + 2] < 16) {
                    if (p == 38) {
                        fg = params[i + 2];
                    } else {
                        bg = params[i + 2];
                    }
                }
                i += 2;
            } else if (i + 1 < n_params && params[i + 1] == 2) {
                i += 4;
            }
        }
    }
}

static void do_code(char final) {
    switch (final) {
        case 'A':
            move_to(col, row - param(0, 1));
            break;
        case 'B':
            move_to(col, row + param(0, 1));
            break;
        case 'C':
            move_to(col + param(0, 1), row);
            break;
        case 'D':
            move_to(col - param(0, 1), row);
            break;
        case 'G':
            move_to(param(0, 1) - 1, row);
            break;
        case 'H':
        case 'f':
            move_to(param(1, 1) - 1, param(0, 1) - 1);
            break;
        case 'J': {
            int mode = n_params > 0 ? params[0] : 0;
            int first = mode == 0 ? row + 1 : 0;
            int last = mode == 1 ? row : ROWS;
            if (mode == 0) {
                clear_cells(row, col, COLUMNS);
            } else if (mode == 1) {
                clear_cells(row, 0, col + 1);
            }
            for (int r = first; r < last; r++) {
                clear_cells(r, 0, COLUMNS);
            }
            break;
        }
        case 'K': {
            int mode = n_params > 0 ? params[0] : 0;
            clear_cells(row, mode == 0 ? col : 0, mode == 1 ? col + 1 : COLUMNS);
            break;
        }
        case 'm':
            set_colours();
            break;
        case 's':
            saved_col = col;
            saved_row = row;
            break;
        case 'u':
            move_to(saved_col, saved_row);
            break;
    }
}

static void write_byte(uint8_t ch) {
    if (state == ESC) {
        state = TEXT;
        if (ch == '[') {
            state = CSI;
            n_params = 0;
            params[0] = 0;
            ignore_code = false;
        } else if (ch == '7') {
            saved_col = col;
            saved_row = row;
        } else if (ch == '8') {
            move_to(saved_col, saved_row);
        } else if (ch == 'c') {
            psp_console_reset();
        }
        return;
    }
    if (state == CSI) {
        if (ch >= '0' && ch <= '9') {
            if (n_params == 0) {
                n_params = 1;
            }
            int *p = &params[n_params - 1];
            if (*p < 10000) {
                *p = *p * 10 + (ch - '0');
            }
        } else if (ch == ';') {
            if (n_params == 0) {
                n_params = 1;
            }
            if (n_params < MAX_PARAMS) {
                params[n_params++] = 0;
            }
        } else if (ch >= 0x40 && ch <= 0x7e) {
            if (!ignore_code) {
                do_code(ch);
            }
            state = TEXT;
        } else {
            // A private code (ESC[?25l, which hides the cursor) or one with
            // intermediate characters: none of ours.
            ignore_code = true;
        }
        return;
    }
    // UTF-8: one cell a character, which the font can only show as "?" past
    // ASCII.
    if (utf8_skip > 0 && (ch & 0xc0) == 0x80) {
        utf8_skip--;
        return;
    }
    utf8_skip = 0;
    if (ch >= 0x20 && ch < 0x7f) {
        put_char(ch);
    } else if (ch >= 0xc0) {
        utf8_skip = ch >= 0xf0 ? 3 : ch >= 0xe0 ? 2 : 1;
        put_char('?');
    } else if (ch == '\n') {
        col = 0;
        wrap_pending = false;
        line_feed();
    } else if (ch == '\r') {
        move_to(0, row);
    } else if (ch == '\b') {
        move_to(col - 1, row);
    } else if (ch == '\t') {
        move_to((col / 8 + 1) * 8, row);
    } else if (ch == 0x1b) {
        state = ESC;
    }
}

void psp_console_write(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        write_byte((uint8_t)str[i]);
    }
}

void psp_console_draw_char(int x, int y, uint32_t pixel, uint8_t ch) {
    const uint8_t *glyph = &msx[ch * 8];
    uint32_t *p = vram_uncached() + y * VRAM_STRIDE + x;
    for (int gy = 0; gy < 8; gy++, p += VRAM_STRIDE) {
        for (int gx = 0; gx < 8; gx++) {
            if (glyph[gy] & (0x80 >> gx)) {
                p[gx] = pixel;
            }
        }
    }
}

void psp_console_cell(int c, int r, char *ch, uint32_t *fg_rgb, uint32_t *bg_rgb) {
    *ch = chars[r][c];
    *fg_rgb = palette[attrs[r][c] & 15];
    *bg_rgb = palette[attrs[r][c] >> 4];
}

void psp_console_cursor(int *c, int *r) {
    *c = col;
    *r = row;
}
