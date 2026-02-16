#!/usr/bin/env python3
"""Basic MQTT connect, subscribe, publish, ping, disconnect."""
import sys, os, time
sys.path.insert(0, os.path.dirname(__file__))
from mqtt_helpers import MQTTClient

port = int(sys.argv[1])

with MQTTClient(port, 'basic-sub') as sub:
    sub.subscribe('test/topic')

    with MQTTClient(port, 'basic-pub') as pub:
        pub.publish('test/topic', 'hello world')
        time.sleep(0.2)

        data = sub.recv()
        assert b'hello world' in data, f"message not received: {data.hex()}"

        # PINGREQ / PINGRESP
        sub.ping()
