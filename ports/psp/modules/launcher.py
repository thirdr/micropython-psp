# The script launcher: runs when the EBOOT's folder has no main.py.
#
# Lists the .py files in the folder (except boot.py and main.py). Up and down
# choose, wrapping round at the ends, X runs, triangle shows the file (up and down scroll, left and right
# turn a page), HOME exits. After a
# script ends, or raises an error, its output stays on screen until O.
#
# Under PPSSPPHeadless, which has no buttons, it draws the list once, logs
# it, and returns.
import gc
import os
import sys
import time

import _launcher as ui

SCREEN_WIDTH = 480
COLUMNS = 60
LIST_TOP = 32
ROW_PITCH = 16
VISIBLE_ROWS = 13
FOOTER_Y = 256

HEADER_BG = 0x1F4FA8
HEADER_TEXT = 0xFFFFFF
HEADER_NOTE = 0xCFE0FF
SELECTED_BG = 0x2B2B2B
SELECTED_TEXT = 0xFFD34D
ROW_TEXT = 0xDDDDDD
FOOTER_BG = 0x1A1A1A
FOOTER_TEXT = 0xAAAAAA
PREVIEW_TEXT = 0x999999

REPEAT_DELAY_MS = 400
REPEAT_RATE_MS = 80

FILE_TOP = 24
FILE_ROWS = 28
FILE_MAX_BYTES = 64 * 1024

VERSION = sys.version.split("; ")[1].split(" on ")[0]


def find_scripts():
    scripts = []
    for name in os.listdir():
        lower = name.lower()
        # Skip macOS's "._name" metadata files, which it leaves on FAT cards.
        if not lower.endswith(".py") or lower.startswith(".") or lower in ("boot.py", "main.py"):
            continue
        if os.stat(name)[0] & 0x4000:
            continue
        scripts.append(name)
    scripts.sort(key=lambda n: n.lower())
    return scripts


def display_name(name):
    # The PSP lists short lowercase names in upper case (MAIN.PY), so show
    # an all-capitals name in lower case.
    return name.lower() if name.isupper() else name


def header(right):
    ui.fill(0, 0, SCREEN_WIDTH, 16, HEADER_BG)
    ui.text(8, 4, VERSION, HEADER_TEXT)
    ui.text(SCREEN_WIDTH - 8 - 8 * len(right), 4, right, HEADER_NOTE)


def footer(words):
    ui.fill(0, FOOTER_Y, SCREEN_WIDTH, 16, FOOTER_BG)
    ui.text(8, FOOTER_Y + 4, words, FOOTER_TEXT)


def draw_list(scripts, selected, top):
    ui.fill(0, 16, SCREEN_WIDTH, FOOTER_Y - 16, 0x000000)
    count = "1 script" if len(scripts) == 1 else "{} scripts".format(len(scripts))
    header(count)
    if not scripts:
        ui.text(8, LIST_TOP + 2, "No .py files in " + os.getcwd(), ROW_TEXT)
        ui.text(8, LIST_TOP + 18, "Copy scripts next to EBOOT.PBP.", ROW_TEXT)
        footer("HOME exit")
        return
    for row in range(VISIBLE_ROWS):
        index = top + row
        if index >= len(scripts):
            break
        y = LIST_TOP + row * ROW_PITCH
        name = display_name(scripts[index])[: COLUMNS - 3]
        if index == selected:
            ui.fill(0, y, SCREEN_WIDTH, 12, SELECTED_BG)
            ui.text(8, y + 2, "> " + name, SELECTED_TEXT)
        else:
            ui.text(8, y + 2, "  " + name, ROW_TEXT)
    footer("X run   /\\ show file   HOME exit")


def wait_release():
    while ui.buttons():
        pass


def wait_press(mask):
    wait_release()
    while not ui.buttons() & mask:
        pass
    wait_release()


