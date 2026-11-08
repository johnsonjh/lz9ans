#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

"""Exercise the CLI's file, stdin, and stdout paths for both input modes."""

import argparse
import os
from pathlib import Path
import random
import subprocess
import tempfile

BLOCK_N = 32768


def pack9(values):
    """Pack 9-bit symbols MSB first, padding the final octet with zero bits."""
    output = bytearray()
    accumulator = 0
    bits = 0

    for value in values:
        if not 0 <= value < 512:
            raise ValueError("9-bit value out of range")
        accumulator = (accumulator << 9) | value
        bits += 9
        while bits >= 8:
            bits -= 8
            output.append((accumulator >> bits) & 0xFF)
            accumulator &= (1 << bits) - 1

    if bits:
        output.append((accumulator << (8 - bits)) & 0xFF)

    return bytes(output)


def invoke(binary, arguments, input_data=None):
    result = subprocess.run(
        [binary] + [str(argument) for argument in arguments],
        input=input_data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=120,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            "command failed: {}\nreturn code: {}\nstderr: {}".format(
                " ".join([binary] + [str(a) for a in arguments]),
                result.returncode,
                result.stderr.decode("utf-8", "replace"),
            )
        )
    return result.stdout


def check_frame(data, mode, unknown_count, symbol_count):
    if len(data) < 17 or data[:4] != b"LZ9A":
        raise AssertionError("encoder did not write a complete LZ9A frame")
    if data[4] != 9 or data[5] != mode:
        raise AssertionError("unexpected frame version or input mode")

    header_length = int.from_bytes(data[6:8], "little")
    total = int.from_bytes(data[8:12], "little")
    if unknown_count:
        if header_length != 20 or total != 0:
            raise AssertionError("stdin encoder did not write an unknown-count frame")
        if data[13:16] != b"\x81\x00\x00":
            raise AssertionError("unknown-count frame TLV is missing or malformed")
        if data[-1:] != b"\x02":
            raise AssertionError("unknown-count frame has no terminal END record")
    else:
        if header_length != 17 or total != symbol_count:
            raise AssertionError("file encoder did not write a known-count frame")


def exercise_decode(
    binary, directory, mode, source_data, encoded_known, encoded_unknown, label
):
    source_path = directory / (label + ".lz9a")
    output_path = directory / (label + ".out")

    # Known-count frame: compressed file and stdin, each to file and stdout.
    source_path.write_bytes(encoded_known)
    stdout_data = invoke(binary, ["d", source_path, output_path])
    if stdout_data:
        raise AssertionError(label + ": file decode wrote payload to stdout")
    if output_path.read_bytes() != source_data:
        raise AssertionError(label + ": known file-to-file decode mismatch")

    stdout_data = invoke(binary, ["d", source_path, "-"])
    if stdout_data != source_data:
        raise AssertionError(label + ": known file-to-stdout decode mismatch")

    stdout_data = invoke(binary, ["d", "-", output_path], encoded_known)
    if stdout_data:
        raise AssertionError(label + ": stdin-to-file decode wrote to stdout")
    if output_path.read_bytes() != source_data:
        raise AssertionError(label + ": known stdin-to-file decode mismatch")

    stdout_data = invoke(binary, ["d", "-", "-"], encoded_known)
    if stdout_data != source_data:
        raise AssertionError(label + ": known stdin-to-stdout decode mismatch")

    # Unknown-count frame: the decoder must honor END on both file and pipe.
    source_path.write_bytes(encoded_unknown)
    stdout_data = invoke(binary, ["d", source_path, output_path])
    if stdout_data:
        raise AssertionError(label + ": unknown file decode wrote payload to stdout")
    if output_path.read_bytes() != source_data:
        raise AssertionError(label + ": unknown file-to-file decode mismatch")

    stdout_data = invoke(binary, ["d", source_path, "-"])
    if stdout_data != source_data:
        raise AssertionError(label + ": unknown file-to-stdout decode mismatch")

    stdout_data = invoke(binary, ["d", "-", output_path], encoded_unknown)
    if stdout_data:
        raise AssertionError(label + ": unknown stdin-to-file decode wrote to stdout")
    if output_path.read_bytes() != source_data:
        raise AssertionError(label + ": unknown stdin-to-file decode mismatch")

    stdout_data = invoke(binary, ["d", "-", "-"], encoded_unknown)
    if stdout_data != source_data:
        raise AssertionError(label + ": unknown stdin-to-stdout decode mismatch")


