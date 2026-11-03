#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# SPDX-License-Identifier: MIT

from pathlib import Path
import random
import os
import struct
import subprocess
import tempfile
import zlib

from inspect_lz9ans import block_header, decode, frame_header, pack9

ROOT = Path(__file__).resolve().parent
BINARY = Path(os.environ.get("LZ9ANS_BIN", str(ROOT / "lz9ans"))).resolve()


def run(*args, expect=0):
    result = subprocess.run(
        [str(BINARY), *map(str, args)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=120,
        check=False,
    )
    if result.returncode != expect:
        raise AssertionError(
            f"{args}: return code {result.returncode}, "
            f"expected {expect}: {result.stderr.decode(errors='replace')}"
        )


def stored9_block(symbols):
    payload = pack9(symbols)
    header = bytearray(11)
    header[:4] = b"LZ9B"
    header[4] = 9 << 2
    struct.pack_into("<H", header, 5, len(symbols))
    struct.pack_into("<I", header, 7, zlib.crc32(payload))
    return bytes(header) + payload


def split_block_frame(blocks, total):
    header = bytearray(17)
    header[:4] = b"LZ9A"
    header[4] = 8
    header[5] = 9
    struct.pack_into("<H", header, 6, len(header))
    struct.pack_into("<I", header, 8, total)
    header[12] = 0
    struct.pack_into("<I", header, 13, zlib.crc32(header[:13]))
    return bytes(header) + b"".join(blocks)


def main():
    rng = random.Random(0xC32B_A7C)

    with tempfile.TemporaryDirectory(prefix="lz9ans-crc-batch-") as temp:
        work = Path(temp)
        source = work / "input"
        archive = work / "archive"
        decoded = work / "decoded"

        # Exercise each packed-9 block-tail alignment, including tails that
        # follow a complete eight-symbol packing group.
        for count in range(1, 17):
            symbols = [rng.randrange(512) for _ in range(count)]
            raw = pack9(symbols)
            source.write_bytes(raw)
            run("c9", source, archive, "-5")
            frame = archive.read_bytes()
            independent, _ = decode(frame)
            assert independent == raw, count
            run("d", archive, decoded)
            assert decoded.read_bytes() == raw, count

        # A valid v8 frame may have short non-final blocks.  These deliberately
        # chosen block lengths leave a partial packed-9 byte at the boundary,
        # exercising the bulk output packer's carry-in path.
        first_symbols = [rng.randrange(512) for _ in range(11)]
        second_symbols = [rng.randrange(512) for _ in range(17)]
        split_symbols = first_symbols + second_symbols
        split_raw = pack9(split_symbols)
        split_frame = split_block_frame(
            [stored9_block(first_symbols), stored9_block(second_symbols)],
            len(split_symbols),
        )
        source.write_bytes(split_raw)
        archive.write_bytes(split_frame)
        independent, _ = decode(split_frame)
        assert independent == split_raw
        run("d", archive, decoded)
        assert decoded.read_bytes() == split_raw

        # Cross the 32K history-ring boundary inside a block while also
        # carrying a partial packed-9 output byte from the preceding block.
        first_symbols = [rng.randrange(512) for _ in range(20003)]
        second_symbols = [rng.randrange(512) for _ in range(20001)]
        wrap_symbols = first_symbols + second_symbols
        wrap_raw = pack9(wrap_symbols)
        wrap_frame = split_block_frame(
            [stored9_block(first_symbols), stored9_block(second_symbols)],
            len(wrap_symbols),
        )
        source.write_bytes(wrap_raw)
        archive.write_bytes(wrap_frame)
        independent, _ = decode(wrap_frame)
        assert independent == wrap_raw
        run("d", archive, decoded)
        assert decoded.read_bytes() == wrap_raw

        # Exercise the eight-symbol CRC/output packer across multiple complete
        # blocks and both history-window sizes.  At 64K, the second block is
        # a non-overlapping full-window match and takes the memcpy path.
        symbols = [rng.randrange(512) for _ in range(32768)]
        first = pack9(symbols)
        for options in ([], ["--window=64k"]):
            raw = first + first
            source.write_bytes(raw)
            run("c9", source, archive, "-6", *options)
            frame = archive.read_bytes()
            independent, stats = decode(frame)
            assert independent == raw
            if options:
                assert stats["lz"] >= 1, stats
                assert stats["offset_19"] >= 1, stats
            run("d", archive, decoded)
            assert decoded.read_bytes() == raw

        # A failed block CRC must be detected before any bytes from that block
        # are committed to the output file.
        raw = pack9([rng.randrange(512) for _ in range(32768)])
        source.write_bytes(raw)
        run("c9", source, archive, "-5")
        damaged = bytearray(archive.read_bytes())
        position = frame_header(damaged)["length"]
        header = block_header(damaged, position)
        crc_offset = position + (7 if header["type"] == 0 else 23)
        damaged[crc_offset] ^= 1
        archive.write_bytes(damaged)
        run("d", archive, decoded, expect=1)
        assert decoded.exists() and decoded.stat().st_size == 0

    print(
        "PASS packed-9 CRC tail alignments, multi-block ring output, "
        "and pre-emission CRC validation"
    )


if __name__ == "__main__":
    main()