class Input:
    # Newly pressed buttons, with auto-repeat while a direction is held.
    def __init__(self, repeat=ui.UP | ui.DOWN):
        self.held = 0
        self.repeat_at = 0
        self.repeat = repeat

    def next(self):
        while True:
            buttons = ui.buttons()
            pressed = buttons & ~self.held
            now = time.ticks_ms()
            arrows = buttons & self.repeat
            if pressed & arrows:
                self.repeat_at = time.ticks_add(now, REPEAT_DELAY_MS)
            elif arrows and time.ticks_diff(now, self.repeat_at) >= 0:
                pressed |= arrows
                self.repeat_at = time.ticks_add(now, REPEAT_RATE_MS)
            self.held = buttons
            if pressed:
                return pressed


def read_lines(name):
    try:
        with open(name) as f:
            text = f.read(FILE_MAX_BYTES)
            more = f.read(1)
    except Exception as e:
        return ["Can't read the file: " + repr(e)]
    lines = text.replace("\t", "    ").split("\n")
    if more:
        lines.append("(only the first {} KB shown)".format(FILE_MAX_BYTES // 1024))
    return lines


def show_file(name):
    # Returns the top line shown last, for the tests.
    lines = read_lines(name)
    last_top = max(len(lines) - FILE_ROWS, 0)
    top = 0
    keys = Input(ui.UP | ui.DOWN | ui.LEFT | ui.RIGHT)
    wait_release()
    ui.fill(0, 0, SCREEN_WIDTH, 272, 0x000000)
    while True:
        header(display_name(name)[:30])
        ui.fill(0, 16, SCREEN_WIDTH, FOOTER_Y - 16, 0x000000)
        for i, line in enumerate(lines[top : top + FILE_ROWS]):
            ui.text(8, FILE_TOP + i * 8, line[: COLUMNS - 2], PREVIEW_TEXT)
        shown = "{}-{} of {}".format(top + 1, min(top + FILE_ROWS, len(lines)), len(lines))
        keys_help = "^v scroll  <> page  O back"
        footer(keys_help + " " * (COLUMNS - 2 - len(keys_help) - len(shown)) + shown)
        pressed = keys.next()
        if pressed & ui.CIRCLE:
            break
        if pressed & ui.UP:
            top -= 1
        elif pressed & ui.DOWN:
            top += 1
        elif pressed & ui.LEFT:
            top -= FILE_ROWS
        elif pressed & ui.RIGHT:
            top += FILE_ROWS
        top = min(max(top, 0), last_top)
    wait_release()
    return top


def restore_cwd(cwd):
    # getcwd() gives "umd0:" for the top of a drive, but chdir() needs
    # "umd0:/", so add the slash back.
    if os.getcwd() != cwd:
        os.chdir(cwd + "/" if cwd.endswith(":") else cwd)


def run_script(name):
    wait_release()
    ui.console()
    cwd = os.getcwd()
    scope = {"__name__": "__main__", "__file__": name}
    try:
        with open(name) as f:
            source = f.read()
        exec(compile(source, name, "exec"), scope)
    except SystemExit:
        pass
    except BaseException as e:
        # Back from pspdisplay first, so the error shows on screen.
        ui.release_display()
        sys.print_exception(e)
    finally:
        ui.end_script()
        restore_cwd(cwd)
    del scope
    gc.collect()
    footer("O back")
    wait_press(ui.CIRCLE)


def main():
    scripts = find_scripts()
    selected = 0
    top = 0
    draw_list(scripts, selected, top)
    if ui.headless():
        ui.log("launcher: {} scripts: {}".format(len(scripts), ", ".join(display_name(s) for s in scripts)))
        return
    keys = Input()
    while True:
        pressed = keys.next()
        if not scripts:
            continue
        if pressed & ui.UP:
            selected = (selected - 1) % len(scripts)
        elif pressed & ui.DOWN:
            selected = (selected + 1) % len(scripts)
        elif pressed & ui.CROSS:
            run_script(scripts[selected])
            scripts = find_scripts()
            selected = min(selected, max(len(scripts) - 1, 0))
            ui.fill(0, 0, SCREEN_WIDTH, 272, 0x000000)
        elif pressed & ui.TRIANGLE:
            show_file(scripts[selected])
        else:
            continue
        top = min(max(top, selected - VISIBLE_ROWS + 1), selected)
        draw_list(scripts, selected, top)


# Run as __main__ by the port; tests import it and drive it instead.
if __name__ == "__main__":
    main()
