#!/usr/bin/env python3
"""
tracker_shim.py

Small local bridge for tracker.html:
- connects to a plain TCP MQTT broker (MQTT 3.1.1)
- subscribes to conez/+/status
- stores the latest node payloads in memory
- serves tracker.html and a JSON snapshot endpoint for the browser

No third-party dependencies required.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import select
import socket
import socketserver
import struct
import threading
import time
from dataclasses import dataclass, field
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from urllib.parse import parse_qs, urlparse


TOPIC_ROOT = "conez"
TOPIC_STATUS_PATTERN = f"{TOPIC_ROOT}/+/status"
DEFAULT_MQTT_HOST = "sewerpipe.local"
DEFAULT_MQTT_PORT = 1883
DEFAULT_HTTP_HOST = "127.0.0.1"
DEFAULT_HTTP_PORT = 8080
DEFAULT_KEEPALIVE_S = 30
RETRY_INTERVAL_S = 2
WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
DEFAULT_TELNET_PORT = 23


class WebSocketClosed(Exception):
    pass


def encode_remaining_length(value: int) -> bytes:
    out = bytearray()
    while True:
        digit = value % 128
        value //= 128
        if value > 0:
            digit |= 0x80
        out.append(digit)
        if value == 0:
            return bytes(out)


def decode_remaining_length(sock: socket.socket) -> int:
    multiplier = 1
    value = 0
    while True:
      raw = sock.recv(1)
      if not raw:
          raise ConnectionError("socket closed while decoding remaining length")
      digit = raw[0]
      value += (digit & 0x7F) * multiplier
      if (digit & 0x80) == 0:
          return value
      multiplier *= 128
      if multiplier > (128 * 128 * 128):
          raise ValueError("malformed MQTT remaining length")


def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        part = sock.recv(size - len(chunks))
        if not part:
            raise ConnectionError("socket closed while reading MQTT packet")
        chunks.extend(part)
    return bytes(chunks)


def encode_utf8_field(value: str) -> bytes:
    data = value.encode("utf-8")
    return struct.pack("!H", len(data)) + data


def parse_utf8_field(buf: bytes, offset: int = 0) -> Tuple[str, int]:
    if offset + 2 > len(buf):
        raise ValueError("short MQTT utf8 field")
    size = struct.unpack("!H", buf[offset:offset + 2])[0]
    start = offset + 2
    end = start + size
    if end > len(buf):
        raise ValueError("truncated MQTT utf8 field")
    return buf[start:end].decode("utf-8", errors="replace"), end


def recv_http_headers(sock: socket.socket) -> bytes:
    data = bytearray()
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("socket closed during websocket handshake")
        data.extend(chunk)
        if len(data) > 16384:
            raise ConnectionError("websocket handshake too large")
    return bytes(data)


def parse_http_header_block(raw: bytes) -> Tuple[str, Dict[str, str]]:
    text = raw.decode("iso-8859-1", errors="replace")
    lines = text.split("\r\n")
    request_line = lines[0]
    headers: Dict[str, str] = {}
    for line in lines[1:]:
        if not line or ":" not in line:
            continue
        key, value = line.split(":", 1)
        headers[key.strip().lower()] = value.strip()
    return request_line, headers


def websocket_accept_value(key: str) -> str:
    digest = hashlib.sha1((key + WEBSOCKET_GUID).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


def build_ws_frame(payload: bytes, opcode: int = 1) -> bytes:
    header = bytearray()
    header.append(0x80 | (opcode & 0x0F))
    length = len(payload)
    if length < 126:
        header.append(length)
    elif length < 65536:
        header.append(126)
        header.extend(struct.pack("!H", length))
    else:
        header.append(127)
        header.extend(struct.pack("!Q", length))
    return bytes(header) + payload


def recv_ws_frame(sock: socket.socket) -> Tuple[int, bytes]:
    first_two = recv_exact(sock, 2)
    b1, b2 = first_two
    opcode = b1 & 0x0F
    masked = (b2 & 0x80) != 0
    length = b2 & 0x7F

    if length == 126:
        length = struct.unpack("!H", recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", recv_exact(sock, 8))[0]

    mask_key = recv_exact(sock, 4) if masked else b""
    payload = recv_exact(sock, length) if length else b""

    if masked:
        payload = bytes(byte ^ mask_key[index % 4] for index, byte in enumerate(payload))

    if opcode == 0x8:
        raise WebSocketClosed()
    return opcode, payload


class TelnetStreamFilter:
    def __init__(self) -> None:
        self.state = 0
        self.cmd = 0
        self.sb_iac = False

    def feed(self, data: bytes) -> Tuple[bytes, bytes]:
        out = bytearray()
        responses = bytearray()

        for byte in data:
            if self.state == 0:
                if byte == 0xFF:
                    self.state = 1
                else:
                    out.append(byte)
                continue

            if self.state == 1:
                if byte == 0xFF:
                    out.append(0xFF)
                    self.state = 0
                elif byte in (0xFB, 0xFC, 0xFD, 0xFE):
                    self.cmd = byte
                    self.state = 2
                elif byte == 0xFA:
                    self.state = 3
                    self.sb_iac = False
                else:
                    self.state = 0
                continue

            if self.state == 2:
                if self.cmd == 0xFB:
                    responses.extend((0xFF, 0xFD, byte))
                elif self.cmd == 0xFC:
                    responses.extend((0xFF, 0xFE, byte))
                elif self.cmd == 0xFD:
                    responses.extend((0xFF, 0xFC, byte))
                elif self.cmd == 0xFE:
                    responses.extend((0xFF, 0xFC, byte))
                self.state = 0
                continue

            if self.state == 3:
                if self.sb_iac and byte == 0xF0:
                    self.state = 0
                    self.sb_iac = False
                else:
                    self.sb_iac = (byte == 0xFF)

        return bytes(out), bytes(responses)


def build_connect_packet(client_id: str, keepalive_s: int) -> bytes:
    variable = (
        encode_utf8_field("MQTT") +
        bytes([0x04, 0x02]) +
        struct.pack("!H", keepalive_s)
    )
    payload = encode_utf8_field(client_id)
    remaining = encode_remaining_length(len(variable) + len(payload))
    return bytes([0x10]) + remaining + variable + payload


def build_subscribe_packet(packet_id: int, topics: List[str]) -> bytes:
    payload = b"".join(encode_utf8_field(topic) + b"\x00" for topic in topics)
    variable = struct.pack("!H", packet_id)
    remaining = encode_remaining_length(len(variable) + len(payload))
    return bytes([0x82]) + remaining + variable + payload


def build_puback(packet_id: int) -> bytes:
    return b"\x40\x02" + struct.pack("!H", packet_id)


PINGREQ_PACKET = b"\xC0\x00"


@dataclass
class NodeEntry:
    node_id: str
    topic: str
    payload: object
    updated_at: str


@dataclass
class TrackerState:
    mqtt_host: str
    mqtt_port: int
    topic: str = TOPIC_STATUS_PATTERN
    connected: bool = False
    last_error: str = ""
    nodes: Dict[str, NodeEntry] = field(default_factory=dict)
    lock: threading.Lock = field(default_factory=threading.Lock)

    def endpoint(self) -> str:
        return f"{self.mqtt_host}:{self.mqtt_port}"

    def set_connected(self, connected: bool, error: str = "") -> None:
        with self.lock:
            self.connected = connected
            self.last_error = error

    def update_from_publish(self, topic: str, payload_text: str) -> None:
        parsed = self._try_parse_json(payload_text)
        timestamp = time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime()) + time.strftime("%z")
        with self.lock:
            if topic == TOPIC_ROOT:
                if isinstance(parsed, dict):
                    for node_id, node_payload in parsed.items():
                        self.nodes[str(node_id)] = NodeEntry(
                            node_id=str(node_id),
                            topic=topic,
                            payload=node_payload,
                            updated_at=timestamp,
                        )
                else:
                    self.nodes[TOPIC_ROOT] = NodeEntry(
                        node_id=TOPIC_ROOT,
                        topic=topic,
                        payload=parsed if parsed is not None else payload_text,
                        updated_at=timestamp,
                    )
                return

            if topic.startswith(TOPIC_ROOT + "/"):
                suffix = topic[len(TOPIC_ROOT) + 1:]
                node_id = suffix.split("/", 1)[0] or topic
            else:
                node_id = topic

            self.nodes[node_id] = NodeEntry(
                node_id=node_id,
                topic=topic,
                payload=parsed if parsed is not None else payload_text,
                updated_at=timestamp,
            )

    def snapshot(self) -> dict:
        with self.lock:
            nodes = [
                {
                    "nodeId": entry.node_id,
                    "topic": entry.topic,
                    "payload": entry.payload,
                    "updatedAt": entry.updated_at,
                }
                for entry in sorted(self.nodes.values(), key=lambda item: item.node_id.lower())
            ]
            return {
                "connected": self.connected,
                "broker": self.endpoint(),
                "topic": self.topic,
                "lastError": self.last_error,
                "nodes": nodes,
            }

    @staticmethod
    def _try_parse_json(text: str) -> Optional[object]:
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return None


class MqttBridge(threading.Thread):
    def __init__(self, state: TrackerState, keepalive_s: int) -> None:
        super().__init__(daemon=True)
        self.state = state
        self.keepalive_s = keepalive_s
        self.stop_event = threading.Event()
        self.packet_id = 1
        self.publish_count = 0

    def stop(self) -> None:
        self.stop_event.set()

    def run(self) -> None:
        while not self.stop_event.is_set():
            try:
                self._run_session()
            except Exception as exc:
                self.state.set_connected(False, str(exc))
                print(f"MQTT bridge error: {exc}")
            if not self.stop_event.wait(RETRY_INTERVAL_S):
                continue

    def _run_session(self) -> None:
        client_id = f"tracker-shim-{os.getpid()}"
        print(f"Connecting to MQTT broker {self.state.endpoint()} ...")
        with socket.create_connection((self.state.mqtt_host, self.state.mqtt_port), timeout=10) as sock:
            sock.settimeout(1.0)
            sock.sendall(build_connect_packet(client_id, self.keepalive_s))
            self._expect_connack(sock)
            print("MQTT connected")
            sock.sendall(build_subscribe_packet(self._next_packet_id(), [TOPIC_STATUS_PATTERN]))
            self._expect_suback(sock)
            self.state.set_connected(True, "")
            print(f"Subscribed to {TOPIC_STATUS_PATTERN}")

            last_tx = time.monotonic()
            while not self.stop_event.is_set():
                if time.monotonic() - last_tx >= (self.keepalive_s / 2):
                    sock.sendall(PINGREQ_PACKET)
                    last_tx = time.monotonic()

                try:
                    packet_type, flags, payload = self._read_packet(sock)
                except socket.timeout:
                    continue

                last_tx = time.monotonic()
                if packet_type == 3:
                    self._handle_publish(sock, flags, payload)
                elif packet_type == 9:
                    continue
                elif packet_type == 13:
                    continue
                else:
                    continue

    def _next_packet_id(self) -> int:
        packet_id = self.packet_id
        self.packet_id = 1 if self.packet_id >= 0xFFFF else self.packet_id + 1
        return packet_id

    def _expect_connack(self, sock: socket.socket) -> None:
        packet_type, _flags, payload = self._read_packet(sock)
        if packet_type != 2 or len(payload) < 2:
            raise ConnectionError("did not receive CONNACK")
        return_code = payload[1]
        if return_code != 0:
            raise ConnectionError(f"broker rejected CONNECT with code {return_code}")

    def _expect_suback(self, sock: socket.socket) -> None:
        packet_type, _flags, payload = self._read_packet(sock)
        if packet_type != 9 or len(payload) < 3:
            raise ConnectionError("did not receive SUBACK")
        if any(code == 0x80 for code in payload[2:]):
            raise ConnectionError("broker rejected one or more subscriptions")

    def _read_packet(self, sock: socket.socket) -> Tuple[int, int, bytes]:
        first = recv_exact(sock, 1)[0]
        packet_type = first >> 4
        flags = first & 0x0F
        remaining = decode_remaining_length(sock)
        payload = recv_exact(sock, remaining) if remaining else b""
        return packet_type, flags, payload

    def _handle_publish(self, sock: socket.socket, flags: int, payload: bytes) -> None:
        topic, offset = parse_utf8_field(payload, 0)
        qos = (flags >> 1) & 0x03
        packet_id = None
        if qos > 0:
            if offset + 2 > len(payload):
                raise ValueError("truncated QoS publish packet")
            packet_id = struct.unpack("!H", payload[offset:offset + 2])[0]
            offset += 2

        body = payload[offset:].decode("utf-8", errors="replace")
        self.state.update_from_publish(topic, body)
        self.publish_count += 1
        print(f"MQTT publish #{self.publish_count}: {topic}")

        if qos == 1 and packet_id is not None:
            sock.sendall(build_puback(packet_id))


class TrackerRequestHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, directory: str, state: TrackerState, **kwargs) -> None:
        self.state = state
        super().__init__(*args, directory=directory, **kwargs)

    def log_message(self, fmt: str, *args) -> None:
        return

    def do_GET(self) -> None:
        if self.path in ("/api/status", "/api/status/"):
            self._serve_status()
            return
        if self.path in ("/", "/tracker.html"):
            self.path = "/tracker.html"
        return super().do_GET()

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def _serve_status(self) -> None:
        payload = json.dumps(self.state.snapshot()).encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


class ReusableThreadingHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True


class ReusableThreadingTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


class TelnetWebSocketHandler(socketserver.BaseRequestHandler):
    def handle(self) -> None:
        self.request.settimeout(5.0)
        raw_headers = recv_http_headers(self.request)
        request_line, headers = parse_http_header_block(raw_headers)
        parts = request_line.split()
        if len(parts) < 2:
            return

        parsed_url = urlparse(parts[1])
        if parsed_url.path != "/telnet":
            self._send_http_error(HTTPStatus.NOT_FOUND, "Not Found")
            return

        ws_key = headers.get("sec-websocket-key")
        upgrade = headers.get("upgrade", "")
        if not ws_key or upgrade.lower() != "websocket":
            self._send_http_error(HTTPStatus.BAD_REQUEST, "Expected websocket upgrade")
            return

        query = parse_qs(parsed_url.query)
        host = (query.get("host") or [""])[0].strip()
        try:
            port = int((query.get("port") or [str(DEFAULT_TELNET_PORT)])[0])
        except ValueError:
            port = DEFAULT_TELNET_PORT

        if not host:
            self._send_http_error(HTTPStatus.BAD_REQUEST, "Missing host")
            return

        response = (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Accept: {websocket_accept_value(ws_key)}\r\n"
            "\r\n"
        ).encode("ascii")
        self.request.sendall(response)

        try:
            with socket.create_connection((host, port), timeout=5.0) as telnet_sock:
                telnet_sock.settimeout(0.2)
                self.request.settimeout(0.2)
                self._proxy_telnet(telnet_sock)
        except WebSocketClosed:
            return
        except Exception as exc:
            message = f"Connection failed: {exc}\r\n".encode("utf-8", errors="replace")
            try:
                self.request.sendall(build_ws_frame(message))
            except OSError:
                return

    def _send_http_error(self, status: HTTPStatus, text: str) -> None:
        body = text.encode("utf-8", errors="replace")
        payload = (
            f"HTTP/1.1 {status.value} {status.phrase}\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            f"Content-Length: {len(body)}\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).encode("ascii") + body
        self.request.sendall(payload)

    def _proxy_telnet(self, telnet_sock: socket.socket) -> None:
        filter_state = TelnetStreamFilter()

        while True:
            readable, _writable, _errors = select.select([self.request, telnet_sock], [], [], 0.2)

            if self.request in readable:
                opcode, payload = recv_ws_frame(self.request)
                if opcode == 0x9:
                    self.request.sendall(build_ws_frame(payload, opcode=0xA))
                    continue
                if opcode in (0x1, 0x2) and payload:
                    telnet_sock.sendall(payload)

            if telnet_sock in readable:
                chunk = telnet_sock.recv(4096)
                if not chunk:
                    break
                filtered, responses = filter_state.feed(chunk)
                if responses:
                    telnet_sock.sendall(responses)
                if filtered:
                    text_payload = filtered.decode("utf-8", errors="replace").encode("utf-8")
                    self.request.sendall(build_ws_frame(text_payload))


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Serve ConeZ tracker page with a TCP MQTT shim.")
    parser.add_argument("--mqtt-host", default=DEFAULT_MQTT_HOST, help=f"MQTT broker host (default: {DEFAULT_MQTT_HOST})")
    parser.add_argument("--mqtt-port", type=int, default=DEFAULT_MQTT_PORT, help=f"MQTT broker port (default: {DEFAULT_MQTT_PORT})")
    parser.add_argument("--http-host", default=DEFAULT_HTTP_HOST, help=f"HTTP bind host (default: {DEFAULT_HTTP_HOST})")
    parser.add_argument("--http-port", type=int, default=DEFAULT_HTTP_PORT, help=f"HTTP bind port (default: {DEFAULT_HTTP_PORT})")
    parser.add_argument("--ws-port", type=int, default=0, help="WebSocket telnet bridge port (default: http-port + 1)")
    parser.add_argument("--keepalive", type=int, default=DEFAULT_KEEPALIVE_S, help=f"MQTT keepalive in seconds (default: {DEFAULT_KEEPALIVE_S})")
    return parser


def main() -> int:
    args = build_arg_parser().parse_args()
    tracker_dir = Path(__file__).resolve().parent
    state = TrackerState(mqtt_host=args.mqtt_host, mqtt_port=args.mqtt_port)
    bridge = MqttBridge(state=state, keepalive_s=args.keepalive)
    bridge.start()
    ws_port = args.ws_port if args.ws_port > 0 else args.http_port + 1

    def handler(*handler_args, **handler_kwargs):
        return TrackerRequestHandler(
            *handler_args,
            directory=str(tracker_dir),
            state=state,
            **handler_kwargs,
        )

    with ReusableThreadingHTTPServer((args.http_host, args.http_port), handler) as server, \
         ReusableThreadingTCPServer((args.http_host, ws_port), TelnetWebSocketHandler) as ws_server:
        ws_thread = threading.Thread(target=ws_server.serve_forever, kwargs={"poll_interval": 0.5}, daemon=True)
        ws_thread.start()
        print(f"Tracker HTTP server: http://{args.http_host}:{args.http_port}/")
        print(f"Telnet WebSocket bridge: ws://{args.http_host}:{ws_port}/telnet")
        print(f"MQTT broker target: {args.mqtt_host}:{args.mqtt_port}")
        try:
            server.serve_forever(poll_interval=0.5)
        except KeyboardInterrupt:
            pass
        finally:
            ws_server.shutdown()
            ws_thread.join(timeout=2.0)
            bridge.stop()
            bridge.join(timeout=2.0)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
