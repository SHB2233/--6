"""Real-process tests for the basic, length-prefixed TCP programs."""
import argparse
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
import unittest


BIN_DIR = Path(__file__).resolve().parents[2] / "build" / "basic"


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def frame(text):
    data = text.encode("utf-8")
    return struct.pack("!I", len(data)) + data


def recv_exact(peer, length):
    data = bytearray()
    while len(data) < length:
        chunk = peer.recv(length - len(data))
        if not chunk:
            raise AssertionError("peer closed before a complete frame arrived")
        data.extend(chunk)
    return bytes(data)


def recv_frame(peer):
    length = struct.unpack("!I", recv_exact(peer, 4))[0]
    return recv_exact(peer, length).decode("utf-8")


class BasicTCPTests(unittest.TestCase):
    def setUp(self):
        suffix = ".exe" if sys.platform == "win32" else ""
        self.server_exe = BIN_DIR / ("tcp_basic_server" + suffix)
        self.client_exe = BIN_DIR / ("tcp_basic_client" + suffix)
        for executable in (self.server_exe, self.client_exe):
            self.assertTrue(executable.is_file(), f"Missing executable: {executable}")
        self.processes = []

    def tearDown(self):
        for process in self.processes:
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=5)

    def server(self, replies=""):
        port = free_port()
        process = subprocess.Popen(
            [str(self.server_exe), str(port)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8", errors="strict")
        self.processes.append(process)
        if replies:
            process.stdin.write(replies)
            process.stdin.flush()
        return process, port

    def peer(self, port):
        deadline = time.monotonic() + 5
        while True:
            try:
                peer = socket.create_connection(("127.0.0.1", port), timeout=2)
                self.addCleanup(peer.close)
                return peer
            except ConnectionRefusedError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.02)

    def client(self, port, text):
        # A failed connection has no application side effects; retry only startup races.
        deadline = time.monotonic() + 5
        while True:
            result = subprocess.run(
                [str(self.client_exe), "127.0.0.1", str(port)], input=text,
                capture_output=True, text=True, encoding="utf-8", timeout=5)
            if ("connect failed" not in result.stderr.lower()
                    or time.monotonic() >= deadline):
                return result
            time.sleep(0.02)

    def test_chinese_round_trip_and_quit(self):
        server, port = self.server("服务端回复：收到中文。\n")
        client = self.client(port, "客户端：你好，网络！\n/quit\n")
        output, error = server.communicate(timeout=5)
        self.assertEqual(client.returncode, 0, client.stderr)
        self.assertEqual(server.returncode, 0, error)
        self.assertIn("服务端回复：收到中文。", client.stdout)
        self.assertIn("客户端：你好，网络！", output)
        self.assertIn("closed the session", client.stdout)

    def test_client_empty_line_keeps_session(self):
        server, port = self.server("收到\n")
        client = self.client(port, "\n空行之后\n/quit\n")
        output, error = server.communicate(timeout=5)
        self.assertEqual(client.returncode, 0, client.stderr)
        self.assertEqual(server.returncode, 0, error)
        self.assertIn("空行之后", output)
        self.assertIn("收到", client.stdout)
        self.assertIn("Empty input", client.stdout)

    def test_server_empty_reply_keeps_session(self):
        server, port = self.server("\n重新输入回复\n")
        client = self.client(port, "请回复\n/quit\n")
        output, error = server.communicate(timeout=5)
        self.assertEqual(client.returncode, 0, client.stderr)
        self.assertEqual(server.returncode, 0, error)
        self.assertIn("重新输入回复", client.stdout)
        self.assertIn("Empty input", output)

    def test_fragmented_and_coalesced_frames(self):
        server, port = self.server("第一条回复\n第二条回复\n")
        peer = self.peer(port)
        first = frame("拆开的中文消息")
        # Split both the four-byte header and a UTF-8 body across writes.
        for part in (first[:1], first[1:3], first[3:6], first[6:]):
            peer.sendall(part)
            time.sleep(0.01)
        self.assertEqual(recv_frame(peer), "第一条回复")
        peer.sendall(frame("合并消息") + frame("/quit"))
        self.assertEqual(recv_frame(peer), "第二条回复")
        self.assertIn("closed the session", recv_frame(peer))
        output, error = server.communicate(timeout=5)
        self.assertEqual(server.returncode, 0, error)
        self.assertIn("拆开的中文消息", output)
        self.assertIn("合并消息", output)

    def test_invalid_ports_for_both_programs(self):
        invalid = ("", "abc", "12abc", "+8888", "-1", "0", "65536",
                   "9999999999999999999999999", " 8888", "8888 ")
        for executable, prefix in ((self.server_exe, []),
                                   (self.client_exe, ["127.0.0.1"])):
            for port in invalid:
                with self.subTest(program=executable.name, port=port):
                    result = subprocess.run(
                        [str(executable), *prefix, port], capture_output=True,
                        text=True, encoding="utf-8", timeout=5)
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn("Port", result.stderr)
                    self.assertIn("1-65535", result.stderr)

    def test_invalid_frame_lengths(self):
        for length in (0, 1024 * 1024 + 1):
            with self.subTest(length=length):
                server, port = self.server()
                peer = self.peer(port)
                peer.sendall(struct.pack("!I", length))
                self.assertEqual(peer.recv(1), b"")
                _, error = server.communicate(timeout=5)
                self.assertEqual(server.returncode, 1)
                self.assertIn("Invalid frame length", error)

    def test_client_input_eof(self):
        server, port = self.server()
        client = self.client(port, "")
        output, error = server.communicate(timeout=5)
        self.assertEqual(client.returncode, 0, client.stderr)
        self.assertEqual(server.returncode, 0, error)
        self.assertIn("Input ended", client.stdout)
        self.assertIn("Peer closed", output)

    def test_server_input_eof(self):
        server, port = self.server()
        server.stdin.close()
        server.stdin = None
        client = self.client(port, "你好\n")
        output, error = server.communicate(timeout=5)
        self.assertEqual(server.returncode, 0, error)
        self.assertEqual(client.returncode, 0, client.stderr)
        self.assertIn("Input ended", output)
        self.assertIn("Peer closed", client.stdout)

    def test_truncated_frame_reports_error(self):
        server, port = self.server()
        peer = self.peer(port)
        peer.sendall(struct.pack("!I", 10) + b"ab")
        peer.shutdown(socket.SHUT_WR)
        _, error = server.communicate(timeout=5)
        self.assertEqual(server.returncode, 1)
        self.assertIn("Incomplete frame", error)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, default=BIN_DIR)
    arguments, unittest_args = parser.parse_known_args()
    BIN_DIR = arguments.bin_dir.resolve()
    unittest.main(argv=[sys.argv[0], *unittest_args])
