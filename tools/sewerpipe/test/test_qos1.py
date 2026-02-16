#!/usr/bin/env python3
"""QoS 1 publish, PUBACK, and delivery."""
import sys, os, time
sys.path.insert(0, os.path.dirname(__file__))
from mqtt_helpers import MQTTClient

port = int(sys.argv[1])

with MQTTClient(port, 'q1-sub') as sub:
    sub.subscribe('qos1/test', qos=1)

    with MQTTClient(port, 'q1-pub') as pub:
        # QoS 1 publish — publisher gets PUBACK
        pub.publish('qos1/test', 'important', qos=1)
        time.sleep(0.2)

        # Subscriber receives QoS 1 message
        data = sub.recv()
        assert b'important' in data, f"QoS 1 message not received: {data.hex()}"

        # Verify it's marked QoS 1 (bit 1 of first byte)
        assert (data[0] & 0x06) == 0x02, \
            f"received message not QoS 1: flags=0x{data[0]:02x}"
