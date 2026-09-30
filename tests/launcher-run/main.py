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

launcher.show_file(scripts[names.index("hello.py")])
_result("shows a file and waits for O", fake.waits >= 3, "waits={}".format(fake.waits))

launcher.draw_list(scripts, 1, 0)
_launcher.log("launchertest: {}".format("PASS" if _failures == 0 else "FAIL ({} failed)".format(_failures)))
