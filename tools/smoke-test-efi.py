#!/usr/bin/env python3
"""Automated smoke test for the UEFI AArch64 build.

Boots EDK2 under QEMU, selects the ESP once, and exercises the shared shell
through the UEFI console. Exits non-zero on failure.
"""

import os
import socket
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = tempfile.mkdtemp(prefix="simple-os-efi-test-")
ESP = os.path.join(TMP, "esp")
VARS = os.path.join(TMP, "vars.fd")
SOCKET = os.path.join(TMP, "serial.sock")

CODE = os.environ.get("EDK2_CODE")
VARS_SRC = os.environ.get("EDK2_VARS")
if not CODE or not VARS_SRC:
    if sys.platform == "darwin":
        CODE = CODE or "/opt/homebrew/share/qemu/edk2-aarch64-code.fd"
        VARS_SRC = VARS_SRC or "/opt/homebrew/share/qemu/edk2-arm-vars.fd"
    else:
        CODE = CODE or "/usr/share/qemu/edk2-aarch64-code.fd"
        VARS_SRC = VARS_SRC or "/usr/share/qemu/edk2-arm-vars.fd"

for path, label in [(CODE, "EDK2 code"), (VARS_SRC, "EDK2 vars")]:
    if not os.path.exists(path):
        print(f"FAIL: {label} firmware not found: {path}")
        print("Set EDK2_CODE and EDK2_VARS to your AArch64 EDK2 firmware files.")
        sys.exit(1)

os.makedirs(os.path.join(ESP, "EFI", "BOOT"), exist_ok=True)
shutil.copy(os.path.join(ROOT, "build", "BOOTAA64.EFI"),
            os.path.join(ESP, "EFI", "BOOT", "BOOTAA64.EFI"))
shutil.copy(VARS_SRC, VARS)

CMD = [
    "qemu-system-aarch64",
    "-M", "virt,gic-version=2",
    "-cpu", "cortex-a57",
    "-m", "512M",
    "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={CODE}",
    "-drive", f"if=pflash,format=raw,unit=1,file={VARS}",
    "-drive", f"if=none,id=esp,file=fat:rw:{ESP},format=raw",
    "-device", "virtio-blk-pci,drive=esp",
    "-display", "none",
    "-monitor", "none",
    "-serial", f"unix:{SOCKET},server=on,wait=on",
]

TESTS = [
    ("help", ["commands:", "panic"]),
    ("info", ["UEFI firmware (AArch64)", "CurrentEL", "heap"]),
    ("uptime", ["uptime", "ticks)"]),
    ("heap", ["1048576"]),
    ("alloc 64", ["allocated 64 bytes"]),
    ("sleep 200", []),
    ("uptime", ["ticks)"]),
    ("matrix 1", ["matrix  |  press any key to stop", "simple-os>"]),
    ("panic", ["panic requested; halting."]),
]

proc = subprocess.Popen(CMD, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

try:
    for _ in range(300):
        if os.path.exists(SOCKET):
            break
        if proc.poll() is not None:
            print("FAIL: QEMU exited before opening the serial socket")
            sys.exit(1)
        time.sleep(0.02)

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(SOCKET)
    sock.setblocking(False)

    def read(seconds: float) -> str:
        data = bytearray()
        deadline = time.time() + seconds
        while time.time() < deadline:
            try:
                chunk = sock.recv(65536)
            except BlockingIOError:
                chunk = None
            if chunk:
                data += chunk
                deadline = time.time() + 0.4
            else:
                time.sleep(0.05)
        return data.decode(errors="replace")

    def command(text: str) -> str:
        sock.sendall((text + "\r").encode())
        return read(1.3)

    boot = read(8.0)
    sock.sendall(b"\r")

    deadline = time.time() + 15.0
    transcript = boot
    while "simple-os>" not in transcript and time.time() < deadline:
        transcript += read(1.0)

    if "simple-os>" not in transcript:
        print("FAIL: UEFI build did not reach the shell prompt")
        print(transcript)
        sys.exit(1)

    failures = 0
    for name, needles in TESTS:
        output = command(name)
        missing = [needle for needle in needles if needle not in output]
        if missing:
            failures += 1
            print(f"FAIL: {name!r} missing {missing}")
            print(output)
        else:
            print(f"PASS: {name}")

    if failures:
        sys.exit(1)

    print("\nall UEFI smoke tests passed")
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
    if proc.stderr:
        err = proc.stderr.read().decode(errors="replace")
        if "error" in err.lower():
            print(err)
