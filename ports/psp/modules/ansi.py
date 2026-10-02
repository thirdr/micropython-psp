# ansi: colours and cursor moves for print(), as ANSI codes.
#
#   import ansi
#   ansi.clear()
#   ansi.move(10, 5)
#   print(ansi.RED + "Game over" + ansi.RESET)
#
# The console on the PSP's screen acts on these, and so does a terminal on a
# computer during a REPL session over USB. Columns and rows count from 0, as
# elsewhere in the port; the console has COLUMNS x ROWS of them.

COLUMNS = 68
ROWS = 34

RESET = "\x1b[0m"
BRIGHT = "\x1b[1m"
REVERSE = "\x1b[7m"

BLACK = "\x1b[30m"
RED = "\x1b[31m"
GREEN = "\x1b[32m"
YELLOW = "\x1b[33m"
BLUE = "\x1b[34m"
MAGENTA = "\x1b[35m"
CYAN = "\x1b[36m"
WHITE = "\x1b[37m"

BRIGHT_BLACK = "\x1b[90m"
BRIGHT_RED = "\x1b[91m"
BRIGHT_GREEN = "\x1b[92m"
BRIGHT_YELLOW = "\x1b[93m"
BRIGHT_BLUE = "\x1b[94m"
BRIGHT_MAGENTA = "\x1b[95m"
BRIGHT_CYAN = "\x1b[96m"
BRIGHT_WHITE = "\x1b[97m"

BG_BLACK = "\x1b[40m"
BG_RED = "\x1b[41m"
BG_GREEN = "\x1b[42m"
BG_YELLOW = "\x1b[43m"
BG_BLUE = "\x1b[44m"
BG_MAGENTA = "\x1b[45m"
BG_CYAN = "\x1b[46m"
BG_WHITE = "\x1b[47m"


def clear():
    # The whole screen, with the cursor back at the top left.
    print("\x1b[2J\x1b[H", end="")


def clear_line():
    # The rest of the line, from the cursor.
    print("\x1b[K", end="")


def move(col, row):
    print("\x1b[{};{}H".format(row + 1, col + 1), end="")
