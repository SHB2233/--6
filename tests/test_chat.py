"""Standard-library integration tests for the real Qt chat server."""
import argparse
import base64
import ipaddress
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
import unittest


EXE = Path(__file__).resolve().parents[2] / "build" / "qt" / "release" / "QtTcpChat.exe"
MAX_FRAME = 10 * 1024 * 1024
MAX_FILE = 5 * 1024 * 1024


def encode(message):
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return struct.pack("!I", len(body)) + body


class Peer:
    def __init__(self, sock):
        self.socket = sock
        self.buffer = bytearray()
        self.inbox = []

    def close(self):
        self.socket.close()

    def send(self, message):
        self.socket.sendall(encode(message))

    def wait(self, kind, predicate=None, timeout=3):
        """Consume only the matching message; retain all unrelated broadcasts."""
        deadline = time.monotonic() + timeout
        while True:
            for index, message in enumerate(self.inbox):
                if message.get("type") == kind and (predicate is None or predicate(message)):
                    return self.inbox.pop(index)
            while len(self.buffer) >= 4:
                size = struct.unpack("!I", self.buffer[:4])[0]
                if not 0 < size <= MAX_FRAME:
                    raise AssertionError(f"Server returned invalid frame length: {size}")
                if len(self.buffer) < size + 4:
                    break
                body = bytes(self.buffer[4:size + 4])
                del self.buffer[:size + 4]
                message = json.loads(body.decode("utf-8"))
                if not isinstance(message, dict):
                    raise AssertionError("Server returned a non-object JSON frame")
                self.inbox.append(message)
            if any(m.get("type") == kind and (predicate is None or predicate(m))
                   for m in self.inbox):
                continue
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"Timed out waiting for {kind}; inbox={self.inbox!r}")
            self.socket.settimeout(remaining)
            try:
                data = self.socket.recv(65536)
            except socket.timeout as exc:
                raise TimeoutError(f"Timed out waiting for {kind}") from exc
            if not data:
                raise AssertionError(f"Server disconnected while waiting for {kind}")
            self.buffer.extend(data)

    def assert_absent(self, test, kind, predicate=None, timeout=0.10):
        try:
            message = self.wait(kind, predicate, timeout)
        except TimeoutError:
            return
        test.fail(f"Unexpected {kind}: {message!r}")


