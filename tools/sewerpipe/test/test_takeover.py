#!/usr/bin/env python3
"""Duplicate client ID causes old connection to be closed."""
import sys, os, time, socket
sys.path.insert(0, os.path.dirname(__file__))
from mqtt_helpers import MQTTClient, mqtt_connect, mqtt_pingreq

port = int(sys.argv[1])

# Connect first client
s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s1.connect(('127.0.0.1', port))
s1.settimeout(2)
s1.send(mqtt_connect('dupe-id'))
connack1 = s1.recv(64)
assert connack1[0] == 0x20 and connack1[3] == 0

# Connect second client with same ID — should boot the first
with MQTTClient(port, 'dupe-id') as c2:
    time.sleep(0.2)

    # Old socket should be dead
    try:
        s1.settimeout(0.5)
        s1.send(mqtt_pingreq())
        time.sleep(0.1)
        r = s1.recv(64)
        # Empty recv = connection closed by peer
        assert len(r) == 0, f"old client still alive: {r.hex()}"
    except (ConnectionError, BrokenPipeError, OSError):
        pass  # expected — connection reset

s1.close()
