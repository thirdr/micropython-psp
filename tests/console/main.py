# Console check: run with tools/run-ppsspp.sh --files tests/console <EBOOT>.
# Prints to the console on screen, then reads back what it holds, cell by
# cell with _launcher.console_cell(), and some pixels with screen_pixel(),
# to check the scrolling, wrapping and ANSI codes. Results go to the log only
# (_launcher.log()), so they don't move the screen being checked. Each check
# logs "consoletest: <name>: ok" or "... FAIL <detail>", then a summary line
# that CI greps for.
import sys

import _launcher
import ansi

_failures = 0

WHITE = 0xFFFFFF
BLACK = 0x000000


def _result(name, ok, detail=""):
    global _failures
    if ok:
        _launcher.log("consoletest: {}: ok".format(name))
    else:
        _failures += 1
        _launcher.log("consoletest: {}: FAIL {}".format(name, detail))


def _check(name, fn):
    _launcher.console()
    try:
        ok, detail = fn()
    except Exception as e:
        ok, detail = False, "{}: {}".format(type(e).__name__, e)
    _result(name, ok, detail)


def row_text(row):
    return "".join(_launcher.console_cell(col, row)[0] for col in range(ansi.COLUMNS)).rstrip()


def screen_text():
    return [row_text(row) for row in range(ansi.ROWS)]


def t_reset():
    cells = set(_launcher.console_cell(c, r) for r in range(ansi.ROWS) for c in range(ansi.COLUMNS))
    cursor = _launcher.console_cursor()
    return cells == {(" ", WHITE, BLACK)} and cursor == (0, 0), repr((cells, cursor))


def t_scroll():
    # 40 lines on 34 rows: the first 7 scroll off the top, and the cursor
    # waits on the blank bottom row.
    for i in range(40):
        print("line", i)
    want = ["line {}".format(i) for i in range(7, 40)] + [""]
    got = screen_text()
    cursor = _launcher.console_cursor()
    return got == want and cursor == (0, 33), repr((got[:2], got[-2:], cursor))


def t_scroll_pixels():
    # The scrolled-up rows are in VRAM too: "line 7" on row 0 has lit pixels,
    # and the bottom row, cleared, has none.
    for i in range(40):
        print("line", i)
    top = [_launcher.screen_pixel(x, y) for y in range(8) for x in range(7 * 6)]
    bottom = [_launcher.screen_pixel(x, y) for y in range(264, 272) for x in range(480)]
    return WHITE in top and set(bottom) == {BLACK}, repr((set(top), set(bottom)))


def t_wrap():
    # A full line then "\n" leaves no blank line; a longer one wraps.
    print("x" * 68)
    print("y" * 70)
    got = screen_text()[:4]
    want = ["x" * 68, "y" * 68, "yy", ""]
    return got == want and _launcher.console_cursor() == (0, 3), repr(got)


def t_move():
    ansi.move(9, 4)
    print("hi", end="")
    got = (_launcher.console_cell(9, 4)[0], _launcher.console_cell(10, 4)[0])
    cursor = _launcher.console_cursor()
    print("\x1b[H", end="")
    home = _launcher.console_cursor()
    return got == ("h", "i") and cursor == (11, 4) and home == (0, 0), repr((got, cursor, home))


def t_relative():
    # Up, down, right and left, stopping at the edges.
    ansi.move(5, 5)
    print("\x1b[2A\x1b[3C", end="")
    a = _launcher.console_cursor()
    print("\x1b[B\x1b[D", end="")
    b = _launcher.console_cursor()
    print("\x1b[99A\x1b[99D", end="")
    c = _launcher.console_cursor()
    print("\x1b[99B\x1b[99C", end="")
    d = _launcher.console_cursor()
    got = (a, b, c, d)
    return got == ((8, 3), (7, 4), (0, 0), (67, 33)), repr(got)


def t_save_restore():
    ansi.move(3, 7)
    print("\x1b[s", end="")
    ansi.move(20, 20)
    print("\x1b[u", end="")
    a = _launcher.console_cursor()
    print("\x1b7", end="")
    ansi.move(1, 1)
    print("\x1b8", end="")
    b = _launcher.console_cursor()
    return a == (3, 7) and b == (3, 7), repr((a, b))


def t_clear():
    for i in range(5):
        print("row", i)
    ansi.clear()
    blank = screen_text() == [""] * ansi.ROWS
    return blank and _launcher.console_cursor() == (0, 0), repr(screen_text()[:5])