def exercise_mode(binary, directory, mode, sample, label):
    source_path = directory / (label + ".in")
    encoded_path = directory / (label + ".known.lz9a")
    unknown_path = directory / (label + ".unknown.lz9a")
    source_path.write_bytes(sample)
    symbol_count = len(sample) if mode == 8 else (len(sample) * 8) // 9

    # File-to-file remains the known-count baseline.
    stdout_data = invoke(binary, ["c" + str(mode), source_path, encoded_path, "-1"])
    if stdout_data:
        raise AssertionError(label + ": file compression wrote payload to stdout")
    encoded_known = encoded_path.read_bytes()
    check_frame(encoded_known, mode, False, symbol_count)

    # File-to-stdout must preserve the known-count representation.
    encoded_stdout = invoke(binary, ["c" + str(mode), source_path, "-", "-1"])
    if encoded_stdout != encoded_known:
        raise AssertionError(label + ": file-to-stdout encoding differs")

    # Stdin-to-file and stdin-to-stdout use the unknown-count representation.
    stdout_data = invoke(binary, ["c" + str(mode), "-", unknown_path, "-1"], sample)
    if stdout_data:
        raise AssertionError(label + ": stdin-to-file compression wrote to stdout")
    encoded_unknown = unknown_path.read_bytes()
    check_frame(encoded_unknown, mode, True, symbol_count)

    encoded_pipe = invoke(binary, ["c" + str(mode), "-", "-", "-1"], sample)
    if encoded_pipe != encoded_unknown:
        raise AssertionError(label + ": stdin-to-stdout encoding differs")

    exercise_decode(
        binary, directory, mode, sample, encoded_known, encoded_unknown, label
    )


def exercise_path_aliases(binary, directory):
    source_path = directory / "path-alias-input.bin"
    original = b"do not truncate this source\n"
    source_path.write_bytes(original)

    aliases = (
        (directory / "path-alias-hardlink.bin", os.link),
        (directory / "path-alias-symlink.bin", os.symlink),
    )

    for alias_path, create_alias in aliases:
        try:
            create_alias(source_path, alias_path)
        except (NotImplementedError, OSError):
            if create_alias is os.link:
                raise
            continue

        result = subprocess.run(
            [binary, "c8", str(source_path), str(alias_path), "-1"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
            check=False,
        )
        if result.returncode == 0:
            raise AssertionError("CLI accepted an input/output file alias")
        if source_path.read_bytes() != original:
            raise AssertionError("CLI truncated input through an output alias")

        alias_path.unlink()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", default="./lz9ans")
    args = parser.parse_args()
    binary = str(Path(args.bin).resolve())
    rng = random.Random(0x9A097)

    samples = (
        ("empty", b"", []),
        (
            "multi-block",
            bytes(rng.randrange(256) for _ in range(2 * BLOCK_N + 37)),
            [rng.randrange(512) for _ in range(2 * BLOCK_N + 37)],
        ),
    )

    with tempfile.TemporaryDirectory(prefix="lz9ans-stdio-") as temporary:
        directory = Path(temporary)
        for label, octets, symbols in samples:
            exercise_mode(binary, directory, 8, octets, "c8-" + label)
            exercise_mode(binary, directory, 9, pack9(symbols), "c9-" + label)

        # Exercise each possible final packed-9 padding count on the streaming
        # encoder path, not just on the known-length file path.
        for count in range(1, 9):
            source = pack9(list(range(count)))
            encoded = invoke(binary, ["c9", "-", "-", "-1"], source)
            decoded = invoke(binary, ["d", "-", "-"], encoded)
            if decoded != source:
                raise AssertionError(
                    "stdin/stdout packed-9 tail mismatch for {} symbols".format(count)
                )

        exercise_path_aliases(binary, directory)

        # Noncanonical packed-9 EOF padding and an incomplete symbol must fail.
        valid_one = bytearray(pack9([1]))
        valid_one[-1] |= 1
        for malformed in (bytes(valid_one), b"\x00"):
            result = subprocess.run(
                [binary, "c9", "-", "-", "-1"],
                input=malformed,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=30,
                check=False,
            )
            if result.returncode == 0:
                raise AssertionError("stdin c9 accepted malformed final padding")

    print(
        "PASS: stdin/stdout combinations (c8, c9, decode; empty, multi-block, "
        "all packed-9 tail paddings, malformed-tail rejection, and path-alias "
        "protection)"
    )


if __name__ == "__main__":
    main()
