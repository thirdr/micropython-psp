#!/usr/bin/env python3
# Join the PSP's stdin and stdout, which usbhostfs_pc serves on a TCP port
# under PSPLINK, to a pseudo-terminal, so tools that want a serial port can
# use it. mpremote needs this: its REPL waits on the port's file descriptor,
# which its socket:// connections don't have.
#
# Usage: tools/psp-tty.py [--port 10002] [--link /tmp/psp-tty]
#
# Waits for the port, then keeps the terminal open at the link path until
# the PSP side closes, so mpremote can connect and disconnect as often as it
# likes. Uses only the Python standard library.
import argparse
import os
import pty
import select
import signal
import socket
import sys
import time
import tty


def connect(port):
    while True:
        try:
            return socket.create_connection(("localhost", port))
        except OSError:
            time.sleep(0.2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=10002)
    parser.add_argument("--link", default="/tmp/psp-tty")
    args = parser.parse_args()

    # Stopped with kill (as tools/psp-repl.sh does): clean up the same way.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

    sock = connect(args.port)

    # Keep our own handle on the terminal's far end, so the near end doesn't
    # see end-of-file each time a client closes it.
    master, slave = pty.openpty()
    tty.setraw(slave)
    if os.path.lexists(args.link):
        os.remove(args.link)
    os.symlink(os.ttyname(slave), args.link)
    print("psp-tty: {} -> localhost:{}".format(args.link, args.port), flush=True)

    try:
        while True:
            ready, _, _ = select.select([master, sock], [], [])
            if sock in ready:
                data = sock.recv(4096)
                if not data:
                    break
                os.write(master, data)
            if master in ready:
                sock.sendall(os.read(master, 4096))
    except KeyboardInterrupt:
        pass
    finally:
        os.remove(args.link)
    return 0


if __name__ == "__main__":
    sys.exit(main())
