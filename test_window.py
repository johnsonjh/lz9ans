#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

from pathlib import Path
import random
import subprocess
import tempfile

from inspect_lz9ans import decode

ROOT = Path(__file__).resolve().parent


def run(binary, action, source, target, *options):
    return subprocess.run(
        [str(binary), action, str(source), str(target), *options],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=120,
    )


def main():
    binary = (ROOT / "lz9ans").resolve()
    rng = random.Random(0x9A32768)
    with tempfile.TemporaryDirectory(prefix="lz9ans-window-") as temp:
        temp = Path(temp)
        source, archive, recovered = (temp / n for n in ("input", "archive", "out"))

        pattern16 = bytes(rng.getrandbits(8) for _ in range(16384))
        sample16 = pattern16 + pattern16
        source.write_bytes(sample16)
        result = run(binary, "c8", source, archive, "-9")
        assert result.returncode == 0, result.stderr.decode()
        encoded16 = archive.read_bytes()
        decoded16, stats16 = decode(encoded16)
        assert decoded16 == sample16
        assert stats16["offset_18"] > 0, stats16
        result = run(binary, "d", archive, recovered)
        assert result.returncode == 0, result.stderr.decode()
        assert recovered.read_bytes() == sample16
        print(
            f"PASS 16K distance: {len(sample16)} -> {len(encoded16)} bytes; "
            f"offset_18={stats16['offset_18']}"
        )

        pattern32 = bytes(rng.getrandbits(8) for _ in range(32768))
        sample32 = pattern32 + pattern32
        source.write_bytes(sample32)
        result = run(binary, "c8", source, archive, "-9")
        assert result.returncode == 0, result.stderr.decode()
        encoded32 = archive.read_bytes()
        decoded32, stats32 = decode(encoded32)
        assert decoded32 == sample32
        assert stats32["offset_19"] > 0, stats32
        result = run(binary, "d", archive, recovered)
        assert result.returncode == 0, result.stderr.decode()
        assert recovered.read_bytes() == sample32
        print(
            f"PASS 32K cross-block distance: {len(sample32)} -> "
            f"{len(encoded32)} bytes; offset_19={stats32['offset_19']}"
        )

        # A v4 frame must not be accepted by the v5 implementation. Recompute
        # the header CRC so rejection is specifically for the version number.
        bad_version = bytearray(encoded32)
        bad_version[4] = 4
        import zlib

        bad_version[16:20] = zlib.crc32(bad_version[:16]).to_bytes(4, "little")
        archive.write_bytes(bad_version)
        result = run(binary, "d", archive, recovered)
        assert result.returncode != 0, "C decoder accepted v4 frame"
        try:
            decode(bytes(bad_version))
        except ValueError:
            pass
        else:
            raise AssertionError("independent decoder accepted v4 frame")
        print("PASS explicit rejection of version 4")


if __name__ == "__main__":
    main()
