# Built into test builds only (MICROPY_PSP_TEST_BUILD). Runs when there's no
# main.py. Each check prints "selftest: <name>: ok" or "... FAIL <detail>",
# then a summary line that CI greps for.
import gc
import sys

_failures = 0


def _result(name, ok, detail=""):
    global _failures
    if ok:
        print("selftest: {}: ok".format(name))
    else:
        _failures += 1
        print("selftest: {}: FAIL {}".format(name, detail))


def arithmetic():
    print(1 + 1)
    _result("arithmetic", 1 + 1 == 2 and 7 // 2 == 3 and 2**100 > 2**99)


def floats():
    x = 1.5 * 2
    _result("float", x == 3.0 and abs(0.1 + 0.2 - 0.3) < 1e-6, repr(x))


def exceptions():
    try:
        raise ValueError("boom")
    except ValueError as e:
        _result("exception", str(e) == "boom")


def _churn():
    # Allocate lots of short-lived objects while keeping a few live ones, so
    # a root-scanning bug shows up as a corrupted survivor or a crash.
    keep = []
    for i in range(2000):
        junk = [str(j) * 8 for j in range(20)]
        if i % 100 == 0:
            keep.append((i, "s" * i, junk))
    gc.collect()
    for k in keep:
        if k[0] != len(k[1]) or len(k[2]) != 20:
            return False
    return True


def gc_stress():
    # Run the churn in its own function, so its objects are unreachable once
    # it returns. The GC is conservative, so stale pointers can keep a little
    # garbage alive; the check is that free memory doesn't keep shrinking.
    free = []
    intact = True
    for _ in range(3):
        intact = _churn() and intact
        gc.collect()
        free.append(gc.mem_free())
    recovered = free[-1] >= free[0] - 4096
    _result("gc", intact and recovered, "intact={} free={}".format(intact, free))


def run():
    print("Hello from MicroPython on", sys.platform)
    print(sys.implementation.name, sys.version)
    arithmetic()
    floats()
    exceptions()
    gc_stress()
    print("selftest: {}".format("PASS" if _failures == 0 else "FAIL ({} failed)".format(_failures)))


if __name__ == "__main__":
    run()
