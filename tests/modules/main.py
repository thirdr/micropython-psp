# Standard-module check: run with tools/run-ppsspp.sh --files tests/modules <EBOOT>.
# Each check prints "modtest: <name>: ok" or "... FAIL <detail>", then a
# summary line that CI greps for.
import sys

_failures = 0


def _result(name, ok, detail=""):
    global _failures
    if ok:
        print("modtest: {}: ok".format(name))
    else:
        _failures += 1
        print("modtest: {}: FAIL {}".format(name, detail))


def _check(name, fn):
    try:
        ok, detail = fn()
    except Exception as e:
        ok, detail = False, "{}: {}".format(type(e).__name__, e)
    _result(name, ok, detail)


def t_json():
    import json

    s = json.dumps({"a": [1, 2.5, None, True], "b": "x"})
    back = json.loads(s)
    return back == {"a": [1, 2.5, None, True], "b": "x"}, s


def t_struct():
    import struct

    b = struct.pack("<HIf", 1, 0x12345678, 1.5)
    return struct.unpack("<HIf", b) == (1, 0x12345678, 1.5) and len(b) == 10, repr(b)


def t_math():
    import math

    ok = abs(math.sqrt(2) - 1.41421) < 1e-4 and abs(math.sin(math.pi / 2) - 1) < 1e-5
    return ok and math.floor(-1.5) == -2 and math.isnan(math.nan), ""


def t_random():
    import random

    random.seed(42)
    a = [random.randint(0, 1000) for _ in range(5)]
    random.seed(42)
    b = [random.randint(0, 1000) for _ in range(5)]
    c = random.choice("abc")
    return a == b and c in "abc" and 0 <= random.random() < 1, repr(a)


def t_re():
    import re

    m = re.match(r"(\w+)-(\d+)", "psp-1000")
    return m is not None and m.group(1) == "psp" and m.group(2) == "1000", ""


def t_collections():
    from collections import OrderedDict, deque, namedtuple

    P = namedtuple("P", ("x", "y"))
    d = deque((), 4)
    d.append(1)
    d.append(2)
    od = OrderedDict([("z", 1), ("a", 2)])
    return P(1, 2).y == 2 and d.popleft() == 1 and list(od.keys()) == ["z", "a"], ""


def t_binascii_hashlib():
    import binascii
    import hashlib

    h = binascii.hexlify(hashlib.sha256(b"abc").digest())
    want = b"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    return h == want and binascii.a2b_base64(b"cHNw") == b"psp", h


def t_heapq():
    import heapq

    h = []
    for x in (5, 1, 4, 2):
        heapq.heappush(h, x)
    return [heapq.heappop(h) for _ in range(4)] == [1, 2, 4, 5], ""


def t_io():
    import io

    s = io.StringIO()
    s.write("hi")
    b = io.BytesIO(b"xyz")
    return s.getvalue() == "hi" and b.read(2) == b"xy", ""


def t_deflate():
    import deflate
    import io

    data = b"psp " * 50
    buf = io.BytesIO()
    with deflate.DeflateIO(buf, deflate.ZLIB) as f:
        f.write(data)
    comp = buf.getvalue()
    out = deflate.DeflateIO(io.BytesIO(comp), deflate.ZLIB).read()
    return out == data and len(comp) < len(data), "{} bytes".format(len(comp))


def t_errno_select():
    import errno
    import select

    return errno.ENOENT == 2 and hasattr(select, "poll"), ""


def t_time():
    import time

    now = time.time()
    t = time.localtime()
    ok = now > 1577836800  # after 2020-01-01: the RTC is real, not uptime
    ok = ok and 2020 <= t[0] < 2100 and 1 <= t[1] <= 12
    ok = ok and time.gmtime(0)[:6] == (1970, 1, 1, 0, 0, 0)
    ok = ok and time.mktime((2000, 1, 1, 0, 0, 0, 0, 0)) == 946684800
    t0 = time.ticks_ms()
    time.sleep_ms(20)
    ok = ok and time.ticks_diff(time.ticks_ms(), t0) >= 20
    return ok, "time={} localtime={}".format(now, t)


def t_builtins():
    ok = "{:08.3f}".format(3.14159) == "0003.142" and f"{2 + 2}" == "4"
    ok = ok and bytearray(b"ab").hex() == "6162" and int.from_bytes(b"\x01\x00", "little") == 1
    ok = ok and sorted({3, 1, 2}) == [1, 2, 3] and 2**100 == 1267650600228229401496703205376
    return ok, ""


for name, fn in (
    ("json", t_json),
    ("struct", t_struct),
    ("math", t_math),
    ("random", t_random),
    ("re", t_re),
    ("collections", t_collections),
    ("binascii+hashlib", t_binascii_hashlib),
    ("heapq", t_heapq),
    ("io", t_io),
    ("deflate", t_deflate),
    ("errno+select", t_errno_select),
    ("time", t_time),
    ("builtins", t_builtins),
):
    _check(name, fn)

print("modtest: platform", sys.platform, sys.implementation)
print("modtest: PASS" if _failures == 0 else "modtest: FAIL ({} failed)".format(_failures))
