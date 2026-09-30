# Filesystem check: run with tools/run-ppsspp.sh --files tests/fs <EBOOT>.
# Each check prints "fstest: <name>: ok" or "... FAIL <detail>", then a
# summary line that CI greps for.
import os
import sys

import fsmod

_failures = 0


def _result(name, ok, detail=""):
    global _failures
    if ok:
        print("fstest: {}: ok".format(name))
    else:
        _failures += 1
        print("fstest: {}: FAIL {}".format(name, detail))


def _raises_enoent(fn):
    try:
        fn()
    except OSError as e:
        return e.errno == 2
    return False


print("fstest: cwd", os.getcwd())
_result("boot.py ran first", fsmod.boot_ran)
_result("import from lib/", "lib" in sys.path and fsmod.__name__ == "fsmod", repr(sys.path))

# The PSP's FAT driver reports lowercase 8.3 names in upper case (main.py
# lists as MAIN.PY), and PPSSPP copies that, so compare without case. Opening
# a file works with either case.
names = [n.lower() for n in os.listdir()]
_result("listdir", "main.py" in names and "lib" in names and "eboot.pbp" in names, repr(names))

with open("written.txt", "w") as f:
    f.write("line 1\n")
with open("written.txt", "a") as f:
    f.write("line 2\n")
with open("written.txt") as f:
    text = f.read()
_result("write, append, read", text == "line 1\nline 2\n", repr(text))
_result("stat size", os.stat("written.txt")[6] == len(text), repr(os.stat("written.txt")))

with open("bytes.bin", "wb") as f:
    f.write(bytes(range(256)))
with open("bytes.bin", "rb") as f:
    data = f.read()
_result("binary round trip", data == bytes(range(256)), "len={}".format(len(data)))

# sceIoRename can't move a file to another folder: it ignores any folder in
# the new name. So rename within one folder only.
os.mkdir("subdir")
with open("subdir/a.bin", "wb") as f:
    f.write(data)
os.rename("subdir/a.bin", "subdir/b.bin")
listing = [n.lower() for n in os.listdir("subdir")]
_result("mkdir and rename", listing == ["b.bin"], repr(listing))
os.remove("subdir/b.bin")
os.remove("bytes.bin")
os.rmdir("subdir")
_result("remove and rmdir", "subdir" not in [n.lower() for n in os.listdir()])

_result("missing file raises ENOENT", _raises_enoent(lambda: open("no-such-file.txt")))

print("fstest: {}".format("PASS" if _failures == 0 else "FAIL ({} failed)".format(_failures)))
