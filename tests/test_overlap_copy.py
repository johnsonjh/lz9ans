#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

import os
from pathlib import Path
import random
import subprocess
import tempfile

from inspect_lz9ans import decode, pack9

ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(os.environ.get("LZ9ANS_BIN", str(ROOT / "lz9ans"))).resolve()


def run(*args):
    result = subprocess.run(
        [str(BINARY), *map(str, args)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=90,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {args!r}\n"
            f"{result.stderr.decode(errors='replace')}"
        )
    return result


def make_periodic_input(window, period, width, seed):
    rng = random.Random(seed)
    prefix = [rng.randrange(1 << width) for _ in range(window)]
    pattern = prefix[-period:]
    repeated = (pattern * ((3 * window + period - 1) // period))[: 3 * window]
    values = prefix + repeated
    return bytes(values) if width == 8 else pack9(values)


def main():
    # These periods select the explicit OF distance-code ranges containing
    # distances 2, 3, 4, 7, 16, and 257.  Three windows of repetition force
    # repeated history-ring wraparound in both window configurations.
    cases = ((2, 5), (3, 5), (4, 6), (7, 6), (16, 8), (257, 12))

    with tempfile.TemporaryDirectory(prefix="lz9ans-overlap-copy-") as temp:
        work = Path(temp)
        source = work / "source"
        archive = work / "archive.lz9a"
        decoded = work / "decoded"

        for window in (32768, 65536):
            window_option = ["--window=64k"] if window == 65536 else []
            for period, distance_code in cases:
                for mode, width in (("c8", 8), ("c9", 9)):
                    raw = make_periodic_input(
                        window,
                        period,
                        width,
                        0x0A11CE + window + period + width,
                    )
                    source.write_bytes(raw)
                    run(mode, source, archive, "-6", *window_option)

                    independent, stats = decode(archive.read_bytes())
                    assert independent == raw, (mode, window, period, "Python decode")
                    assert stats["lz"] > 0, (mode, window, period, "no LZ blocks")
                    assert stats["match_ge19"] > 0, (
                        mode,
                        window,
                        period,
                        "no long matches",
                    )
                    assert stats[f"offset_{distance_code}"] > 0, (
                        mode,
                        window,
                        period,
                        "expected period distance not selected",
                        stats,
                    )

                    run("d", archive, decoded)
                    assert decoded.read_bytes() == raw, (
                        mode,
                        window,
                        period,
                        "C decode",
                    )

    print(
        "PASS overlapping match copies: periods 2/3/4/7/16/257, "
        "32K/64K windows, c8/c9, C/Python decode and ring wrap"
    )


if __name__ == "__main__":
    main()
