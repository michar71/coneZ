"""Minimal MQTT 3.1.1 packet builders for testing."""
import socket
import struct
import time

def mqtt_connect(client_id, keep_alive=60):
    proto = b'\x00\x04MQTT'
    level = bytes([4])
    flags = bytes([0x02])  # clean session
    ka = struct.pack('>H', keep_alive)
    cid = client_id.encode()
    cid_field = struct.pack('>H', len(cid)) + cid
    var_payload = proto + level + flags + ka + cid_field
    return bytes([0x10]) + encode_remaining(len(var_payload)) + var_payload

def mqtt_publish(topic, message, qos=0, retain=False, msg_id=1):
    tf = topic.encode()
    flags = 0
    if retain: flags |= 1
    if qos == 1: flags |= 2
    var_header = struct.pack('>H', len(tf)) + tf
    if qos > 0:
        var_header += struct.pack('>H', msg_id)
    payload = var_header + (message.encode() if isinstance(message, str) else message)
    return bytes([0x30 | flags]) + encode_remaining(len(payload)) + payload

def mqtt_subscribe(msg_id, topic, qos=0):
    tf = topic.encode()
    payload = struct.pack('>H', msg_id) + struct.pack('>H', len(tf)) + tf + bytes([qos])
    return bytes([0x82]) + encode_remaining(len(payload)) + payload

def mqtt_unsubscribe(msg_id, topic):
    tf = topic.encode()
    payload = struct.pack('>H', msg_id) + struct.pack('>H', len(tf)) + tf
    return bytes([0xA2]) + encode_remaining(len(payload)) + payload

def mqtt_pingreq():
    return bytes([0xC0, 0x00])

def mqtt_disconnect():
    return bytes([0xE0, 0x00])

def encode_remaining(length):
    out = bytearray()
    while True:
        byte = length % 128
        length //= 128
        if length > 0:
            byte |= 0x80
        out.append(byte)
        if length == 0:
            break
    return bytes(out)

class MQTTClient:
    """Simple blocking MQTT test client."""

    def __init__(self, port, client_id, timeout=2):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect(('127.0.0.1', port))
        self.sock.settimeout(timeout)
        self.client_id = client_id
        self.sock.send(mqtt_connect(client_id))
        connack = self.sock.recv(64)
        assert len(connack) >= 4 and connack[0] == 0x20 and connack[3] == 0, \
            f"CONNACK failed for {client_id}: {connack.hex()}"

    def subscribe(self, topic, qos=0, msg_id=1):
        self.sock.send(mqtt_subscribe(msg_id, topic, qos))
        # Read SUBACK + any retained messages that follow
        time.sleep(0.1)
        data = self.sock.recv(4096)
        assert data[0] == 0x90, f"Expected SUBACK, got 0x{data[0]:02x}"
        return data

    def publish(self, topic, message, qos=0, retain=False, msg_id=1):
        self.sock.send(mqtt_publish(topic, message, qos, retain, msg_id))
        if qos == 1:
            time.sleep(0.05)
            ack = self.sock.recv(64)
            assert ack[0] == 0x40, f"Expected PUBACK, got 0x{ack[0]:02x}"
            return ack
        return None

    def recv(self, timeout=0.5):
        self.sock.settimeout(timeout)
        try:
            return self.sock.recv(4096)
        except socket.timeout:
            return b''

    def ping(self):
        self.sock.send(mqtt_pingreq())
        resp = self.recv(1)
        assert resp[0] == 0xD0, f"Expected PINGRESP, got 0x{resp[0]:02x}"

    def disconnect(self):
        try:
            self.sock.send(mqtt_disconnect())
        except (BrokenPipeError, OSError):
            pass
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.disconnect()
