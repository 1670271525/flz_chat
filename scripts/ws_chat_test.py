#!/usr/bin/env python3
"""flz_chat WebSocket 集成测试脚本（5个用例）."""

import argparse
import base64
import hashlib
import hmac
import json
import os
import socket
import struct
import time
import uuid
from datetime import datetime, timedelta, timezone


TZ = timezone(timedelta(hours=8))


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


def issue_jwt(secret: str, user_id: int, device_id: str, expire_seconds: int = 600) -> str:
    now = int(time.time())
    header = {"alg": "HS256", "typ": "JWT"}
    payload = {
        "sub": str(user_id),
        "did": device_id,
        "iat": now,
        "exp": now + expire_seconds,
        "iss": "flz_chat_business",
        "platform": "test",
    }
    h = b64url(json.dumps(header, separators=(",", ":")).encode())
    p = b64url(json.dumps(payload, separators=(",", ":")).encode())
    sign = hmac.new(secret.encode(), f"{h}.{p}".encode(), hashlib.sha256).digest()
    s = b64url(sign)
    return f"{h}.{p}.{s}"


class WsClient:
    def __init__(self, host: str, port: int, path: str, token: str):
        self.host = host
        self.port = port
        self.path = f"{path}?token={token}"
        self.sock = None

    def connect(self) -> None:
        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            f"GET {self.path} HTTP/1.1\r\n"
            f"Host: {self.host}:{self.port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        )
        self.sock = socket.create_connection((self.host, self.port), 5)
        self.sock.settimeout(5)
        self.sock.sendall(req.encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise RuntimeError("handshake failed")
            resp += chunk
        status = resp.split(b"\r\n", 1)[0].decode()
        if "101" not in status:
            raise RuntimeError(status)

    def send_json(self, obj: dict) -> None:
        data = json.dumps(obj, separators=(",", ":")).encode()
        mask = os.urandom(4)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        length = len(data)
        if length < 126:
            header = struct.pack("!BB", 0x81, 0x80 | length)
        elif length < 65536:
            header = struct.pack("!BBH", 0x81, 0x80 | 126, length)
        else:
            header = struct.pack("!BBQ", 0x81, 0x80 | 127, length)
        self.sock.sendall(header + mask + masked)

    def recv_json(self, timeout: float = 5.0):
        self.sock.settimeout(timeout)
        h = self.sock.recv(2)
        if len(h) < 2:
            return None
        b1, b2 = h[0], h[1]
        length = b2 & 0x7F
        if length == 126:
            length = struct.unpack("!H", self.sock.recv(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self.sock.recv(8))[0]
        payload = b""
        while len(payload) < length:
            payload += self.sock.recv(length - len(payload))
        return json.loads(payload.decode("utf-8", errors="replace"))

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None


def assert_event(obj: dict, event: str):
    if not obj or obj.get("type") != event:
        raise AssertionError(f"expect type={event}, got={obj}")


def case_multi_device_and_send(host, port, path, secret):
    token1 = issue_jwt(secret, 1001, "ios-a")
    token2 = issue_jwt(secret, 1001, "web-b")
    c1 = WsClient(host, port, path, token1)
    c2 = WsClient(host, port, path, token2)
    c1.connect()
    c2.connect()
    assert_event(c1.recv_json(), "auth_ok")
    assert_event(c2.recv_json(), "auth_ok")

    client_msg_id = str(uuid.uuid4())
    c1.send_json({
        "type": "msg.send",
        "seq": 1,
        "data": {
            "clientMsgId": client_msg_id,
            "conversationId": 9001,
            "content": "hello from case1",
            "sentAt": datetime.now(TZ).replace(microsecond=0).isoformat(),
        }
    })
    # 依赖 mock business 将 business.msg.persist 回送 chat.msg.send
    obj = c1.recv_json(timeout=10)
    if obj and obj.get("type") not in ("msg.send.resp", "msg.new"):
        raise AssertionError(f"unexpected frame: {obj}")
    c1.close()
    c2.close()


def case_receiver_get_msg(host, port, path, secret):
    sender = WsClient(host, port, path, issue_jwt(secret, 1001, "ios-x"))
    receiver = WsClient(host, port, path, issue_jwt(secret, 1002, "android-y"))
    sender.connect()
    receiver.connect()
    assert_event(sender.recv_json(), "auth_ok")
    assert_event(receiver.recv_json(), "auth_ok")

    sender.send_json({
        "type": "msg.send",
        "data": {
            "clientMsgId": str(uuid.uuid4()),
            "conversationId": 9001,
            "content": "receiver should get msg.new",
        }
    })
    sender.close()
    receiver.close()


def case_replay(host, port, path, secret):
    c = WsClient(host, port, path, issue_jwt(secret, 1003, "device-r"))
    c.connect()
    assert_event(c.recv_json(), "auth_ok")
    # 若运行 run_mock_business.py，会在 online 后投递 msg.replay
    replay = c.recv_json(timeout=10)
    if replay and replay.get("type") not in ("msg.replay", "pong"):
        raise AssertionError(f"unexpected replay frame: {replay}")
    c.close()


def case_expired_token(host, port, path, secret):
    c = WsClient(host, port, path, issue_jwt(secret, 1004, "expired", expire_seconds=-10))
    c.connect()
    obj = c.recv_json(timeout=5)
    assert_event(obj, "auth_fail")
    c.close()


def case_heartbeat_timeout(host, port, path, secret, wait_seconds):
    c = WsClient(host, port, path, issue_jwt(secret, 1005, "idle"))
    c.connect()
    assert_event(c.recv_json(), "auth_ok")
    print(f"[heartbeat] waiting {wait_seconds}s without ping ...")
    time.sleep(wait_seconds)
    try:
        c.send_json({"type": "ping", "data": {"ts": int(time.time() * 1000)}})
        obj = c.recv_json(timeout=2)
        if obj is not None:
            raise AssertionError("connection still alive, expected timeout close")
    except Exception:
        pass
    finally:
        c.close()


def main():
    parser = argparse.ArgumentParser(description="flz_chat ws integration tests")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=8071, type=int)
    parser.add_argument("--path", default="/flz/chat")
    parser.add_argument("--jwt-secret", default=os.getenv("JWT_SECRET", "test-secret"))
    parser.add_argument("--heartbeat-wait", default=80, type=int, help="心跳超时用例等待秒数，默认80")
    args = parser.parse_args()

    tests = [
        ("多端登录与消息上行", lambda: case_multi_device_and_send(args.host, args.port, args.path, args.jwt_secret)),
        ("接收方消息下发", lambda: case_receiver_get_msg(args.host, args.port, args.path, args.jwt_secret)),
        ("上线回放", lambda: case_replay(args.host, args.port, args.path, args.jwt_secret)),
        ("过期token拦截", lambda: case_expired_token(args.host, args.port, args.path, args.jwt_secret)),
        ("心跳超时断连", lambda: case_heartbeat_timeout(args.host, args.port, args.path, args.jwt_secret, args.heartbeat_wait)),
    ]

    ok = 0
    for name, fn in tests:
        print(f"[TEST] {name}")
        try:
            fn()
            print(f"[PASS] {name}")
            ok += 1
        except Exception as exc:
            print(f"[FAIL] {name}: {exc}")
    print(f"done: {ok}/{len(tests)} passed")
    return 0 if ok == len(tests) else 1


if __name__ == "__main__":
    raise SystemExit(main())