def t_clear_parts():
    for i in range(3):
        print("abcdef")
    ansi.move(2, 1)
    print("\x1b[J", end="")
    after_j = screen_text()[:3]
    ansi.move(3, 0)
    print("\x1b[K", end="")
    after_k = row_text(0)
    print("\x1b[1K", end="")
    after_1k = row_text(0)
    print("abc\x1b[2K", end="")
    after_2k = row_text(0)
    got = (after_j, after_k, after_1k, after_2k)
    want = (["abcdef", "ab", ""], "abc", "", "")
    return got == want, repr(got)


def t_clear_above():
    for i in range(3):
        print("abcdef")
    ansi.move(2, 1)
    print("\x1b[1J", end="")
    got = screen_text()[:3]
    return got == ["", "   def", "abcdef"], repr(got)


def t_colours():
    print(ansi.RED + "r" + ansi.BG_BLUE + "b" + ansi.RESET + "n", end="")
    print(ansi.BRIGHT + ansi.RED + "R" + ansi.RESET, end="")
    print(ansi.BRIGHT_GREEN + ansi.BG_WHITE + "g" + ansi.RESET, end="")
    print(ansi.REVERSE + "v" + ansi.RESET, end="")
    print("\x1b[38;5;3m" + "y" + "\x1b[38;5;200m" + "z" + "\x1b[38;2;1;2;3;44m" + "w" + ansi.RESET, end="")
    got = [_launcher.console_cell(c, 0) for c in range(9)]
    want = [
        ("r", 0xCD0000, BLACK),
        ("b", 0xCD0000, 0x0000EE),
        ("n", WHITE, BLACK),
        ("R", 0xFF0000, BLACK),
        ("g", 0x00FF00, 0xE5E5E5),
        ("v", BLACK, WHITE),
        ("y", 0xCDCD00, BLACK),
        ("z", 0xCDCD00, BLACK),
        ("w", 0xCDCD00, 0x0000EE),
    ]
    return got == want, repr(got)


def t_colour_pixels():
    # The bottom pixel row of a letter is its background colour.
    print(ansi.BG_BLUE + "b" + ansi.RESET + "n", end="")
    got = (_launcher.screen_pixel(3, 7), _launcher.screen_pixel(10, 7))
    return got == (0x0000EE, BLACK), repr(got)


def t_clear_colour():
    # Clearing uses the background colour set at the time.
    print(ansi.BG_RED + "\x1b[2K" + ansi.RESET, end="")
    cells = set(_launcher.console_cell(c, 0) for c in range(ansi.COLUMNS))
    return cells == {(" ", WHITE, 0xCD0000)}, repr(cells)


def t_split_code():
    # A code written in pieces still works.
    sys.stdout.write("\x1b[3")
    sys.stdout.write("1;4")
    sys.stdout.write("4mX\x1b[0m")
    got = _launcher.console_cell(0, 0)
    return got == ("X", 0xCD0000, 0x0000EE), repr(got)


def t_dropped():
    # Codes the console doesn't know, and other control characters, leave no
    # trace; non-ASCII characters take one cell each, shown as "?".
    print("a\x1b[?25lb\x07c\x1b[5nd\x04e", end="")
    print("é€😀f", end="")
    got = row_text(0)
    return got == "abcde???f", repr(got)


def t_control():
    print("abc\bX", end="")
    a = row_text(0)
    print("\rY\tZ", end="")
    b = row_text(0)
    return a == "abX" and b == "YbX     Z", repr((a, b))


def t_reset_code():
    print(ansi.RED + "abc\x1bc", end="")
    print("d", end="")
    got = (row_text(0), _launcher.console_cell(0, 0))
    return got == ("d", ("d", WHITE, BLACK)), repr(got)


_check("reset", t_reset)
_check("scroll", t_scroll)
_check("scroll pixels", t_scroll_pixels)
_check("wrap", t_wrap)
_check("move", t_move)
_check("relative moves", t_relative)
_check("save and restore", t_save_restore)
_check("clear", t_clear)
_check("clear parts", t_clear_parts)
_check("clear above", t_clear_above)
_check("colours", t_colours)
_check("colour pixels", t_colour_pixels)
_check("clear colour", t_clear_colour)
_check("split code", t_split_code)
_check("dropped", t_dropped)
_check("control", t_control)
_check("reset code", t_reset_code)

# Something to see in the screenshot.
_launcher.console()
print(ansi.BRIGHT + "Console test" + ansi.RESET)
for i, colour in enumerate((ansi.RED, ansi.GREEN, ansi.YELLOW, ansi.BLUE, ansi.MAGENTA, ansi.CYAN)):
    print(colour + "colour {}".format(i) + ansi.RESET)
_launcher.log("consoletest: PASS" if _failures == 0 else "consoletest: FAIL ({} failed)".format(_failures))
