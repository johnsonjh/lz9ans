#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson
# SPDX-License-Identifier: MIT
"""Exercise the reference and KL-optimal tANS frequency normalisers."""

import decimal
from functools import lru_cache
import os
import random
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DECIMAL = decimal.Decimal
decimal.getcontext().prec = 90


@lru_cache(maxsize=None)
def ln_consecutive_ratio(j):
    """Return ln(j / (j - 1)) using a convergent Decimal atanh series."""
    z = DECIMAL(1) / DECIMAL(2 * j - 1)
    z_squared = z * z
    term = z
    total = z
    denominator = 3
    threshold = DECIMAL(1).scaleb(-100)

    while True:
        term *= z_squared
        addend = term / denominator
        total += addend
        if addend < threshold:
            return total * 2
        denominator += 2


def build_harness(path, force_32bit=False, bfprt=False):
    cc = os.environ.get("CC", "cc")
    cflags = os.environ.get("CFLAGS", "")
    command = (
        [cc]
        + cflags.split()
        + [
            "-D_ALL_SOURCE",
            "-D_GNU_SOURCE",
            "-O2",
            "-ansi",
            "-pedantic",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-o",
            str(path),
        ]
    )
    if force_32bit:
        command.extend(["-DNORM_FORCE_32BIT_MUL", "-DNORM_FORCE_32BIT_MULDIV"])
    if bfprt:
        command.append("-DNORM_KL_BFPRT")
    command.extend([str(ROOT / "tests" / "normalizer_harness.c"), "-lm"])
    subprocess.run(command, cwd=ROOT, check=True)


def make_cases():
    rng = random.Random(0x9A9A510)
    cases = [
        [1, 1],
        [1, 2],
        [31, 32737],
        [1, 32767],
        [2, 32766],
        [3, 32765],
        [1000, 1000],
        [1] * 512,
        [1 + i for i in range(512)],
        [max(1, 32768 // (i + 1)) for i in range(512)],
        [1, 0x3FFFFE],
        [2, 0x3FFFFD],
        [1] * 511 + [0x3FFFFF - 511],
        [8191] * 512,
    ]

    for active in (2, 3, 4, 8, 16, 32, 64, 128, 256, 512):
        for _ in range(24):
            mode = rng.randrange(4)
            if mode == 0:
                counts = [rng.randint(1, 1000) for _ in range(active)]
            elif mode == 1:
                counts = [max(1, int(10000 / ((i + 1) ** 1.1))) for i in range(active)]
            elif mode == 2:
                counts = [rng.randint(1, 8) for _ in range(active)]
                counts[rng.randrange(active)] += rng.randint(10000, 100000)
            else:
                counts = [1] * active
                for _ in range(rng.randint(active, 4 * active)):
                    counts[rng.randrange(active)] += rng.randint(1, 1000)
            cases.append(counts)

    return cases


def is_kl_optimal(counts, frequencies):
    decrement = []
    increment = []

    for count, frequency in zip(counts, frequencies):
        if frequency > 1:
            decrement.append(DECIMAL(count) * ln_consecutive_ratio(frequency))
        increment.append(DECIMAL(count) * ln_consecutive_ratio(frequency + 1))

    return min(decrement) >= max(increment)


def main():
    cases = make_cases()

    with tempfile.TemporaryDirectory(prefix="lz9ans-normalizer-") as tmp:
        binary = Path(tmp) / "normalizer_harness"
        binary_32 = Path(tmp) / "normalizer_harness_32bit"
        binary_bfprt = Path(tmp) / "normalizer_harness_bfprt"
        build_harness(binary)
        build_harness(binary_32, force_32bit=True)
        build_harness(binary_bfprt, bfprt=True)

        input_lines = []
        for counts in cases:
            input_lines.append(
                f"{len(counts)} {sum(counts)} "
                + " ".join(f"{i} {count}" for i, count in enumerate(counts))
            )

        result = subprocess.run(
            [str(binary)],
            input="\n".join(input_lines) + "\n",
            text=True,
            capture_output=True,
            check=True,
        )
        result_32 = subprocess.run(
            [str(binary_32)],
            input="\n".join(input_lines) + "\n",
            text=True,
            capture_output=True,
            check=True,
        )
        result_bfprt = subprocess.run(
            [str(binary_bfprt)],
            input="\n".join(input_lines) + "\n",
            text=True,
            capture_output=True,
            check=True,
        )

    if result.stdout != result_32.stdout:
        raise SystemExit("FAIL: 32-bit arithmetic fallback differs")

    rows = result.stdout.splitlines()
    rows_bfprt = result_bfprt.stdout.splitlines()
    if len(rows) != len(cases):
        raise SystemExit("FAIL: harness returned an unexpected row count")
    if len(rows_bfprt) != len(cases):
        raise SystemExit("FAIL: BFPRT harness returned an unexpected row count")

    kl_differences = 0

    for case_number, (counts, row, row_bfprt) in enumerate(
        zip(cases, rows, rows_bfprt), 1
    ):
        if not row.startswith("OK"):
            raise SystemExit(f"FAIL: normalizer rejected case {case_number}")

        old_text, fast_text, kl_text = row[2:].split("|")
        old_freq = [int(value) for value in old_text.split()]
        fast_freq = [int(value) for value in fast_text.split()]
        kl_freq = [int(value) for value in kl_text.split()]

        if sum(old_freq) != 1024 or sum(fast_freq) != 1024 or sum(kl_freq) != 1024:
            raise SystemExit(f"FAIL: frequency sum in case {case_number}")
        if any(value < 1 for value in old_freq + fast_freq + kl_freq):
            raise SystemExit(f"FAIL: zero frequency in case {case_number}")
        if old_freq != fast_freq:
            raise SystemExit(f"FAIL: legacy selection mismatch in case {case_number}")
        if not is_kl_optimal(counts, kl_freq):
            raise SystemExit(f"FAIL: KL optimality condition in case {case_number}")

        if not row_bfprt.startswith("OK"):
            raise SystemExit(f"FAIL: BFPRT rejected case {case_number}")
        bfprt_text = row_bfprt[2:].split("|")[2]
        bfprt_freq = [int(value) for value in bfprt_text.split()]
        if (
            sum(bfprt_freq) != 1024
            or any(value < 1 for value in bfprt_freq)
            or not is_kl_optimal(counts, bfprt_freq)
        ):
            raise SystemExit(f"FAIL: BFPRT KL optimality in case {case_number}")

        kl_differences += old_freq != kl_freq

    print(f"PASS: {len(cases)} histograms; sums, support, and KL optimality")
    print("PASS: linear-selection legacy normalizer is byte-for-byte equivalent")
    print("PASS: emulated 32-bit multiply/divide paths match 64-bit paths")
    print("PASS: BFPRT selector also satisfies KL optimality")
    print(f"PASS: KL normalizer differs from legacy on {kl_differences} cases")


if __name__ == "__main__":
    main()
