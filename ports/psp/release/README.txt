MicroPython for the Sony PSP
============================

MicroPython (a small Python 3) that runs on a PSP with custom firmware,
such as ARK-4, and in the PPSSPP emulator.

Installing
----------
Copy the PSP folder from this zip onto the memory stick, merging it with
the PSP folder already there. You should end up with:

    PSP/GAME/MicroPython/EBOOT.PBP

Then pick MicroPython under Game > Memory Stick.

Running scripts
---------------
Put .py files in PSP/GAME/MicroPython/, next to EBOOT.PBP. Starting
MicroPython shows them in a list:

    Up/Down   choose a script
    X         run it
    Triangle  show its source
    O         back to the list
    HOME      quit

If there's a main.py in the folder, MicroPython runs that instead of the
list (after boot.py, if there is one). Modules in a lib/ folder can be
imported.

The scripts that come with this zip are examples: hello.py, buttons.py,
stick.py, dice.py, clock.py, notes.py and tasks.py. Read them, change them,
or delete them.

The psp module
--------------
    import psp
    psp.held()            buttons held now, e.g. ['CROSS', 'UP']
    psp.pressed()         buttons pressed since the last call
    psp.released()        buttons released since the last call
    psp.CROSS             button names: UP DOWN LEFT RIGHT CROSS CIRCLE
                          TRIANGLE SQUARE L R START SELECT
    psp.analog()          stick position (x, y), -127..127
    psp.set_deadzone(n)   ignore stick movement within n of centre (16)
    psp.vsync()           wait for the next screen refresh (60 Hz)
    psp.battery()         battery percent, or None
    psp.battery_minutes() minutes left, or None
    psp.charging()        True while charging
    psp.on_ac()           True on mains power
    psp.freq()            (cpu, bus) clock in MHz; psp.freq(222) sets it
    psp.emulator()        True in PPSSPP
    psp.VERSION           this release's version

Most standard modules are there too: os, time, json, re, random, math,
struct, collections, asyncio and more. Output is text on screen; there's
no graphics module yet.

Licence
-------
MicroPython is MIT-licensed; see LICENSE-MicroPython.txt.
