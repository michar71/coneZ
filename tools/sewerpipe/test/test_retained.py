#!/usr/bin/env python3
"""Retained message store and delete."""
import sys, os, time
sys.path.insert(0, os.path.dirname(__file__))
from mqtt_helpers import MQTTClient

port = int(sys.argv[1])

# Publish a retained message
with MQTTClient(port, 'ret-pub') as pub:
    pub.publish('ret/topic', 'retained payload', retain=True)
    time.sleep(0.1)

    # New subscriber should receive retained message on subscribe
    with MQTTClient(port, 'ret-sub1') as sub1:
        data = sub1.subscribe('ret/topic')
        # SUBACK + retained PUBLISH should both be in the response
        assert b'retained payload' in data, \
            f"retained message not in subscribe response: {data.hex()}"

    # Delete retained message (empty payload + retain flag)
    pub.publish('ret/topic', '', retain=True)
    time.sleep(0.1)

    # New subscriber should NOT receive it
    with MQTTClient(port, 'ret-sub2') as sub2:
        data = sub2.subscribe('ret/topic')
        assert b'retained payload' not in data, \
            f"deleted retained message still delivered: {data.hex()}"
