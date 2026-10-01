# network/socket/requests check: start tests/network/server.py on the host
# first, then run with tools/run-ppsspp.sh --files tests/network <EBOOT>.
# PPSSPP fakes the Wi-Fi connection (one saved profile, number 1) and its
# sockets are the host's, so 127.0.0.1 is the host. Each check prints
# "nettest: <name>: ok" or "... FAIL <detail>", then a summary line that CI
# greps for.
import errno
import select
import socket
import time

import network
import requests

_failures = 0
HOST = "127.0.0.1"


def _result(name, ok, detail=""):
    global _failures
    if ok:
        print("nettest: {}: ok".format(name))
    else:
        _failures += 1
        print("nettest: {}: FAIL {}".format(name, detail))


def _check(name, fn):
    try:
        ok, detail = fn()
    except Exception as e:
        ok, detail = False, "{}: {}".format(type(e).__name__, e)
    _result(name, ok, detail)


def _raises(exc, fn):
    try:
        fn()
    except exc as e:
        return True, str(e)
    return False, "no {}".format(exc.__name__)


wlan = network.WLAN()
_check("switch on", lambda: (wlan.active(), ""))
_check("profiles", lambda: (len(wlan.profiles()) >= 1 and wlan.profiles()[0][0] == 1, repr(wlan.profiles())))
_check("no such profile", lambda: _raises(ValueError, lambda: wlan.connect("no such profile")))


def connect():
    wlan.connect(wlan.profiles()[0][1])   # by name
    return wlan.isconnected() and wlan.status() == network.STAT_GOT_IP, repr((wlan.isconnected(), wlan.status()))
_check("connect by name", connect)


def ifconfig():
    ip, mask, gw, dns = wlan.ifconfig()
    return ip.count(".") == 3 and mask.count(".") == 3, repr(wlan.ifconfig())
_check("ifconfig", ifconfig)

ECHO = socket.getaddrinfo(HOST, 8766)[0][-1]
_check("getaddrinfo", lambda: (socket.getaddrinfo(HOST, 80)[0][0] == socket.AF_INET, repr(socket.getaddrinfo(HOST, 80))))
_check("inet_pton", lambda: (socket.inet_pton(socket.AF_INET, "10.1.2.3") == b"\x0a\x01\x02\x03", ""))


def echo():
    s = socket.socket()
    s.connect(ECHO)
    s.send(b"ping 123")
    got = s.recv(64)
    s.close()
    return got == b"ping 123", repr(got)
_check("tcp echo", echo)


def stream_echo():
    s = socket.socket()
    s.connect(ECHO)
    s.write(b"line one\n")
    got = s.readline()
    s.close()
    return got == b"line one\n", repr(got)
_check("socket as a stream", stream_echo)


def recv_timeout():
    s = socket.socket()
    s.connect(ECHO)
    s.settimeout(0.5)
    t = time.ticks_ms()
    try:
        s.recv(10)   # the echo server sends nothing until we do
        return False, "no timeout"
    except OSError as e:
        ms = time.ticks_diff(time.ticks_ms(), t)
        return e.errno in (errno.ETIMEDOUT, errno.EAGAIN) and 400 <= ms <= 1500, "{} after {} ms".format(e, ms)
    finally:
        s.close()
_check("recv timeout", recv_timeout)


def nonblocking():
    s = socket.socket()
    s.connect(ECHO)
    s.setblocking(False)
    try:
        s.recv(10)
        return False, "no EAGAIN"
    except OSError as e:
        return e.errno == errno.EAGAIN, str(e)
    finally:
        s.close()
_check("non-blocking recv", nonblocking)


def poll():
    s = socket.socket()
    s.connect(ECHO)
    p = select.poll()
    p.register(s, select.POLLIN)
    before = p.poll(0)
    s.send(b"x")
    after = p.poll(2000)
    s.close()
    return before == [] and len(after) == 1, repr((before, after))
_check("poll", poll)


def get():
    r = requests.get("http://{}:8765/hello".format(HOST))
    return (r.status_code, r.text) == (200, "hello from the host\n"), repr((r.status_code, r.text))
_check("requests.get", get)


def post_json():
    r = requests.post("http://{}:8765/echo".format(HOST), json={"psp": 1000})
    return r.json() == {"psp": 1000}, repr(r.text)
_check("requests.post json", post_json)
_check("404", lambda: (requests.get("http://{}:8765/missing".format(HOST)).status_code == 404, ""))


def disconnect():
    wlan.disconnect()
    for _ in range(50):
        if not wlan.isconnected():
            return True, ""
        time.sleep_ms(100)
    return False, repr(wlan.status())
_check("disconnect", disconnect)

print("nettest: {}".format("PASS" if _failures == 0 else "FAIL ({} failed)".format(_failures)))
