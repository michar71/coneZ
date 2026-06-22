#!/usr/bin/env python3
"""conez-masterctl — control a running conez-master.py over its Unix control socket.

Usage:
    ./conez-masterctl.py <command>

Commands:
    status              show TX state, current manifest, radio params
    reload              re-read the config file + re-apply radio + reload the manifest
    enable | on         enable LoRa transmission
    disable | off       disable LoRa transmission (master stays up, keeps RX)
    stop                stop the master process
    help                list commands

Socket path: $CONEZ_MASTER_SOCKET or /tmp/conez-master.sock
"""
import socket
import sys
import os

SOCK = os.environ.get("CONEZ_MASTER_SOCKET", "/tmp/conez-master.sock")


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        sys.stderr.write(__doc__)
        sys.exit(0 if len(sys.argv) >= 2 else 2)
    cmd = " ".join(sys.argv[1:])
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(100)   # `reload` re-encodes the firmware (~20 s); allow margin
        s.connect(SOCK)
        s.sendall((cmd + "\n").encode())
        resp = s.recv(4096).decode(errors="replace").rstrip("\n")
        s.close()
    except (ConnectionRefusedError, FileNotFoundError):
        sys.stderr.write(f"conez-masterctl: master not running (no socket at {SOCK})\n")
        sys.exit(1)
    except socket.timeout:
        sys.stderr.write("conez-masterctl: timed out waiting for the master\n")
        sys.exit(1)
    except OSError as e:
        sys.stderr.write(f"conez-masterctl: {e}\n")
        sys.exit(1)
    print(resp)
    # Non-zero exit if the master reported an error.
    sys.exit(1 if resp.startswith("ERROR") else 0)


if __name__ == "__main__":
    main()
