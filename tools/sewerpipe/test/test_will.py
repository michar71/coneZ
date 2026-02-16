#!/usr/bin/env python3
"""Will messages: published on unexpected disconnect, suppressed on clean disconnect."""
import sys, os, time, socket
sys.path.insert(0, os.path.dirname(__file__))
from mqtt_helpers import MQTTClient, mqtt_connect, mqtt_disconnect

port = int(sys.argv[1])

# --- Test 1: Will published on TCP close (no DISCONNECT) ---

# Subscriber listens on the will topic
with MQTTClient(port, 'will-sub') as sub:
    sub.subscribe('client/status')

    # Client connects with a will, then drops TCP without DISCONNECT
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('127.0.0.1', port))
    s.settimeout(2)
    s.send(mqtt_connect('will-client', will_topic='client/status',
                        will_msg='offline'))
    connack = s.recv(64)
    assert connack[0] == 0x20 and connack[3] == 0

    # Abrupt close — broker should publish will
    s.close()
    time.sleep(0.3)

    data = sub.recv(timeout=1)
    assert b'offline' in data, \
        f"will message not received after TCP drop: {data.hex()}"

# --- Test 2: Will NOT published on clean DISCONNECT ---

with MQTTClient(port, 'will-sub2') as sub:
    sub.subscribe('client/status2')

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('127.0.0.1', port))
    s.settimeout(2)
    s.send(mqtt_connect('will-client2', will_topic='client/status2',
                        will_msg='gone'))
    connack = s.recv(64)
    assert connack[0] == 0x20 and connack[3] == 0

    # Clean disconnect — will should be suppressed
    s.send(mqtt_disconnect())
    s.close()
    time.sleep(0.3)

    data = sub.recv(timeout=0.5)
    assert b'gone' not in data, \
        f"will message should NOT be sent on clean disconnect: {data.hex()}"
