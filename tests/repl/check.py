# Checks a REPL test transcript (build/logs/latest/stdout.log) for each step's
# output, in order. Prints "repltest: <step>: ok" or "... FAIL", then
# "repltest: PASS" or "repltest: FAIL" for CI to grep.
#   python3 tests/repl/check.py build/logs/latest/stdout.log
import sys

EXPECTED = [
    ("banner", "MicroPython v"),
    ("expression", ">>> 1+1\n2\n"),
    ("auto-indent block", "\n42\n"),
    ("backspace", "\n5\n"),
    ("Ctrl-C", "KeyboardInterrupt"),
    ("paste mode", "paste 0\npaste 1\npaste 2\n"),
    ("raw REPL", "raw REPL; CTRL-B to exit\n>"),
    ("raw REPL run", "OKraw 42\n\x04\x04>"),
    ("back from raw REPL", "MicroPython v"),
    ("raw REPL soft reset", "raw REPL; CTRL-B to exit\n>OK\nMPY: soft reboot\nraw REPL; CTRL-B to exit\n>"),
    ("run after it", "OKafter reset\n\x04\x04>"),
    ("soft reset", "MPY: soft reboot\n"),
    ("variables gone", "NameError: name 'x' isn't defined"),
    ("finished", "repl done"),
]

with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
    log = f.read().replace("\r", "")

failures = 0
at = 0
for name, text in EXPECTED:
    found = log.find(text, at)
    if found < 0:
        failures += 1
        print("repltest: {}: FAIL (no {!r} after offset {})".format(name, text, at))
    else:
        print("repltest: {}: ok".format(name))
        at = found + len(text)
print("repltest: PASS" if failures == 0 else "repltest: FAIL ({} failed)".format(failures))
sys.exit(1 if failures else 0)
