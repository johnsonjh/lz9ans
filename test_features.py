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

from inspect_lz9ans import decode, FormatError, pack9

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

    # V5 flags: bit 0 selects the 64K history; bit 1 enables EXTRA-ANS.
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
    with tempfile.TemporaryDirectory(prefix="lz9ans-v5-test-") as temporary:
        temp = Path(temporary)
        source, archive, decoded = (temp / name for name in ("in", "arc", "out"))
        for name, mode, raw, options, flags in cases:
            source.write_bytes(raw)
            run("c" + str(mode), source, archive, "-6", *options)
            data = archive.read_bytes()
            assert data[:4] == b"LZ9A"
            assert data[4] == 5, (name, "encoder did not emit v5")
            assert struct.unpack_from("<I", data, 12)[0] == flags, (name, "wrong flags")
            independent, stats = decode(data)
            assert independent == raw, (name, "independent decoder mismatch")
            run("d", archive, decoded)
            assert decoded.read_bytes() == raw, (name, "C decoder mismatch")
            if name == "window64-exact32k":
                assert stats["offset_19"] >= 1, stats
            if name == "window64-exact64k":
                assert stats["offset_20"] >= 1, stats
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

        # V5 deliberately has no v3/v4 compatibility path.
        for old_version in (3, 4):
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

        # Differential malformed-stream tests for the combined v5 features.
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
        "PASS v5 feature flags, EXTRA-ANS, 32K/64K distances, removed-option rejection, and malformed-stream tests"
    )


if __name__ == "__main__":
    main()
