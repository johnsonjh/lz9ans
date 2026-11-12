#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import zlib

from inspect_lz9ans import FormatError, decode, pack9

ROOT = Path(__file__).resolve().parent
BINARY = Path(os.environ.get("LZ9ANS_BIN", str(ROOT / "../lz9ans"))).resolve()


def pack_bits(values, width):
    result = bytearray()
    acc = bits = 0
    for value in values:
        assert 0 <= value < (1 << width)
        acc = (acc << width) | value
        bits += width
        while bits >= 8:
            bits -= 8
            result.append((acc >> bits) & 255)
            acc &= (1 << bits) - 1
    if bits:
        result.append((acc << (8 - bits)) & 255)
    return bytes(result)


def frame_header(mode, count):
    header = bytearray(
        b"LZ9A"
        + bytes((9, mode))
        + struct.pack("<H", 17)
        + struct.pack("<I", count)
        + b"\x00"
    )
    header += struct.pack("<I", zlib.crc32(header))
    return bytes(header)


def stored_block(values, width):
    payload = pack_bits(values, width)
    header = bytes((width << 2,)) + struct.pack("<H", len(values))
    header += struct.pack("<I", zlib.crc32(pack9(values)))
    return header + payload


def run_decode(frame, directory, name, expected_ok=True):
    archive = directory / f"{name}.lz9a"
    output = directory / f"{name}.out"
    archive.write_bytes(frame)
    result = subprocess.run(
        [str(BINARY), "d", str(archive), str(output)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if expected_ok:
        assert result.returncode == 0, result.stderr.decode(errors="replace")
    else:
        assert result.returncode != 0, name
        assert not output.exists() or output.stat().st_size == 0, name
    return result, output


def main():
    rng = random.Random(0x53544F52)
    with tempfile.TemporaryDirectory(prefix="lz9ans-stored-bulk-") as temp:
        work = Path(temp)

        # Widths 7, 8, and 9, including short tails and a full-window write
        # that wraps the history ring after a nonzero starting position.
        mode8_blocks = [
            ([rng.randrange(128) for _ in range(12345)], 7),
            ([rng.randrange(256) for _ in range(32768)], 9),
            ([rng.randrange(256) for _ in range(19)], 8),
        ]
        mode8_values = [value for values, _ in mode8_blocks for value in values]
        mode8_frame = frame_header(8, len(mode8_values)) + b"".join(
            stored_block(values, width) for values, width in mode8_blocks
        )
        independent, _ = decode(mode8_frame)
        assert independent == bytes(mode8_values)
        _, output = run_decode(mode8_frame, work, "mode8-mixed-widths")
        assert output.read_bytes() == bytes(mode8_values)

        mode9_blocks = [
            ([rng.randrange(512) for _ in range(32767)], 9),
            ([rng.randrange(128) for _ in range(13)], 7),
        ]
        mode9_values = [value for values, _ in mode9_blocks for value in values]
        mode9_expected = pack9(mode9_values)
        mode9_frame = frame_header(9, len(mode9_values)) + b"".join(
            stored_block(values, width) for values, width in mode9_blocks
        )
        independent, _ = decode(mode9_frame)
        assert independent == mode9_expected
        _, output = run_decode(mode9_frame, work, "mode9-mixed-widths")
        assert output.read_bytes() == mode9_expected

        # A width-9 STORED block may not smuggle nine-bit symbols into an
        # eight-bit frame, even though the width itself is structurally valid.
        bad_values = [1, 2, 255, 256, 3, 4, 5, 6]
        bad_frame = frame_header(8, len(bad_values)) + stored_block(bad_values, 9)
        try:
            decode(bad_frame)
        except FormatError:
            pass
        else:
            raise AssertionError("Python decoder accepted a 9-bit value in mode 8")
        run_decode(bad_frame, work, "mode8-overwide-symbol", expected_ok=False)

        # CRC and low-order padding are independently checked by both paths.
        bad_crc = bytearray(mode8_frame)
        bad_crc[17 + 3] ^= 1
        try:
            decode(bytes(bad_crc))
        except FormatError:
            pass
        else:
            raise AssertionError("Python decoder accepted an invalid STORED CRC")
        run_decode(bytes(bad_crc), work, "bad-crc", expected_ok=False)

        bad_padding = bytearray(frame_header(9, 1) + stored_block([1], 9))
        bad_padding[-1] |= 1
        try:
            decode(bytes(bad_padding))
        except FormatError:
            pass
        else:
            raise AssertionError("Python decoder accepted nonzero STORED padding")
        run_decode(bytes(bad_padding), work, "bad-padding", expected_ok=False)

    print(
        "PASS STORED bulk decode widths 7/8/9, ring wrap, CRC, padding, and symbol bounds"
    )


if __name__ == "__main__":
    main()
