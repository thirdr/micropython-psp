# Launcher check: run with tools/run-ppsspp.sh --files tests/launcher-run <EBOOT>.
#
# Headless PPSSPP has no buttons, so this drives launcher.py with a stand-in
# for _launcher whose buttons() "presses" O once each time it's waited on.
# Each check prints "launchertest: <name>: ok" or "... FAIL <detail>", then a
# summary line that CI greps for.
import _launcher
import launcher

_failures = 0


def _result(name, ok, detail=""):
    global _failures
    if ok:
        _launcher.log("launchertest: {}: ok".format(name))
    else:
        _failures += 1
        _launcher.log("launchertest: {}: FAIL {}".format(name, detail))


class PressO:
    # Draws with the real module; buttons() reads released, then O, then
    # released again, which is what wait_press(CIRCLE) waits for.
    def __init__(self):
        self.sequence = []
        self.waits = 0

    def __getattr__(self, name):
        return getattr(_launcher, name)

    def buttons(self):
        if not self.sequence:
            self.waits += 1
            self.sequence = [0, _launcher.CIRCLE, 0]
        return self.sequence.pop(0)


fake = PressO()
launcher.ui = fake

scripts = launcher.find_scripts()
names = [launcher.display_name(s) for s in scripts]
_result("lists .py files, not main.py", names == ["error_demo.py", "hello.py"], repr(names))

launcher.run_script(scripts[names.index("hello.py")])
_result("runs a script and waits for O", fake.waits >= 1)

launcher.run_script(scripts[names.index("error_demo.py")])
_result("survives a script that raises", True)

import os
start = os.getcwd()
os.mkdir("sub")
with open("chdir_demo.py", "w") as f:
    f.write("import os\nos.chdir('sub')\n")
launcher.run_script("chdir_demo.py")
_result("restores the working directory", os.getcwd() == start, repr(os.getcwd()))
os.remove("chdir_demo.py")
os.rmdir("sub")

# A pspdisplay script: two frames, so the second ends up in VRAM buffer 1
# and has to be copied back. print() mustn't reach the screen meanwhile.
FRAME_1, FRAME_2 = 0x204080, 0x80C040
with open("gfx_demo.py", "w") as f:
    f.write(
        "from picovector import color\n"
        "from pspdisplay import screen, update\n"
        "for rgb in ({:#x}, {:#x}):\n"
        "    screen.pen = color.rgb(rgb >> 16, (rgb >> 8) & 255, rgb & 255)\n"
        "    screen.clear()\n"
        "    print('#' * 40)\n"
        "    update()\n".format(FRAME_1, FRAME_2))
launcher.run_script("gfx_demo.py")
os.remove("gfx_demo.py")
top = [_launcher.screen_pixel(x, y) for y in range(0, 16) for x in range(0, 320, 7)]
_result("pspdisplay frame stays after the script", all(p == FRAME_2 for p in top),
        "{:06x}".format([p for p in top if p != FRAME_2][0]) if any(p != FRAME_2 for p in top) else "")
_result("launcher draws again after pspdisplay", _launcher.screen_pixel(479, launcher.FOOTER_Y) == launcher.FOOTER_BG,
        "{:06x}".format(_launcher.screen_pixel(479, launcher.FOOTER_Y)))

# A pspdisplay script that raises: the error shows on screen.
with open("gfx_error.py", "w") as f:
    f.write(
        "from picovector import color\n"
        "from pspdisplay import screen, update\n"
        "screen.pen = color.rgb(0, 0, 255)\n"
        "screen.clear()\n"
        "update()\n"
        "raise ValueError('boom')\n")
launcher.run_script("gfx_error.py")
os.remove("gfx_error.py")
row = [_launcher.screen_pixel(x, y) for y in range(0, 40) for x in range(0, 480)]
_result("pspdisplay error shows on screen", any(p == 0xFFFFFF for p in row))

launcher.show_file(scripts[names.index("hello.py")])
_result("shows a file and waits for O", fake.waits >= 3, "waits={}".format(fake.waits))

launcher.draw_list(scripts, 1, 0)
_launcher.log("launchertest: {}".format("PASS" if _failures == 0 else "FAIL ({} failed)".format(_failures)))
