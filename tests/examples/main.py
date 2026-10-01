# Example check: runs every script in examples/ headless. Run with the
# examples copied in beside this file (as tools/make-release.sh and CI do):
#   mkdir -p build/examples-test && cp tests/examples/main.py examples/*.py build/examples-test/
#   cp -R examples/fonts build/examples-test/
#   tools/run-ppsspp.sh --files build/examples-test build/psp/EBOOT.PBP 60
#
# Headless PPSSPP has no buttons, so a stand-in psp module passes everything
# through to the real one, except that pressed() "presses" X on its 5th call
# and START from its 30th, which ends each interactive example.
# Each example prints "examplestest: <name>: ok" or "... FAIL <detail>",
# then a summary line that CI greps for.
import os
import sys

import _launcher
import psp as real_psp

_failures = 0


class FakePsp:
    def __init__(self):
        self.calls = 0

    def __getattr__(self, name):
        return getattr(real_psp, name)

    def pressed(self):
        self.calls += 1
        if self.calls == 5:
            return [real_psp.CROSS]
        if self.calls >= 30:
            return [real_psp.START]
        return []


def run(name):
    global _failures
    sys.modules["psp"] = FakePsp()
    try:
        with open(name) as f:
            exec(compile(f.read(), name, "exec"), {"__name__": "__main__"})
        ok, detail = True, ""
    except SystemExit:
        ok, detail = True, ""
    except Exception as e:
        ok, detail = False, "{}: {}".format(type(e).__name__, e)
    finally:
        del sys.modules["psp"]
        # As the launcher does after each script.
        _launcher.release_display()
    if ok:
        print("examplestest: {}: ok".format(name))
    else:
        _failures += 1
        print("examplestest: {}: FAIL {}".format(name, detail))


names = sorted(n for n in os.listdir() if n.lower().endswith(".py") and n.lower() != "main.py")
for name in names:
    run(name)
ok = _failures == 0 and len(names) > 0
print("examplestest: {} examples".format(len(names)))
print("examplestest: PASS" if ok else "examplestest: FAIL ({} failed)".format(_failures))
