#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

from pathlib import Path
import random
import os
import struct
import subprocess
import tempfile
import zlib

from inspect_lz9ans import (
    block_header,
    decode,
    FormatError,
    frame_header,
    pack9,
    parse_descriptor,
)

ROOT = Path(__file__).resolve().parent
BINARY = Path(os.environ.get("LZ9ANS_BIN", str(ROOT / "../lz9ans"))).resolve()


def run(*args, expect=0):
    result = subprocess.run(
        [str(BINARY), *map(str, args)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=120,
        check=False,
    )
    if result.returncode != expect:
        raise AssertionError((args, result.returncode, result.stderr.decode()))
    return result


def aligned_block_flags(data):
    flags = []
    position = frame_header(data)["length"]
    while position < len(data):
        header = block_header(data, position)
        block_type = header["type"]
        assert header["header_size"] == (7 if block_type == 0 else 23)
        flags.append((block_type, bool(block_type == 1 and header.get("aligned"))))
        position = header["next_position"]
    assert position == len(data)
    return flags


def main():
    rng = random.Random(0x9A64)
    block_a = bytes(rng.randrange(256) for _ in range(32768))
    block_b = bytes(rng.randrange(256) for _ in range(32768))
    values9 = list(range(512)) * 128
    packed9 = pack9(values9)
    short_long = bytearray()
    previous = None
    for index in range(1800):
        record = bytearray(rng.randrange(256) for _ in range(24))
        if previous is not None and index % 4 == 0:
            record[0:5] = previous[8:13]
        short_long.extend(record)
        if index % 31 == 0:
            short_long.extend(b"long-match-context-probe/" * 24)
        previous = record
    short_long = bytes(short_long)

    # V9 uses a one-octet frame feature mask. Bits 3 and 4 advertise
    # automatic per-block aligned-distance selection. Bits 1 and 2 advertise
    # phased entropy tables and two-symbol matches, enabled only at levels 6-9.
    cases = [
        ("level5-8", 8, bytes(range(256)) * 400, "-5", [], 24),
        ("level6-8", 8, bytes(range(256)) * 400, "-6", [], 30),
        ("level5-9", 9, packed9, "-5", [], 24),
        ("level6-9", 9, packed9, "-6", [], 30),
        ("window32-explicit", 8, block_a + block_a, "-6", ["--window=32k"], 30),
        ("window64-exact32k", 8, block_a + block_a, "-6", ["--window=64k"], 31),
        (
            "window64-exact64k",
            8,
            block_a + block_b + block_a,
            "-6",
            ["--window=64k"],
            31,
        ),
        ("raw-extra", 8, short_long, "-6", [], 30),
    ]
    with tempfile.TemporaryDirectory(prefix="lz9ans-v9-test-") as temporary:
        temp = Path(temporary)
        source, archive, decoded = (temp / name for name in ("in", "arc", "out"))
        for name, mode, raw, level, options, flags in cases:
            source.write_bytes(raw)
            run("c" + str(mode), source, archive, level, *options)
            data = archive.read_bytes()
            assert data[:4] == b"LZ9A"
            assert data[4] == 9, (name, "encoder did not emit v9")
            assert struct.unpack_from("<H", data, 6)[0] == 17
            assert data[12] == flags, (name, "wrong flags")
            header_length = frame_header(data)["length"]
            assert struct.unpack_from("<I", data, header_length - 4)[0] == zlib.crc32(
                data[: header_length - 4]
            )
            independent, stats = decode(data)
            assert independent == raw, (name, "independent decoder mismatch")
            run("d", archive, decoded)
            assert decoded.read_bytes() == raw, (name, "C decoder mismatch")
            if name in ("window32-explicit", "window64-exact32k"):
                assert stats["offset_19"] >= 1, stats
            if name == "window64-exact64k":
                assert stats["offset_20"] >= 1, stats
            if name == "raw-extra":
                position = header_length
                found_raw_extra = False
                while position < len(data):
                    header = block_header(data, position)
                    if header["type"] == 1 and header["bit_lengths"][4] != 0:
                        found_raw_extra = True
                    position = header["next_position"]
                assert found_raw_extra, "raw EXTRA payload was not exercised"
            print(
                "PASS",
                name,
                "input",
                len(raw),
                "archive",
                len(data),
                "stats",
                dict(stats),
            )

        # Exercise LIT table handling across an intervening STORED block.
        # The KL-optimal model can change the encoder's reuse decision, so
        # validate the emitted descriptors and round trip rather than require
        # a particular descriptor choice.
        model_values = []
        for symbol, count in (
            [(value, 400) for value in range(8)]
            + [(value, 300) for value in range(8, 24)]
            + [(value, 131) for value in range(24, 88)]
        ):
            model_values.extend([symbol] * count)
        rng.shuffle(model_values)
        # Preserve the exact histogram while changing order enough to avoid
        # reusing the earlier block's matches.
        remapped_values = model_values[:]
        rng.shuffle(remapped_values)
        stored_values = [rng.randrange(512) for _ in range(32768)]
        reuse_raw = pack9(
            model_values
            + model_values
            + stored_values
            + remapped_values
            + remapped_values
        )
        source.write_bytes(reuse_raw)
        run("c9", source, archive, "-5")
        reuse_frame = archive.read_bytes()
        reuse_decoded, reuse_stats = decode(reuse_frame)
        assert reuse_decoded == reuse_raw
        run("d", archive, decoded)
        assert decoded.read_bytes() == reuse_raw
        assert reuse_stats["lz"] == 2 and reuse_stats["stored"] == 1, reuse_stats
        table_cache = [None] * 4
        position = frame_header(reuse_frame)["length"]
        first_lit_descriptor_size = None
        reused_lit_table = False
        for block_index in range(3):
            header = block_header(reuse_frame, position)
            block_type = header["type"]
            desc_len = header["desc_length"]
            if block_index == 1:
                assert block_type == 0 and desc_len == 0
            else:
                assert block_type == 1
                descriptor = header["descriptor"]
                descriptor_offset = 0
                parsed_lit, descriptor_offset = parse_descriptor(
                    descriptor, descriptor_offset, 0, 512, table_cache
                )
                for kind in (1, 2, 3):
                    _, descriptor_offset = parse_descriptor(
                        descriptor,
                        descriptor_offset,
                        kind,
                        32 if kind != 3 else 20,
                        table_cache,
                    )
                assert descriptor_offset == desc_len
                if block_index == 0:
                    assert parsed_lit[0] == 3
                    first_lit_descriptor_size = desc_len
                else:
                    assert parsed_lit[0] in (3, 4)
                    if parsed_lit[0] == 4:
                        assert desc_len < first_lit_descriptor_size
                        reused_lit_table = True
            position = header["next_position"]
        assert position == len(reuse_frame)
        print(
            "PASS LIT descriptor handling across STORED block",
            "archive",
            len(reuse_frame),
        )

        # At levels 6-9, a cached table may be selected for a different
        # normalized histogram when trial encoding proves that its payload
        # plus the one-octet REUSE descriptor is smaller.  The input keeps
        # the same 256-symbol support in each region while shifting two
        # frequencies enough to produce a distinct model.
        base_counts = [128] * 256
        first_counts = base_counts[:]
        first_counts[0] += 1024
        first_counts[1] -= 1024
        second_counts = base_counts[:]
        second_counts[2] += 1024
        second_counts[3] -= 1024

        def histogram_values(counts, seed):
            values = []
            for symbol, count in enumerate(counts):
                values.extend([symbol] * count)
            random.Random(seed).shuffle(values)
            return values

        changed_model = pack9(
            histogram_values(first_counts, 15) + histogram_values(second_counts, 16)
        )
        for level in (5, 6):
            source.write_bytes(changed_model)
            run("c9", source, archive, f"-{level}")
            changed_frame = archive.read_bytes()
            changed_decoded, changed_stats = decode(changed_frame)
            assert changed_decoded == changed_model
            run("d", archive, decoded)
            assert decoded.read_bytes() == changed_model
            print("PASS measured normal REUSE selection level", level, changed_stats)

        # Automatic aligned-distance coding is now the default.  The frame
        # advertises per-block selection, and the ninth-bit gate prevents the
        # transformation from being considered for ordinary 8-bit blocks.
        aligned_prefix = [rng.randrange(512) for _ in range(16384)]
        aligned_9 = pack9(aligned_prefix + aligned_prefix)
        source.write_bytes(aligned_9)
        run("c9", source, archive, "-5")
        auto_frame = archive.read_bytes()
        assert auto_frame[12] == 24
        assert aligned_block_flags(auto_frame) == [(1, True)]
        auto_independent, _ = decode(auto_frame)
        assert auto_independent == aligned_9
        run("d", archive, decoded)
        assert decoded.read_bytes() == aligned_9

        # V9 streams using frame-wide aligned-distance mode remain readable.
        # Convert this one-block per-block frame to that form:
        # retain the aligned payload, clear the block selector, and clear the
        # per-block frame flag while leaving the global alignment flag set.
        frame_wide_aligned = bytearray(auto_frame)
        frame_wide_aligned[12] = auto_frame[12] & ~16
        frame_wide_aligned[13:17] = struct.pack(
            "<I", zlib.crc32(frame_wide_aligned[:13])
        )
        frame_wide_aligned[17] &= ~16
        archive.write_bytes(frame_wide_aligned)
        run("d", archive, decoded)
        assert decoded.read_bytes() == aligned_9
        frame_wide_independent, _ = decode(bytes(frame_wide_aligned))
        assert frame_wide_independent == aligned_9

        aligned_8_values = [rng.randrange(256) for _ in range(16384)]
        aligned_8 = pack9(aligned_8_values + aligned_8_values)
        source.write_bytes(aligned_8)
        run("c9", source, archive, "-5")
        gated_frame = archive.read_bytes()
        assert gated_frame[12] == 24
        assert aligned_block_flags(gated_frame) == [(1, False)]
        gated_independent, _ = decode(gated_frame)
        assert gated_independent == aligned_8
        run("d", archive, decoded)
        assert decoded.read_bytes() == aligned_8

        # Frames with neither aligned-distance feature flag retain baseline
        # V9 semantics and remain readable by both decoders.
        alignment_disabled = bytearray(gated_frame)
        alignment_disabled[12] = gated_frame[12] & ~(8 | 16)
        alignment_disabled[13:17] = struct.pack(
            "<I", zlib.crc32(alignment_disabled[:13])
        )
        archive.write_bytes(alignment_disabled)
        run("d", archive, decoded)
        assert decoded.read_bytes() == aligned_8
        alignment_disabled_decoded, _ = decode(bytes(alignment_disabled))
        assert alignment_disabled_decoded == aligned_8

        unaligned_prefix = [rng.randrange(512) for _ in range(16385)]
        unaligned_9 = pack9(unaligned_prefix + unaligned_prefix[:16383])
        source.write_bytes(unaligned_9)
        run("c9", source, archive, "-5")
        unaligned_frame = archive.read_bytes()
        assert aligned_block_flags(unaligned_frame) == [(1, False)]
        unaligned_independent, _ = decode(unaligned_frame)
        assert unaligned_independent == unaligned_9
        run("d", archive, decoded)
        assert decoded.read_bytes() == unaligned_9

        # Exercise aligned code-20 distance selection in a 64K window.
        # Alignment is selected for the match-bearing LZ
        # block, not as a frame-wide setting.
        prefix_values = [rng.randrange(512) for _ in range(32768)]
        distance_raw = pack9(prefix_values + prefix_values[:4] + prefix_values)
        source.write_bytes(distance_raw)
        run("c9", source, archive, "-5", "--window=64k")
        distance_frame = archive.read_bytes()
        assert distance_frame[12] == 25
        assert aligned_block_flags(distance_frame) == [
            (0, False),
            (1, True),
            (0, False),
        ]
        distance_independent, distance_stats = decode(distance_frame)
        assert distance_independent == distance_raw
        assert distance_stats["offset_20"] >= 1
        run("d", archive, decoded)
        assert decoded.read_bytes() == distance_raw
        print(
            "PASS default per-block alignment, ninth-bit gate, frame-wide alignment decode, and 64K code 20"
        )

        auto_distance_values = prefix_values + [1, 2, 3, 4] + prefix_values[:32764]
        auto_distance_raw = pack9(auto_distance_values)
        source.write_bytes(auto_distance_raw)
        run("c9", source, archive, "-5", "--window=64k")
        auto_distance_frame = archive.read_bytes()
        assert auto_distance_frame[12] == 25
        assert aligned_block_flags(auto_distance_frame) == [
            (0, False),
            (1, True),
        ]
        auto_distance_independent, _ = decode(auto_distance_frame)
        assert auto_distance_independent == auto_distance_raw
        run("d", archive, decoded)
        assert decoded.read_bytes() == auto_distance_raw
        print("PASS per-block aligned code-20 selection")

        # Unknown feature bits must be rejected even with a valid frame CRC.
        source.write_bytes(b"feature flags must be validated")
        run("c8", source, archive, "--window=64k")
        original = bytearray(archive.read_bytes())
        malformed = bytearray(original)
        malformed[12] = 32
        malformed[13:17] = struct.pack("<I", zlib.crc32(malformed[:13]))
        archive.write_bytes(malformed)
        run("d", archive, decoded, expect=1)
        try:
            decode(bytes(malformed))
        except FormatError:
            pass
        else:
            raise AssertionError("independent decoder accepted unknown feature bit")

        malformed = bytearray(original)
        malformed[12] = 16
        malformed[13:17] = struct.pack("<I", zlib.crc32(malformed[:13]))
        archive.write_bytes(malformed)
        run("d", archive, decoded, expect=1)
        try:
            decode(bytes(malformed))
        except FormatError:
            pass
        else:
            raise AssertionError(
                "independent decoder accepted auto flag without aligned flag"
            )

        # Reserved feature bits must remain zero.
        for reserved_bit in (64, 128):
            malformed = bytearray(original)
            malformed[12] = reserved_bit
            malformed[13:17] = struct.pack("<I", zlib.crc32(malformed[:13]))
            archive.write_bytes(malformed)
            run("d", archive, decoded, expect=1)
            try:
                decode(bytes(malformed))
            except FormatError:
                pass
            else:
                raise AssertionError(
                    f"independent decoder accepted reserved feature value {reserved_bit}"
                )

        # Experimental coding controls are no longer part of the CLI.
        for removed_option in (
            "--word-literals",
            "--aligned-distances",
            "--auto-aligned-distances",
            "--auto-aligned-distances-any",
            "--word-aware",
        ):
            run("c8", source, archive, removed_option, expect=2)

        # Phased-table and short-match controls are deliberately not CLI options.
        for removed_option in ("--no-phased-tables", "--no-short-matches"):
            run("c8", source, archive, removed_option, expect=2)

        # V9 deliberately has no prior-version compatibility path.
        for old_version in (3, 4, 5, 6, 7, 8, 10):
            malformed = bytearray(original)
            malformed[4] = old_version
            malformed[13:17] = struct.pack("<I", zlib.crc32(malformed[:13]))
            archive.write_bytes(malformed)
            run("d", archive, decoded, expect=1)
            try:
                decode(bytes(malformed))
            except FormatError:
                pass
            else:
                raise AssertionError(f"independent decoder accepted v{old_version}")

        # Removed options are no longer accepted; retained selectors reject duplicates.
        run("c8", source, archive, "--context-literals", expect=2)
        run("c8", source, archive, "--distance-context", expect=2)
        run("c8", source, archive, "--window=64k", "--window=64k", expect=2)

        # Differential malformed-stream tests for the V9 feature combination.
        source.write_bytes(short_long + block_a + block_b + block_a)
        run("c8", source, archive, "-6", "--window=64k")
        original = archive.read_bytes()
        original_decoded, _ = decode(original)
        assert original_decoded == source.read_bytes()
        fuzz_rng = random.Random(0xF00D)
        for index in range(36):
            mutated = bytearray(original)
            if index % 4 == 0:
                mutated = mutated[: fuzz_rng.randrange(len(mutated))]
            else:
                at = fuzz_rng.randrange(len(mutated))
                mutated[at] ^= 1 << fuzz_rng.randrange(8)
            archive.write_bytes(mutated)
            c_result = subprocess.run(
                [str(BINARY), "d", str(archive), str(decoded)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=30,
                check=False,
            )
            assert c_result.returncode in (0, 1), (
                index,
                c_result.returncode,
                c_result.stderr.decode(),
            )
            try:
                python_decoded, _ = decode(bytes(mutated))
                python_ok = True
            except (FormatError, IndexError, struct.error, OverflowError):
                python_ok = False
                python_decoded = None
            c_ok = c_result.returncode == 0
            assert c_ok == python_ok, (index, "C/Python corruption acceptance differs")
            if c_ok:
                assert decoded.read_bytes() == source.read_bytes() == python_decoded, (
                    index,
                    "mutated archive changed decoded data",
                )
    print(
        "PASS v9 feature flags, raw bypass bits, 32K/64K distances, unsupported-option handling, and malformed-stream tests"
    )


if __name__ == "__main__":
    main()
