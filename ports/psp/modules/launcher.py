# The script launcher: runs when the EBOOT's folder has no main.py.
#
# Lists the .py files in the folder (except boot.py and main.py). Up and down
# choose, X runs, triangle shows the start of the file, HOME exits. After a
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
    # Newly pressed buttons, with auto-repeat while up or down is held.
    def __init__(self):
        self.held = 0
        self.repeat_at = 0

    def next(self):
        while True:
            buttons = ui.buttons()
            pressed = buttons & ~self.held
            now = time.ticks_ms()
            arrows = buttons & (ui.UP | ui.DOWN)
            if pressed & arrows:
                self.repeat_at = time.ticks_add(now, REPEAT_DELAY_MS)
            elif arrows and time.ticks_diff(now, self.repeat_at) >= 0:
                pressed |= arrows
                self.repeat_at = time.ticks_add(now, REPEAT_RATE_MS)
            self.held = buttons
            if pressed:
                return pressed


def show_file(name):
    ui.fill(0, 0, SCREEN_WIDTH, 272, 0x000000)
    header(display_name(name)[:30])
    try:
        with open(name) as f:
            lines = f.read(4096).split("\n")
    except Exception as e:
        lines = ["Can't read the file: " + repr(e)]
    for i, line in enumerate(lines[:28]):
        ui.text(8, 24 + i * 8, line.replace("\t", "    ")[: COLUMNS - 2], PREVIEW_TEXT)
    footer("O back")
    wait_press(ui.CIRCLE)


def restore_cwd(cwd):
    # getcwd() gives "umd0:" for the top of a drive, but chdir() needs
    # "umd0:/", so add the slash back.
    if os.getcwd() != cwd:
        os.chdir(cwd + "/" if cwd.endswith(":") else cwd)


def run_script(name):
    wait_release()
    ui.reset_buttons()
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
        sys.print_exception(e)
    finally:
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
        if pressed & ui.UP and selected > 0:
            selected -= 1
        elif pressed & ui.DOWN and selected < len(scripts) - 1:
            selected += 1
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
