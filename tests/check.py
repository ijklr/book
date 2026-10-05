#!/usr/bin/env python3
"""Behavioral tests; only Python's standard library is required."""
import contextlib
import os
from pathlib import Path
import random
import selectors
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = (ROOT / (sys.argv.pop(1) if len(sys.argv) > 1 else "build")).resolve()


def run(name, *args, data=b"", **kwargs):
    return subprocess.run([str(BUILD / name), *map(str, args)], input=data,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=10, **kwargs)


def ready_line(process, timeout=10):
    deadline = time.monotonic() + timeout
    line = bytearray()
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        while len(line) < 4096:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not selector.select(remaining):
                raise AssertionError("child did not announce readiness before deadline")
            # Read only the available byte, not a possibly incomplete line.
            # A single owner reads this pipe; no other reader can consume it.
            byte = os.read(process.stdout.fileno(), 1)
            if not byte:
                raise AssertionError("child closed stdout before a complete readiness line")
            line.extend(byte)
            if byte == b"\n":
                return bytes(line)
    raise AssertionError("child readiness line exceeds 4096 bytes")


@contextlib.contextmanager
def running(name):
    process = subprocess.Popen([str(BUILD / name)], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    try:
        yield process
    finally:
        if process.poll() is None:
            process.kill()
        process.communicate(timeout=10)


def receive_exact(connection, length):
    result = bytearray()
    while len(result) < length:
        part = connection.recv(length - len(result))
        if not part:
            raise AssertionError("unexpected EOF in echo reply")
        result.extend(part)
    return bytes(result)


def oracle(pattern, data, label=b"(stdin)"):
    return b"".join(label + b":" + str(number).encode() + b"\n"
                    for number, line in enumerate(data.split(b"\n"), 1)
                    if pattern in line)


class SiftTests(unittest.TestCase):
    def check_input(self, pattern, data):
        result = run("sift", pattern.decode("ascii"), data=data)
        expected = oracle(pattern, data)
        self.assertEqual(result.stdout, expected, result.stderr)
        self.assertEqual(result.returncode, 0 if expected else 1, result.stderr)

    def test_empty_binary_and_line_semantics(self):
        for data in (b"", b"needle", b"needle\n", b"\nneedle\n\n",
                     b"nee\ndle", b"\0needle\0needle\nnone\nneedle",
                     b"needle\r\nneedle", b"no matches"):
            with self.subTest(data=data):
                self.check_input(b"needle", data)

    def test_read_boundaries_and_large_line(self):
        for offset in range(8185, 8200):
            self.check_input(b"needle", b"x" * offset + b"needle\n")
        self.check_input(b"needle", b"x" * 200000 + b"needle")

    def test_seeded_inputs_against_independent_oracle(self):
        rng = random.Random(20260929)
        for _ in range(100):
            pattern = bytes(rng.choice(b"abc") for _ in range(rng.randint(1, 12)))
            data = bytes(rng.choice(b"aabc\n\0") for _ in range(rng.randint(0, 20000)))
            self.check_input(pattern, data)

    def test_invalid_patterns_and_maximum_pattern(self):
        self.assertEqual(run("sift").returncode, 2)
        for pattern in ("", "a\nb", "x" * 4097):
            self.assertEqual(run("sift", pattern).returncode, 2)
        self.check_input(b"x" * 4096, b"x" * 4096)

    def test_multiple_files_reset_state_and_error_precedence(self):
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "one"
            second = Path(directory) / "two"
            first.write_bytes(b"needle\nnee")
            second.write_bytes(b"dle\nneedle")
            expected = (f"{first}:1\n{second}:2\n").encode()
            result = run("sift", "needle", first, second)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, expected)
            result = run("sift", "needle", first, Path(directory) / "missing", second)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, expected)
            self.assertIn(b"missing", result.stderr)

    def test_escaped_label(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "name:\n\\"
            path.write_bytes(b"needle")
            result = run("sift", "needle", path)
            expected = (directory + "/name\\x3a\\x0a\\x5c:1\n").encode()
            self.assertEqual(result.stdout, expected)
            self.assertEqual(result.returncode, 0)

    def test_explicit_stdin(self):
        result = run("sift", "needle", "-", data=b"needle")
        self.assertEqual(result.stdout, b"(stdin):1\n")
        self.assertEqual(result.returncode, 0)

    def test_output_failure(self):
        reader, writer = os.pipe()
        os.close(reader)
        try:
            result = subprocess.run([str(BUILD / "sift"), "needle"], input=b"needle",
                                    stdout=writer, stderr=subprocess.PIPE, timeout=10)
        finally:
            os.close(writer)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn(b"stdout", result.stderr)


class ExampleTests(unittest.TestCase):
    def test_identity(self):
        result = run("identity", "7")
        self.assertEqual(result.returncode, 7)
        self.assertIn(b"requested-status=7", result.stdout)
        self.assertEqual(run("identity", "126").returncode, 2)

    def test_binary_copy_and_buffer(self):
        for name in ("copyfd", "buffer"):
            for data in (b"", bytes(range(256)) * 1025):
                result = run(name, data=data)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, data)

    def test_copy_output_failure(self):
        reader, writer = os.pipe()
        os.close(reader)
        try:
            result = subprocess.run([str(BUILD / "copyfd")], input=b"hello",
                                    stdout=writer, stderr=subprocess.PIPE, timeout=10)
        finally:
            os.close(writer)
        self.assertEqual(result.returncode, 1)

    def test_fileinfo(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "file"
            path.write_bytes(b"abc")
            link = Path(directory) / "link"
            link.symlink_to(path)
            result = run("fileinfo", link)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(b"name: type=symlink", result.stdout)
            self.assertIn(b"target: type=regular", result.stdout)
            result = run("fileinfo", directory)
            self.assertEqual(result.returncode, 0)
            self.assertIn(b'"file"', result.stdout)
            self.assertIn(b'"link"', result.stdout)
            self.assertEqual(run("fileinfo", Path(directory) / "missing").returncode, 1)

    def test_runner(self):
        self.assertEqual(run("run", "sh", "-c", "exit 7").returncode, 7)
        self.assertEqual(run("run", "/a/nonexistent/book-command").returncode, 127)
        result = run("run", "printf", "%s", "hello")
        self.assertEqual(result.stdout, b"hello")
        self.assertEqual(result.returncode, 0)

    def test_pipeline_stream_and_status(self):
        result = run("pipeline", "printf", "%s", "hello", "--", "wc", "-c")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), b"5")
        result = run("pipeline", str(BUILD / "copyfd"), "--", "wc", "-c",
                     data=b"x" * 262144)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), b"262144")
        result = run("pipeline", "sh", "-c", "exit 7", "--", "sh", "-c", "exit 9")
        self.assertEqual(result.returncode, 9)

    def test_signal_wait(self):
        for number, name in ((signal.SIGINT, "SIGINT"), (signal.SIGTERM, "SIGTERM")):
            with running("signal_wait") as process:
                self.assertTrue(ready_line(process).startswith(b"ready pid="))
                process.send_signal(number)
                output, errors = process.communicate(timeout=10)
                self.assertEqual(process.returncode, 0, errors)
                self.assertIn(("received " + name).encode(), output)

    def test_mapping(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bytes"
            path.write_bytes(b"a\0aba\0")
            for byte, expected in ((0, b"2\n"), (97, b"3\n"), (255, b"0\n")):
                result = run("mapcount", path, byte)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, expected)
            path.write_bytes(b"")
            self.assertEqual(run("mapcount", path, 0).stdout, b"0\n")
            self.assertEqual(run("mapcount", path, 256).returncode, 2)
            self.assertEqual(run("mapcount", directory, 0).returncode, 1)

    def test_threads_and_poll(self):
        result = run("threads")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"count=100000 expected=100000\n")
        result = run("pollpipe")
        self.assertEqual(result.returncode, 0, result.stderr)
        checksum = sum(i % 251 for i in range(65536))
        self.assertEqual(result.stdout, f"bytes=65536 checksum={checksum}\n".encode())


