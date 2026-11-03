#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# SPDX-License-Identifier: MIT

from pathlib import Path
import os
import random
import shlex
import subprocess
import tempfile

from inspect_lz9ans import block_header, decode, frame_header, pack9

ROOT = Path(__file__).resolve().parent


def build(binary, threshold):
    compiler = shlex.split(os.environ.get("CC", "cc"))
    flags = shlex.split(
        os.environ.get(
            "CFLAGS",
            "-D_ALL_SOURCE -D_GNU_SOURCE -O3 -ansi -pedantic -Wall -Wextra -Werror",
        )
    )
    subprocess.run(
        compiler
        + flags
        + [
            f"-DLZ9ANS_EARLY_STORED_LITERAL_PERCENT={threshold}",
            "-o",
            str(binary),
            str(ROOT / "lz9ans.c"),
            "-lm",
        ],
        cwd=ROOT,
        check=True,
    )


def run(binary, *args):
    result = subprocess.run(
        [str(binary), *map(str, args)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=120,
        check=False,
    )
    if result.returncode:
        raise AssertionError(
            f"{binary} {' '.join(map(str, args))} failed:\n"
            f"{result.stderr.decode(errors='replace')}"
        )


def block_types(frame):
    position = frame_header(frame)["length"]
    result = []
    while position < len(frame):
        header = block_header(frame, position)
        result.append(header["type"])
        position = header["next_position"]
    assert position == len(frame)
    return result


def main():
    rng = random.Random(0x97_98_9A)
    random9 = pack9([rng.randrange(512) for _ in range(32768)])
    repetitive9 = pack9([0, 1, 2, 3, 4, 5, 6, 7] * 4096)

    with tempfile.TemporaryDirectory(prefix="lz9ans-early-stored-") as temp:
        work = Path(temp)
        source = work / "input"
        archive = work / "archive"
        decoded = work / "decoded"

        for threshold in (97, 98):
            binary = work / f"lz9ans-{threshold}"
            build(binary, threshold)

            for level in (4, 5, 9):
                source.write_bytes(random9)
                run(binary, "c9", source, archive, f"-{level}")
                frame = archive.read_bytes()
                independent, _ = decode(frame)
                assert independent == random9, (threshold, level, "random9")
                run(binary, "d", archive, decoded)
                assert decoded.read_bytes() == random9

                source.write_bytes(repetitive9)
                run(binary, "c9", source, archive, f"-{level}")
                frame = archive.read_bytes()
                independent, _ = decode(frame)
                assert independent == repetitive9, (
                    threshold,
                    level,
                    "repetitive9",
                )
                assert 1 in block_types(frame), (
                    threshold,
                    level,
                    "expected an LZ block",
                    block_types(frame),
                )
                run(binary, "d", archive, decoded)
                assert decoded.read_bytes() == repetitive9

                # A single-symbol literal stream is encoded as RLE, whose
                # cost estimate must not be mistaken for its RAW bit width.
                unary9 = pack9([0] * 32768)
                source.write_bytes(unary9)
                run(binary, "c9", source, archive, f"-{level}")
                frame = archive.read_bytes()
                independent, _ = decode(frame)
                assert independent == unary9, (
                    threshold,
                    level,
                    "unary9",
                )
                run(binary, "d", archive, decoded)
                assert decoded.read_bytes() == unary9

            print(
                "PASS early-STORED threshold",
                threshold,
                "percent (levels 4, 5, 9; independent and C decoding)",
            )


if __name__ == "__main__":
    main()
