#!/usr/bin/env python3
"""Topic wildcard matching: + (single level) and # (multi-level)."""
import sys, os, time, socket
sys.path.insert(0, os.path.dirname(__file__))
from mqtt_helpers import MQTTClient

port = int(sys.argv[1])

# --- # wildcard ---
with MQTTClient(port, 'wild-hash') as sub:
    sub.subscribe('test/#')

    with MQTTClient(port, 'wild-pub') as pub:
        pub.publish('test/foo', 'msg1')
        time.sleep(0.2)
        data = sub.recv()
        assert b'msg1' in data, f"# did not match test/foo: {data.hex()}"

        pub.publish('test/foo/bar/baz', 'msg2')
        time.sleep(0.2)
        data = sub.recv()
        assert b'msg2' in data, f"# did not match test/foo/bar/baz: {data.hex()}"

# --- + wildcard ---
with MQTTClient(port, 'wild-plus') as sub:
    sub.subscribe('a/+/c')

    with MQTTClient(port, 'wild-pub2') as pub:
        # Should match
        pub.publish('a/b/c', 'match')
        time.sleep(0.2)
        data = sub.recv()
        assert b'match' in data, f"+ did not match a/b/c: {data.hex()}"

        # Should NOT match (wrong last level)
        pub.publish('a/b/d', 'nomatch1')
        time.sleep(0.2)
        data = sub.recv(0.3)
        assert b'nomatch1' not in data, f"+ incorrectly matched a/b/d"

        # Should NOT match (extra level)
        pub.publish('a/b/c/d', 'nomatch2')
        time.sleep(0.2)
        data = sub.recv(0.3)
        assert b'nomatch2' not in data, f"+ incorrectly matched a/b/c/d"

# --- $ topic filtering ---
with MQTTClient(port, 'wild-dollar') as sub:
    sub.subscribe('#')

    with MQTTClient(port, 'wild-pub3') as pub:
        # Normal topic should match
        pub.publish('normal/topic', 'visible')
        time.sleep(0.2)
        data = sub.recv()
        assert b'visible' in data, f"# did not match normal topic"

        # $SYS topic should NOT match # at first level
        pub.publish('$SYS/info', 'hidden')
        time.sleep(0.2)
        data = sub.recv(0.3)
        assert b'hidden' not in data, f"# incorrectly matched $SYS topic"