class ChatTests(unittest.TestCase):
    def test_whitespace_nickname_rejected(self):
        for name in ("Alice Smith", "Alice\tSmith"):
            peer = self.connect()
            peer.send({"type": "join", "nickname": name})
            self.assertIn("昵称", peer.wait("error")["text"])

    def setUp(self):
        self.assertTrue(EXE.is_file(), f"Missing executable: {EXE}")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            self.port = probe.getsockname()[1]
        self.peers = []
        self.server = subprocess.Popen(
            [str(EXE), "--headless", "--port", str(self.port), "--ring-timeout", "150"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(self.stop_server)
        deadline = time.monotonic() + 8
        while True:
            try:
                self.first_peer = self.connect()
                break
            except ConnectionRefusedError:
                if self.server.poll() is not None:
                    output, error = self.server.communicate()
                    self.fail(f"Server exited: {output.decode(errors='replace')} "
                              f"{error.decode(errors='replace')}")
                if time.monotonic() >= deadline:
                    self.fail("Headless server did not listen within 8 seconds")
                time.sleep(0.02)

    def stop_server(self):
        for peer in self.peers:
            peer.close()
        if self.server.poll() is None:
            self.server.terminate()
        self.server.communicate(timeout=5)

    def connect(self):
        peer = Peer(socket.create_connection(("127.0.0.1", self.port), timeout=3))
        self.peers.append(peer)
        return peer

    def login(self, name, peer=None):
        peer = peer or self.connect()
        peer.send({"type": "join", "nickname": name})
        joined = peer.wait("join_ok")
        self.assertEqual(joined["nickname"], name.strip())
        peer.wait("groups", lambda m: any(g["id"] == "lobby" and g["joined"] for g in m["groups"]))
        return peer

    def three(self):
        return (self.login("Alice", self.first_peer), self.login("Bob"), self.login("Carol"))

    def create_group(self, peer, name="研究小组"):
        peer.send({"type": "group_create", "name": name})
        joined = peer.wait("group_joined")
        self.assertEqual(joined["name"], name)
        group_id = joined["groupId"]
        peer.wait("groups", lambda m: any(g["id"] == group_id and g["joined"] for g in m["groups"]))
        return group_id

    def request_call(self, caller, callee, call_id):
        caller.send({"type": "call_request", "to": "Bob", "callId": call_id})
        caller.wait("call_ringing", lambda m: m["callId"] == call_id)
        incoming = callee.wait("call_incoming", lambda m: m["callId"] == call_id)
        self.assertEqual(incoming["from"], "Alice")

    def test_join_chinese_and_nickname_validation(self):
        self.login("  中文用户  ", self.first_peer)
        self.login("Alice")
        for nickname in ("Alice", "aLiCe", "", "   "):
            with self.subTest(nickname=nickname):
                peer = self.connect()
                peer.send({"type": "join", "nickname": nickname})
                self.assertIn("昵称", peer.wait("error")["text"])
                peer.socket.settimeout(3)
                self.assertEqual(peer.socket.recv(1), b"")

    def test_login_required(self):
        for kind in ("chat", "private", "file", "group_create", "call_request"):
            with self.subTest(kind=kind):
                self.first_peer.send({"type": kind, "text": "未登录", "name": "x",
                                      "data": "", "to": "Bob", "callId": "unauthorized"})
                self.assertIn("请先登录", self.first_peer.wait("error")["text"])
        self.login("Alice", self.first_peer)

    def test_split_and_coalesced_frames(self):
        peer = self.first_peer
        data = encode({"type": "join", "nickname": "拆包用户"})
        for fragment in (data[:1], data[1:3], data[3:7], data[7:]):
            peer.socket.sendall(fragment)
            time.sleep(0.01)
        peer.wait("join_ok")
        peer.socket.sendall(encode({"type": "chat", "text": "第一条中文"})
                            + encode({"type": "chat", "text": "第二条中文"}))
        self.assertEqual(peer.wait("chat")["text"], "第一条中文")
        self.assertEqual(peer.wait("chat")["text"], "第二条中文")

    def test_private_message_three_client_isolation(self):
        alice, bob, carol = self.three()
        alice.send({"type": "private", "to": "bOB", "text": "只给Bob的中文"})
        for peer in (alice, bob):
            message = peer.wait("private")
            self.assertEqual((message["from"], message["to"], message["text"]),
                             ("Alice", "Bob", "只给Bob的中文"))
        carol.assert_absent(self, "private")

    def test_group_membership_isolation_and_disconnect_cleanup(self):
        alice, bob, carol = self.three()
        group_id = self.create_group(alice)
        for message in ({"type": "chat", "groupId": group_id, "text": "越权消息"},
                        {"type": "file", "groupId": group_id, "name": "越权.txt", "data": ""}):
            bob.send(message)
            self.assertIn("请先加入", bob.wait("error")["text"])
        alice.assert_absent(self, "chat")
        alice.assert_absent(self, "file")
        bob.send({"type": "group_join", "groupId": group_id})
        self.assertEqual(bob.wait("group_joined")["groupId"], group_id)
        bob.wait("groups", lambda m: any(g["id"] == group_id and g["joined"] for g in m["groups"]))
        alice.send({"type": "chat", "groupId": group_id, "text": "群内中文"})
        for peer in (alice, bob):
            message = peer.wait("chat")
            self.assertEqual((message["groupId"], message["text"]), (group_id, "群内中文"))
        carol.assert_absent(self, "chat")
        alice.send({"type": "file", "groupId": group_id, "name": "群文件.txt", "data": ""})
        self.assertEqual(bob.wait("file")["groupId"], group_id)
        carol.assert_absent(self, "file")
        bob.close()
        users = alice.wait("users", lambda m: set(m["users"]) == {"Alice", "Carol"})
        self.assertEqual(set(users["users"]), {"Alice", "Carol"})
        groups = alice.wait("groups", lambda m:
                            any(g["id"] == group_id and "Bob" not in g["members"] for g in m["groups"])
                            and any(g["id"] == "lobby" and set(g["members"]) == {"Alice", "Carol"}
                                    for g in m["groups"]))
        group = next(g for g in groups["groups"] if g["id"] == group_id)
        self.assertEqual(group["members"], ["Alice"])

    def check_file(self, data, filename):
        alice = self.login("Alice", self.first_peer)
        bob = self.login("Bob")
        encoded = base64.b64encode(data).decode("ascii")
        alice.send({"type": "file", "name": filename, "data": encoded})
        message = bob.wait("file", timeout=8)
        self.assertEqual(message["name"], filename)
        self.assertEqual(message["from"], "Alice")
        self.assertEqual(base64.b64decode(message["data"], validate=True), data)
        alice.wait("system", lambda m: filename in m.get("text", "") and "已转发" in m["text"])

    def test_empty_file(self):
        self.check_file(b"", "空文件.txt")

    def test_chinese_filename_and_binary_file(self):
        self.check_file(bytes(range(256)) + "中文内容".encode("utf-8") + b"\x00\xff", "中文二进制.bin")

    def test_five_mib_file_boundary(self):
        self.check_file(bytes(range(256)) * (MAX_FILE // 256), "边界文件.bin")

    def test_oversized_file_and_invalid_base64_rejected(self):
        alice = self.login("Alice", self.first_peer)
        bob = self.login("Bob")
        for data in (base64.b64encode(b"x" * (MAX_FILE + 1)).decode("ascii"), "%%%invalid%%%"): 
            with self.subTest(oversized=len(data) > MAX_FILE):
                alice.send({"type": "file", "name": "拒绝.bin", "data": data})
                self.assertIn("文件无效", alice.wait("error", timeout=8)["text"])
                bob.assert_absent(self, "file")

    def test_call_accept_observed_address_and_nonparticipant_end(self):
        alice, bob, carol = self.three()
        self.request_call(alice, bob, "accepted")
        bob.send({"type": "call_response", "callId": "accepted", "accept": True, "port": 23456})
        accepted = alice.wait("call_accepted")
        self.assertEqual((accepted["callId"], accepted["port"], accepted["from"]),
                         ("accepted", 23456, "Bob"))
        observed = ipaddress.ip_address(accepted["host"])
        if isinstance(observed, ipaddress.IPv6Address) and observed.ipv4_mapped:
            observed = observed.ipv4_mapped
        self.assertEqual(str(observed), bob.socket.getsockname()[0])
        carol.send({"type": "call_end", "callId": "accepted"})
        alice.assert_absent(self, "call_ended", timeout=0.20)
        bob.assert_absent(self, "call_ended")
        alice.send({"type": "call_end", "callId": "accepted"})
        for peer in (alice, bob):
            self.assertEqual(peer.wait("call_ended")["callId"], "accepted")

    def test_call_reject(self):
        alice, bob, _ = self.three()
        self.request_call(alice, bob, "reject")
        bob.send({"type": "call_response", "callId": "reject", "accept": False})
        for peer in (alice, bob):
            self.assertIn("拒绝", peer.wait("call_ended")["reason"])

    def test_call_offline(self):
        alice = self.login("Alice", self.first_peer)
        alice.send({"type": "call_request", "to": "Nobody", "callId": "offline"})
        ended = alice.wait("call_ended")
        self.assertEqual(ended["callId"], "offline")
        self.assertIn("不在线", ended["reason"])

    def test_call_busy_and_cancel(self):
        alice, bob, carol = self.three()
        self.request_call(alice, bob, "busy-first")
        carol.send({"type": "call_request", "to": "Bob", "callId": "busy-second"})
        ended = carol.wait("call_ended")
        self.assertEqual(ended["callId"], "busy-second")
        self.assertIn("忙线", ended["reason"])
        alice.send({"type": "call_cancel", "callId": "busy-first"})
        for peer in (alice, bob):
            self.assertEqual(peer.wait("call_ended")["callId"], "busy-first")

    def test_call_unanswered_uses_configured_timeout(self):
        alice, bob, _ = self.three()
        started = time.monotonic()
        self.request_call(alice, bob, "timeout")
        for peer in (alice, bob):
            ended = peer.wait("call_ended", timeout=2)
            self.assertEqual(ended["callId"], "timeout")
            self.assertIn("未接听", ended["reason"])
        self.assertGreater(time.monotonic() - started, 0.08)
        self.assertLess(time.monotonic() - started, 2)

    def test_invalid_wire_frames_rejected(self):
        for payload in (struct.pack("!I", 0), struct.pack("!I", MAX_FRAME + 1),
                        struct.pack("!I", 2) + b"[]", struct.pack("!I", 1) + b"{"):
            with self.subTest(payload=payload):
                peer = self.connect()
                peer.socket.sendall(payload)
                self.assertIn("无效的数据帧", peer.wait("error")["text"])
                peer.socket.settimeout(3)
                self.assertEqual(peer.socket.recv(1), b"")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=EXE)
    arguments, unittest_args = parser.parse_known_args()
    EXE = arguments.exe.resolve()
    unittest.main(argv=[sys.argv[0], *unittest_args])
