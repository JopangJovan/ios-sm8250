#!/usr/bin/env python3
"""Add boot-time I2C tracing to a COPY of an Android v0-v2 recovery image.

Only the two cmdline header fields change; kernel, ramdisk, DTB, ID and
all other bytes are preserved. The Android v0-v2 ID hashes payloads/sizes,
not cmdline. This is for temporary fastboot boot on an unlocked device.
Never flash the tracing image; device dumps/output stay outside git.
"""
import pathlib
import struct
import sys


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: umi-recovery-trace.py INPUT.img OUTPUT.img")
    src, dst = map(pathlib.Path, sys.argv[1:])
    if src.resolve() == dst.resolve():
        raise SystemExit("refusing to overwrite source")
    if dst.exists():
        raise SystemExit("output already exists")
    original = src.read_bytes()
    data = bytearray(original)
    if len(data) < 1660 or data[:8] != b"ANDROID!":
        raise SystemExit("not an Android boot image")
    version = struct.unpack_from("<I", data, 40)[0]
    if version > 2:
        raise SystemExit("only v0-v2 supported")
    cmdline = bytes(data[64:576]).split(b"\0")[0]
    cmdline += bytes(data[608:1632]).split(b"\0")[0]
    if b"trace_event=" in cmdline:
        raise SystemExit("existing tracing parameters; inspect manually")
    cmdline += b" trace_event=i2c:* trace_buf_size=8192K trace_clock=global"
    if len(cmdline) >= 1536:
        raise SystemExit("cmdline exceeds header capacity")
    data[64:576] = cmdline[:512].ljust(512, b"\0")
    data[608:1632] = cmdline[512:].ljust(1024, b"\0")
    assert data[:64] == original[:64]
    assert data[576:608] == original[576:608]
    assert data[1632:] == original[1632:]
    dst.write_bytes(data)
    print(f"header v{version}; {len(cmdline)} cmdline bytes; payload unchanged")


if __name__ == "__main__":
    main()