class NetworkTests(unittest.TestCase):
    def test_client_server(self):
        for message in ("", "hello across a stream", "x" * 4096):
            with running("echo_server") as process:
                port = int(ready_line(process).split()[1])
                result = run("echo_client", port, message)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, message.encode() + b"\n")
                _, errors = process.communicate(timeout=10)
                self.assertEqual(process.returncode, 0, errors)

    def test_binary_and_fragmented_frame(self):
        with running("echo_server") as process:
            port = int(ready_line(process).split()[1])
            body = b"a\0b\xff" * 700
            frame = struct.pack("!I", len(body)) + body
            with socket.create_connection(("127.0.0.1", port), timeout=10) as connection:
                for offset in range(0, len(frame), 3):
                    connection.sendall(frame[offset:offset + 3])
                self.assertEqual(receive_exact(connection, len(frame)), frame)
            _, errors = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 0, errors)

    def test_truncated_and_oversized_frames(self):
        frames = (b"\x00\x00", struct.pack("!I", 5) + b"ab", struct.pack("!I", 4097))
        for frame in frames:
            with running("echo_server") as process:
                port = int(ready_line(process).split()[1])
                with socket.create_connection(("127.0.0.1", port), timeout=10) as connection:
                    connection.sendall(frame)
                    connection.shutdown(socket.SHUT_WR)
                    while connection.recv(4096):
                        pass
                _, errors = process.communicate(timeout=10)
                self.assertNotEqual(process.returncode, 0)
                self.assertTrue(errors)

    def test_clean_eof_and_invalid_client_arguments(self):
        with running("echo_server") as process:
            port = int(ready_line(process).split()[1])
            with socket.create_connection(("127.0.0.1", port), timeout=10):
                pass
            _, errors = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 0, errors)
        for port in ("0", "65536", "-1", "abc"):
            self.assertNotEqual(run("echo_client", port, "test").returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
