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

from inspect_lz9ans import decode, FormatError, pack9, parse_descriptor

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
        raise AssertionError((args, result.returncode, result.stderr.decode()))
    return result


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

    # V6 flags: bit 0 selects the 64K history; bit 1 enables EXTRA-ANS.
    cases = [
        ("base-8", 8, bytes(range(256)) * 400, [], 0),
        ("base-9", 9, packed9, [], 0),
        ("window64-exact32k", 8, block_a + block_a, ["--window=64k"], 1),
        ("window64-exact64k", 8, block_a + block_b + block_a, ["--window=64k"], 1),
        ("extra-ans", 8, short_long, ["--extra-ans"], 2),
        (
            "window64-plus-extra",
            8,
            block_a + block_b + block_a,
            ["--window=64k", "--extra-ans"],
            3,
        ),
        ("empty-extra-ans", 8, b"", ["--extra-ans"], 2),
    ]
    with tempfile.TemporaryDirectory(prefix="lz9ans-v6-test-") as temporary:
        temp = Path(temporary)
        source, archive, decoded = (temp / name for name in ("in", "arc", "out"))
        for name, mode, raw, options, flags in cases:
            source.write_bytes(raw)
            run("c" + str(mode), source, archive, "-6", *options)
            data = archive.read_bytes()
            assert data[:4] == b"LZ9A"
            assert data[4] == 6, (name, "encoder did not emit v6")
            assert struct.unpack_from("<I", data, 12)[0] == flags, (name, "wrong flags")
            independent, stats = decode(data)
            assert independent == raw, (name, "independent decoder mismatch")
            run("d", archive, decoded)
            assert decoded.read_bytes() == raw, (name, "C decoder mismatch")
            if name == "window64-exact32k":
                assert stats["offset_19"] >= 1, stats
            if name == "window64-exact64k":
                assert stats["offset_20"] >= 1, stats
            if name == "extra-ans":
                assert stats["offset_2"] >= 1, stats
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

        # The third block must reuse the first block's LIT model even though
        # an intervening incompressible block is emitted as STORED.
        model_values = []
        for symbol, count in (
            [(value, 400) for value in range(8)]
            + [(value, 300) for value in range(8, 24)]
            + [(value, 131) for value in range(24, 88)]
        ):
            model_values.extend([symbol] * count)
        rng.shuffle(model_values)
        permutation = {}
        for group in (range(8), range(8, 24), range(24, 88)):
            source_symbols = list(group)
            target_symbols = source_symbols[:]
            rng.shuffle(target_symbols)
            permutation.update(zip(source_symbols, target_symbols))
        remapped_values = [permutation[value] for value in model_values]
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
        table_cache = [None] * 5
        position = 20
        first_lit_descriptor_size = None
        reused_lit_table = False
        for block_index in range(3):
            header = reuse_frame[position : position + 48]
            block_type = header[5] & 3
            desc_len = struct.unpack_from("<I", header, 20)[0]
            bit_lengths = struct.unpack_from("<5I", header, 24)
            if block_index == 1:
                assert block_type == 0 and desc_len == 0
            else:
                assert block_type == 1
                descriptor = reuse_frame[position + 48 : position + 48 + desc_len]
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
                    assert parsed_lit[0] == 4
                    assert desc_len < first_lit_descriptor_size
                    reused_lit_table = True
            position += 48 + desc_len + sum((bits + 7) // 8 for bits in bit_lengths)
        assert reused_lit_table and position == len(reuse_frame)
        print(
            "PASS compact LIT descriptor and table reuse across STORED block",
            "archive",
            len(reuse_frame),
        )

        # Unknown feature bits must be rejected even with a valid frame CRC.
        source.write_bytes(b"feature flags must be validated")
        run("c8", source, archive, "--window=64k")
        original = bytearray(archive.read_bytes())
        malformed = bytearray(original)
        struct.pack_into("<I", malformed, 12, 4)
        struct.pack_into("<I", malformed, 16, zlib.crc32(malformed[:16]))
        archive.write_bytes(malformed)
        run("d", archive, decoded, expect=1)
        try:
            decode(bytes(malformed))
        except FormatError:
            pass
        else:
            raise AssertionError("independent decoder accepted unknown feature bit")

        # V6 deliberately has no v3/v4/v5 compatibility path.
        for old_version in (3, 4, 5):
            malformed = bytearray(original)
            malformed[4] = old_version
            struct.pack_into("<I", malformed, 16, zlib.crc32(malformed[:16]))
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
        run("c8", source, archive, "--extra-ans", "--extra-ans", expect=2)

        # Differential malformed-stream tests for the combined v6 features.
        source.write_bytes(short_long + block_a + block_b + block_a)
        run("c8", source, archive, "-6", "--window=64k", "--extra-ans")
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
        "PASS v6 feature flags, EXTRA-ANS, 32K/64K distances, removed-option rejection, and malformed-stream tests"
    )


if __name__ == "__main__":
    main()
