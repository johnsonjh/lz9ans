#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

import argparse
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import time
import zlib

from inspect_lz9ans import decode as independent_decode, parse_descriptor


def scan_modes(data):
    assert data[:4] == b"LZ9A"
    pos = 20
    flags = struct.unpack_from("<I", data, 12)[0]
    extra_ans = bool(flags & 2)
    found = {"stored": 0, "lz": 0, "tans": 0, "reuse": 0, "raw": 0, "rle": 0}
    tables = [None] * 5
    kinds = (0, 1, 2, 3, 4) if extra_ans else (0, 1, 2, 3)
    while pos < len(data):
        h = data[pos : pos + 48]
        assert len(h) == 48 and h[:4] == b"LZ9B"
        flag = h[5] & 3
        desc_len = struct.unpack_from("<I", h, 20)[0]
        lengths = struct.unpack_from("<5I", h, 24)
        if flag == 0:
            found["stored"] += 1
        else:
            found["lz"] += 1
            desc = data[pos + 48 : pos + 48 + desc_len]
            index = 0
            for kind in kinds:
                alphabet = (
                    512
                    if kind == 0
                    else (
                        2
                        if kind == 4
                        else 21 if flags & 1 and kind == 3 else 20 if kind == 3 else 32
                    )
                )
                descriptor, index = parse_descriptor(
                    desc, index, kind, alphabet, tables
                )
                mode = descriptor[0]
                if mode == 1:
                    found["raw"] += 1
                elif mode == 2:
                    found["rle"] += 1
                elif mode in (3, 4):
                    found["tans"] += 1
                    if mode == 4:
                        found["reuse"] += 1
            assert index == len(desc)
        pos += 48 + desc_len + sum((x + 7) // 8 for x in lengths)
    assert pos == len(data)
    return found


def pack9(values):
    output = bytearray()
    acc = bits = 0
    for value in values:
        assert 0 <= value < 512
        acc = (acc << 9) | value
        bits += 9
        while bits >= 8:
            bits -= 8
            output.append((acc >> bits) & 255)
            acc &= (1 << bits) - 1
    if bits:
        output.append((acc << (8 - bits)) & 255)
    return bytes(output)


def run(binary, action, source, target, level_arg=None):
    start = time.perf_counter()
    command = [binary, action, str(source), str(target)]
    if level_arg is not None:
        command.append(level_arg)
    result = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=90,
        check=False,
    )
    elapsed = time.perf_counter() - start
    return result, elapsed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", default="./lz9ans")
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--fuzz", action="store_true")
    parser.add_argument(
        "--independent",
        action="store_true",
        help="verify encoded files with the Python-only decoder",
    )
    parser.add_argument(
        "--level",
        choices=tuple(str(n) for n in range(1, 10)) + ("base", "lazy", "fast"),
        default="6",
    )
    args = parser.parse_args()
    binary = str(Path(args.bin).resolve())
    rng = random.Random(914524)
    tests = [
        ("empty", 8, b""),
        ("single-zero", 8, b"\x00"),
        ("single-ff", 8, b"\xff"),
        ("abc", 8, b"ABC"),
        ("one-zero-block", 8, b"\x00" * 32768),
        ("two-zero-blocks", 8, b"\x00" * 65536),
        ("all-octets", 8, bytes(range(256)) * 256),
        ("text-repeating", 8, b"DECLARE X FIXED BIN; CALL PRINT(X);\n" * 4000),
        ("literal-runs", 8, (b"Z" * 390 + b"Q" * 700 + b"X" * 230) * 80),
        (
            "alternating-matches",
            8,
            (
                b"ABRACADABRA-1234567"
                + b"--noise--"
                + b"ABRACADABRA-1234567"
                + b"***noise***"
            )
            * 1400,
        ),
        ("binary-elf-mock", 8, b"\x7fELF" + bytes(range(256)) * 500),
        ("random-8", 8, bytes(rng.getrandbits(8) for _ in range(170001))),
        ("9bit-full-alphabet", 9, pack9(list(range(512)) * 130)),
        ("9bit-high", 9, pack9([511] * 140000)),
        ("9bit-narrow", 9, pack9([v & 127 for v in range(120003)])),
        ("9bit-random", 9, pack9([rng.randrange(512) for _ in range(135001)])),
        ("9bit-repeating", 9, pack9([256, 511, 0, 320, 1, 511] * 18000)),
        (
            "9bit-mixed-tans",
            9,
            pack9(
                [rng.randrange(512) for _ in range(23000)]
                + [rng.randrange(500) for _ in range(13000)]
                + [rng.randrange(48, 350) for _ in range(45000)]
            ),
        ),
    ]
    for path in ("/bin/ls", "/bin/bash"):
        if Path(path).is_file():
            tests.append(("ELF " + path, 8, Path(path).read_bytes()))
    with tempfile.TemporaryDirectory(prefix="lz9ans-test-") as work:
        work = Path(work)
        for name, width, sample in tests:
            source, encoded, decoded = (work / f for f in ("in", "enc", "out"))
            source.write_bytes(sample)
            suffix = {"base": "", "lazy": "l", "fast": "f"}.get(args.level, "")
            level_arg = None if suffix or args.level == "base" else "-" + args.level
            r, ct = run(binary, "c" + str(width) + suffix, source, encoded, level_arg)
            if r.returncode:
                raise AssertionError(f"{name} compression failure: {r.stderr.decode()}")
            r, dt = run(binary, "d", encoded, decoded)
            if r.returncode:
                raise AssertionError(
                    f"{name} decompression failure: {r.stderr.decode()}"
                )
            if decoded.read_bytes() != sample:
                raise AssertionError(f"{name}: decoded data differs")
            code = encoded.read_bytes()
            if args.independent:
                independently_decoded, offset_stats = independent_decode(code)
                if independently_decoded != sample:
                    raise AssertionError(f"{name}: independent decoder mismatch")
                if name == "one-zero-block" and not offset_stats["offset_3"]:
                    raise AssertionError("the dedicated RUN opcode was not emitted")
                if name == "literal-runs" and not sum(
                    offset_stats[f"offset_{slot}"] for slot in range(3)
                ):
                    raise AssertionError("no repeat-distance slot was used")
                if args.benchmark:
                    print(
                        "   independent offset codes:",
                        {
                            k: v
                            for k, v in offset_stats.items()
                            if k.startswith("offset_")
                        },
                    )
            if name != "empty":
                for index in (5, 20, max(0, len(code) - 1)):
                    corrupt = bytearray(code)
                    corrupt[index] ^= 0x80
                    encoded.write_bytes(corrupt)
                    r, _ = run(binary, "d", encoded, decoded)
                    if r.returncode == 0:
                        raise AssertionError(
                            f"{name} accepted corrupt stream at {index}"
                        )
                encoded.write_bytes(code)
            # Full mutation/truncation fuzzing is concentrated on four
            # representative stream shapes to keep `make test` practical.
            # Every case above still receives deterministic header/payload
            # bit flips; selected cases receive 100 mutations and 20 truncations.
            if (
                args.fuzz
                and name
                in ("text-repeating", "random-8", "9bit-mixed-tans", "ELF /bin/ls")
                and len(code) > 64
            ):
                for _ in range(100):
                    corrupt = bytearray(code)
                    where = rng.randrange(5, len(corrupt))
                    corrupt[where] ^= 1 << rng.randrange(8)
                    encoded.write_bytes(corrupt)
                    r, _ = run(binary, "d", encoded, decoded)
                    if r.returncode == 0 and decoded.read_bytes() != sample:
                        raise AssertionError(
                            f"{name} undetected data corruption at {where}"
                        )
                for _ in range(20):
                    prefix = rng.randrange(5, len(code))
                    encoded.write_bytes(code[:prefix])
                    r, _ = run(binary, "d", encoded, decoded)
                    if r.returncode == 0:
                        raise AssertionError(
                            f"{name} accepted truncated stream at {prefix}"
                        )
                encoded.write_bytes(code)
            if args.benchmark:
                zsizes = {}
                ztimes = {}
                for window_bits, label in ((13, "zlib8k"), (15, "zlib32k")):
                    t = time.perf_counter()
                    encoder = zlib.compressobj(6, zlib.DEFLATED, window_bits)
                    compressed = encoder.compress(sample) + encoder.flush()
                    zc = time.perf_counter() - t
                    t = time.perf_counter()
                    assert zlib.decompress(compressed, window_bits) == sample
                    zd = time.perf_counter() - t
                    zsizes[label] = len(compressed)
                    ztimes[label] = (zc, zd)
                print(
                    f"{name:22s} bits={width} input={len(sample):9d} "
                    f"lz9ans={len(code):9d} zlib8k={zsizes['zlib8k']:9d} "
                    f"zlib32k={zsizes['zlib32k']:9d} "
                    f"lz9ans-c={ct:.4f}s lz9ans-d={dt:.4f}s "
                    f"zlib8k-c={ztimes['zlib8k'][0]:.4f}s "
                    f"zlib32k-c={ztimes['zlib32k'][0]:.4f}s"
                )
                print("   modes:", scan_modes(code))
            else:
                print("PASS", name)
    print("PASS all", len(tests), "regression cases")


if __name__ == "__main__":
    main()
