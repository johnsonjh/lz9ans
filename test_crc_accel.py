#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson
# SPDX-License-Identifier: MIT

import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent


def pack9(values):
    output = bytearray()
    acc = bits = 0
    for value in values:
        acc = (acc << 9) | value
        bits += 9
        while bits >= 8:
            bits -= 8
            output.append((acc >> bits) & 255)
            acc &= (1 << bits) - 1
    if bits:
        output.append((acc << (8 - bits)) & 255)
    return bytes(output)


def run(binary, *args):
    result = subprocess.run(
        [str(binary), *map(str, args)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=120,
        check=False,
    )
    if result.returncode:
        raise RuntimeError(
            f"{binary} {' '.join(map(str, args))} failed:\n"
            f"{result.stderr.decode(errors='replace')}"
        )


def main():
    rng = random.Random(0x9A32C)
    samples = (
        ("small-8", "c8", bytes(range(251)) * 3),
        ("large-8", "c8", bytes(rng.getrandbits(8) for _ in range(180003))),
        ("small-9", "c9", pack9([rng.randrange(512) for _ in range(512)])),
        (
            "large-9",
            "c9",
            pack9([rng.randrange(512) for _ in range(78000)]),
        ),
    )
    compiler = shlex.split(os.environ.get("CC", "cc"))
    flags = shlex.split(
        os.environ.get("CFLAGS", "-O3 -ansi -pedantic -Wall -Wextra -Werror")
    )
    with tempfile.TemporaryDirectory(prefix="lz9ans-crc-test-") as temporary:
        work = Path(temporary)
        software = work / "lz9ans-software"
        subprocess.run(
            compiler
            + flags
            + [
                "-DLZ9ANS_NO_CRC32_HW",
                "-o",
                str(software),
                str(ROOT / "lz9ans.c"),
                "-lm",
            ],
            cwd=ROOT,
            check=True,
        )
        hardware = Path(os.environ.get("LZ9ANS_BIN", str(ROOT / "lz9ans"))).resolve()
        for name, mode, sample in samples:
            source = work / (name + ".input")
            sw_archive = work / (name + ".software.lz9a")
            hw_archive = work / (name + ".hardware.lz9a")
            source.write_bytes(sample)
            run(software, mode, source, sw_archive, "-6")
            run(hardware, mode, source, hw_archive, "-6")
            if sw_archive.read_bytes() != hw_archive.read_bytes():
                raise AssertionError(f"hardware/software archive mismatch: {name}")
            for binary in (software, hardware):
                decoded = work / (name + ".decoded")
                run(binary, "d", hw_archive, decoded)
                if decoded.read_bytes() != sample:
                    raise AssertionError(f"cross-decode mismatch: {name} with {binary}")
            print("PASS hardware/software CRC equivalence", name)


if __name__ == "__main__":
    main()
