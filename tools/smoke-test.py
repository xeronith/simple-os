#!/usr/bin/env python3
"""Automated smoke test for simple-os.

Boots the kernel in QEMU with a Unix-socket serial port, runs a sequence of
shell commands, and checks the console output. Exits non-zero on failure.
"""

import os
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMPDIR = tempfile.mkdtemp(prefix="simple-os-test-")
SOCKET_PATH = os.path.join(TMPDIR, "serial.sock")

CMD = [
    "qemu-system-aarch64",
    "-M", "virt,gic-version=2",
    "-cpu", "cortex-a57",
    "-m", "128M",
    "-display", "none",
    "-monitor", "none",
    "-serial", f"unix:{SOCKET_PATH},server=on,wait=on",
    "-kernel", "build/kernel.elf",
]

TESTS = [
    ("help", ["commands:", "uptime", "panic"]),
    ("info", ["CurrentEL", "EL1", "timer", "vectors"]),
    ("uptime", ["uptime", "ticks"]),
    ("heap", ["heap", "bytes used"]),
    ("alloc 64", ["allocated 64 bytes"]),
    ("poke 41000000 12345678", ["<- 0x0000000012345678"]),
    ("peek 41000000", ["= 0x0000000012345678"]),
    ("sleep 200", []),
    ("uptime", ["ticks"]),
    ("matrix 1", ["matrix  |  press any key to stop", "simple-os>"]),
    ("panic", ["KERNEL EXCEPTION", "BRK instruction", "kernel halted"]),
]


def main() -> int:
    proc = subprocess.Popen(
        CMD, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )

    try:
        for _ in range(200):
            if os.path.exists(SOCKET_PATH):
                break
            if proc.poll() is not None:
                print("FAIL: QEMU exited before opening the serial socket")
                return 1
            time.sleep(0.02)

        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(SOCKET_PATH)
        sock.setblocking(False)

        def drain(seconds: float) -> bytes:
            data = bytearray()
            deadline = time.time() + seconds
            while time.time() < deadline:
                try:
                    chunk = sock.recv(65536)
                except BlockingIOError:
                    chunk = None
                if chunk:
                    data += chunk
                    deadline = time.time() + 0.2
                else:
                    time.sleep(0.02)
            return bytes(data)

        def send(command: str) -> str:
            sock.sendall((command + "\r").encode())
            return drain(1.3).decode(errors="replace")

        boot = drain(3.5).decode(errors="replace")
        if "ready. type 'help'" not in boot or "simple-os>" not in boot:
            print("FAIL: kernel did not reach the shell prompt")
            print(boot)
            return 1

        failures = 0
        for command, needles in TESTS:
            output = send(command)
            missing = [needle for needle in needles if needle not in output]
            if missing:
                failures += 1
                print(f"FAIL: {command!r} missing {missing}")
                print(output)
            else:
                print(f"PASS: {command}")

        if failures:
            return 1

        print("\nall smoke tests passed")
        return 0
    finally:
        try:
            sock.close()
        except Exception:
            pass
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
