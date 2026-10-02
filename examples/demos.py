# Demos: the picovector demos from Pimoroni's Tufty 2350, redrawn for the
# PSP's 480x272 screen. Up and Down change demo, START finishes.
#
# Each demo is a module in demos/ with an update(ticks) function, called once
# a frame on a cleared screen with ticks = milliseconds since it started.
import gc
import os
import sys
import time

import psp
from picovector import color, font, image
from pspdisplay import screen, update, WIDTH, HEIGHT

FOLDER = "demos"
MENU_FONT = font.load("fonts/sins.ppf")
ROW = 12
SELECTED_Y = HEIGHT - 30
HINT = "Up/Down demo  START finishes"


def display_name(name):
    # The PSP lists short lowercase names in upper case (PIXELATE.PY).
    return name.lower() if name.isupper() else name


names = sorted(
    (n[:-3] for n in os.listdir(FOLDER) if n.lower().endswith(".py") and not n.startswith((".", "_"))),
    key=lambda n: n.lower(),
)
demo = None
selected = 0
started = 0


def unload():
    global demo
    if demo is not None:
        del sys.modules[FOLDER + "/" + names[selected]]
        demo = None
    gc.collect()


def load(index):
    global demo, selected, started
    unload()
    selected = index % len(names)
    demo = __import__(FOLDER + "/" + names[selected])
    started = time.ticks_ms()
    print("loaded {} ({} KB free)".format(display_name(names[selected]), gc.mem_free() // 1024))


def draw_menu(menu_index):
    # The selected demo near the bottom, the others fading out above it.
    screen.font = MENU_FONT
    for i, name in enumerate(names):
        name = display_name(name)
        y = SELECTED_Y + (i - menu_index) * ROW
        if y < -ROW or y > HEIGHT:
            continue
        alpha = 255 if i == selected else int(abs(y) * 40 / HEIGHT)
        w, h = screen.measure_text(name)
        screen.pen = color.rgb(20, 40, 60, alpha)
        screen.rectangle(3, y + 2, w + 4, h - 2)
        screen.pen = color.rgb(255, 255, 255, alpha)
        screen.text(name, 5, y)
    w, h = screen.measure_text(HINT)
    screen.pen = color.rgb(255, 255, 255, 90)
    screen.text(HINT, WIDTH - w - 5, 2)


def main():
    load(0)
    menu_index = 0.0
    try:
        while True:
            pressed = psp.pressed()
            if psp.START in pressed:
                break
            if psp.DOWN in pressed:
                load(selected + 1)
            elif psp.UP in pressed:
                load(selected - 1)

            screen.antialias = image.OFF
            screen.fill_rule = image.EVEN_ODD
            screen.alpha = 255
            screen.pen = color.rgb(0, 0, 0)
            screen.clear()
            screen.pen = color.rgb(255, 255, 255)
            screen.font = MENU_FONT
            demo.update(time.ticks_diff(time.ticks_ms(), started))

            screen.antialias = image.OFF
            menu_index += (selected - menu_index) / 20
            draw_menu(menu_index)
            update()
    finally:
        unload()


main()
