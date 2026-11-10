#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

# Phased entropy tables (frame flag 0x02) and two-symbol matches (flag 0x04).

import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import zlib

from inspect_lz9ans import (
    FormatError,
    block_header,
    decode,
    frame_header,
    pack9,
    parse_descriptor,
)

ROOT = Path(__file__).resolve().parent
BIN = Path(os.environ.get("LZ9ANS_BIN", str(ROOT / "../lz9ans"))).resolve()


def run(*args, expect=0):
    result = subprocess.run(
        [str(BIN)] + [str(a) for a in args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    assert result.returncode == expect, (args, result.returncode, result.stderr)


def word_data(seed, words):
    """Four-symbol (36-bit) words whose fields have different statistics, with
    repeated structure at distances that are not multiples of four."""
    rng = random.Random(seed)
    ops = [rng.randrange(512) for _ in range(24)]
    values = []
    for index in range(words):
        values.extend(
            [
                rng.choice([0, 0, 1, 2, 3, 4]),
                rng.randrange(512),
                rng.choice(ops),
                rng.choice([0, 4, 6, 12, 12]),
            ]
        )
        if index % 97 == 0:
            values.extend([5, 5])
    return values


def sequence_data(seed, length):
    """Literal runs and word-aligned copies whose lengths depend on the phase
    (frame position modulo four) at which each sequence starts."""
    rng = random.Random(seed)
    ops = [rng.randrange(512) for _ in range(24)]
    out = []
    while len(out) < length:
        for _ in range(rng.randrange(1, 12)):
            phase = len(out) & 3
            if phase == 0:
                out.append(rng.choice([0, 0, 1, 2, 3, 4]))
            elif phase == 1:
                out.append(rng.randrange(512))
            elif phase == 2:
                out.append(rng.choice(ops))
            else:
                out.append(rng.choice([0, 4, 6, 12, 12]))
        if len(out) > 4000:
            distance = 4 * rng.randrange(8, 600)
            match = (-len(out)) % 4 + 4 * rng.randrange(1, 6)
            if match < 3:
                match += 4
            for _ in range(match):
                out.append(out[-distance])
    return out[: length - length % 8]


def refresh_crc(frame):
    length = struct.unpack_from("<H", frame, 6)[0]
    frame[length - 4 : length] = struct.pack("<I", zlib.crc32(frame[: length - 4]))


def locate_phased_descriptor(data):
    """Return (absolute offset of the first mode-5 descriptor, kind) or None."""
    position = frame_header(data)["length"]
    cache = [None] * 10
    while position < len(data):
        header = block_header(data, position)
        if header["type"] == 1:
            descriptor = header["descriptor"]
            start = position + header["header_size"]
            index = 0
            for kind in (0, 1, 2, 3):
                alphabet = 512 if kind == 0 else 20 if kind == 3 else 32
                first = index
                parsed, index = parse_descriptor(
                    descriptor, index, kind, alphabet, cache, True
                )
                if parsed[0] == 5:
                    return start + first, kind
        position += header["size"]
    return None


def main():
    raw = pack9(word_data(7, 36000) + word_data(8, 12000))
    with tempfile.TemporaryDirectory(prefix="lz9ans-phased-") as tmp:
        work = Path(tmp)
        source, archive, decoded = (work / n for n in ("in", "arc", "out"))
        source.write_bytes(raw)

        sizes = {}
        for level in (5, 6, 9):
            run("c9", source, archive, f"-{level}")
            data = archive.read_bytes()
            sizes[level] = len(data)
            assert frame_header(data)["version"] == 9
            assert frame_header(data)["flags"] == (0x18 if level == 5 else 0x1E)
            independent, stats = decode(data)
            assert independent == raw, (level, "independent mismatch")
            run("d", archive, decoded)
            assert decoded.read_bytes() == raw, (level, "C mismatch")
            if level == 5:
                assert not any(
                    stats[k] for k in stats if k.startswith("phased_")
                ), stats
                assert stats["match_2"] == 0, stats
            else:
                assert (
                    sum(v for k, v in stats.items() if k.startswith("phased_")) > 0
                ), stats
            print("PASS feature gating level", level, stats)

        assert sizes[6] <= sizes[5], sizes

        # Level 9 must find two-symbol matches in a workload containing many
        # repeated pairs; the same stream is decoded independently and by C.
        pair_values = []
        rng = random.Random(0x9A6)
        for index in range(6000):
            # The first two 8-symbol records share a 3-symbol prefix and prime
            # REP0 with distance eight. Later records share only the first two
            # symbols, so they require an actual two-symbol REP0 match.
            pair_values.extend((17, 23, 10 if index < 2 else 11 + index % 500))
            pair_values.extend(rng.randrange(512) for _ in range(5))
        pair_raw = pack9(pair_values)
        source.write_bytes(pair_raw)
        run("c9", source, archive, "-9")
        pair_archive = archive.read_bytes()
        independent, stats = decode(pair_archive)
        assert independent == pair_raw
        assert stats["match_2"] > 0, stats
        run("d", archive, decoded)
        assert decoded.read_bytes() == pair_raw
        print("PASS two-symbol matches", stats["match_2"])

        # The final two symbols of a full block can form a profitable
        # two-symbol REP match even though only two symbols remain for the
        # match finder.  The following one-symbol block ensures this is a
        # block-tail case rather than only a frame-tail case.
        # The periodic prefix retains distance 2008 in a repeat slot; the
        # rare marker pair makes the final two-symbol match profitable.
        tail_values = [(index * 73 + (index >> 3) * 19) % 251 for index in range(32768)]
        tail_distance = 2008
        tail_source = 32766 - tail_distance
        tail_values[tail_source : tail_source + 2] = [511, 510]
        tail_values[-3:] = [250, 511, 510]
        tail_values.append(42)
        tail_raw = pack9(tail_values)
        source.write_bytes(tail_raw)
        for level in (6, 9):
            run("c9", source, archive, f"-{level}")
            tail_archive = archive.read_bytes()
            independent, stats = decode(tail_archive)
            assert independent == tail_raw
            assert stats["match_2"] == 1, (level, stats)
            first_position = frame_header(tail_archive)["length"]
            first_block = block_header(tail_archive, first_position)
            assert first_block["type"] == 1 and first_block["n"] == 32768
            second_block = block_header(tail_archive, first_block["next_position"])
            assert second_block["type"] == 0 and second_block["n"] == 1
            assert second_block["next_position"] == len(tail_archive)
            run("d", archive, decoded)
            assert decoded.read_bytes() == tail_raw
        print("PASS two-symbol match at full-block tail")

        # Phase-dependent sequences: exercise all three phased stream kinds.
        # KL-optimal normalization can change whether phase-group reuse wins
        # the encoder's size comparison, so do not require that optional
        # optimization to be selected for this particular fixture.
        structured = pack9(sequence_data(5, 150000))
        source.write_bytes(structured)
        run("c9", source, archive, "-9")
        data = archive.read_bytes()
        independent, stats = decode(data)
        assert independent == structured
        run("d", archive, decoded)
        assert decoded.read_bytes() == structured
        assert all(stats[f"phased_kind{k}"] > 0 for k in (0, 1, 2)), stats
        print(
            "PASS phased LIT, LL and ML descriptors",
            {k: v for k, v in stats.items() if k.startswith("phased_")},
        )

        # Mode 5 without the frame flag is invalid.
        located = locate_phased_descriptor(data)
        assert located is not None
        offset, kind = located
        malformed = bytearray(data)
        malformed[12] &= ~2
        refresh_crc(malformed)
        archive.write_bytes(malformed)
        run("d", archive, decoded, expect=1)
        try:
            decode(bytes(malformed))
        except FormatError:
            pass
        else:
            raise AssertionError("mode 5 accepted without the phased-table flag")

        # Clearing the two-symbol-match flag changes the ML bias; the block
        # CRC must reject the result.
        malformed = bytearray(data)
        malformed[12] &= ~4
        refresh_crc(malformed)
        archive.write_bytes(malformed)
        run("d", archive, decoded, expect=1)
        try:
            decode(bytes(malformed))
        except FormatError:
            pass
        else:
            raise AssertionError("ML bias change was not detected")

        # Invalid partition id, invalid group mode, and desynchronization.
        for label, position, value in (
            ("partition id", offset + 1, 7),
            ("group mode", offset + 2, 9),
            ("desynchronised group", offset + 2, 0),
        ):
            malformed = bytearray(data)
            malformed[position] = value
            archive.write_bytes(malformed)
            run("d", archive, decoded, expect=1)
            try:
                decode(bytes(malformed))
            except FormatError:
                pass
            else:
                raise AssertionError(f"accepted corrupt phased descriptor: {label}")
        print(
            "PASS rejection of corrupt phased descriptors and feature-flag mismatches"
        )

        # Phase follows frame position across STORED blocks and short final
        # blocks: odd-length prefixes shift all later phases.
        for prefix in (1, 2, 3):
            values = word_data(11, 2000)
            raw_shifted = pack9(
                [7] * prefix + values + [0] * ((-prefix - len(values)) % 8)
            )
            source.write_bytes(raw_shifted)
            run("c9", source, archive, "-6")
            independent, _ = decode(archive.read_bytes())
            assert independent == raw_shifted, prefix
            run("d", archive, decoded)
            assert decoded.read_bytes() == raw_shifted, prefix
        print("PASS frame-relative phase with shifted word boundaries")

        rng = random.Random(3)
        random_data = pack9([rng.randrange(512) for _ in range(40000)])
        source.write_bytes(random_data)
        run("c9", source, archive, "-9")
        assert decode(archive.read_bytes())[0] == random_data
        run("d", archive, decoded)
        assert decoded.read_bytes() == random_data
        print("PASS incompressible data with phased tables enabled")

    print("PASS phased tables and two-symbol matches")


if __name__ == "__main__":
    main()
