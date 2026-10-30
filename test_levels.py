#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

from pathlib import Path
import random
import subprocess
import tempfile

from inspect_lz9ans import decode, pack9

ROOT = Path(__file__).resolve().parent
BINARY = (ROOT / "lz9ans").resolve()


def run(*args):
    return subprocess.run(
        [str(BINARY), *map(str, args)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=90,
    )


def main():
    rng = random.Random(0x3039)
    random8 = bytes(rng.getrandbits(8) for _ in range(4096))
    random9 = pack9([rng.randrange(512) for _ in range(4096)])
    motif = b"DECLARE X FIXED BIN; CALL PRINT(X);\n" * 256
    high9 = pack9([511, 256, 0, 320, 1, 511] * 512)
    samples = [
        ("empty8", "c8", b""),
        ("text8", "c8", motif),
        ("random8", "c8", random8),
        ("random9", "c9", random9),
        ("high9", "c9", high9),
    ]
    with tempfile.TemporaryDirectory(prefix="lz9ans-levels-") as temp:
        temp = Path(temp)
        source, archive, decoded = (temp / n for n in ("in", "archive", "out"))
        for level in range(1, 10):
            for name, mode, raw in samples:
                source.write_bytes(raw)
                result = run(mode, source, archive, f"-{level}")
                assert result.returncode == 0, (level, name, result.stderr.decode())
                frame = archive.read_bytes()
                assert frame[4] == 6, (level, name, "wrong format version")
                independent, _ = decode(frame)
                assert independent == raw, (level, name, "independent decode mismatch")
                result = run("d", archive, decoded)
                assert result.returncode == 0, (level, name, result.stderr.decode())
                assert decoded.read_bytes() == raw, (level, name, "C decode mismatch")
            print(
                f"PASS level {level} (5 independent C/Python round trips)", flush=True
            )
    print("PASS all nine compression levels")


if __name__ == "__main__":
    main()
