#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

from hashlib import sha256
from pathlib import Path
import struct
import subprocess
import tempfile

from inspect_lz9ans import FormatError, decode, parse_descriptor

ROOT = Path(__file__).resolve().parent
VECTORS = {
    "empty8": (
        "c8",
        1,
        "c88be22be7c4dab9e4b1236d9ac2ec2432d2e6f6849461a1bac5b09972b6e59a",
    ),
    "abc8": (
        "c8",
        1,
        "469f59ac6397f57087411ccfd2ed3e604572f54fd30ec1fdfe168dce9ffca512",
    ),
    "run8": (
        "c8",
        6,
        "19eb8e29683afc1f79b0d0216e27e70ec4da59f7734accea25e18d7a40abfde5",
    ),
    "mixed9": (
        "c9",
        6,
        "9a6a0a436ae6d438d5b1c183867d9d85c80276e0ae8df7b826fa786a9164e9de",
    ),
    "switch8": (
        "c8",
        6,
        "54a9994f257c71f54f0d1b67fe56dc9f4558655d779ca3de68ea822d04e8b9e1",
    ),
    "longdist16k": (
        "c8",
        9,
        "f442d9e491a86f6d5e3c1f3e84927940d9b2699a4ae3a54bac8bf575826cb363",
    ),
    "longdist32k": (
        "c8",
        9,
        "ce5c60b5e7ccf915c2a3bb4dc35c9a2a3a69447c9ee69523ea94e4fa72b9e410",
    ),
}


def command(*parts):
    result = subprocess.run(
        parts, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
    )
    return result.returncode


def main():
    binary = (ROOT / "lz9ans").resolve()
    with tempfile.TemporaryDirectory(prefix="lz9ans-golden-") as tmp:
        work = Path(tmp)
        for name, (mode, level, expected_hash) in VECTORS.items():
            raw = (ROOT / "vectors" / f"{name}.input").read_bytes()
            vector = (ROOT / "vectors" / f"{name}.lz9a").read_bytes()
            assert sha256(vector).hexdigest() == expected_hash, name
            assert vector[:4] == b"LZ9A" and vector[4] == 6, name
            decoded, stats = decode(vector)
            assert decoded == raw, name
            frame_path = work / f"{name}.lz9a"
            frame_path.write_bytes(vector)
            assert (
                command(str(binary), "d", str(frame_path), str(work / "decoded")) == 0
            )
            assert (work / "decoded").read_bytes() == raw
            assert (
                command(
                    str(binary),
                    mode,
                    str(ROOT / "vectors" / f"{name}.input"),
                    str(work / "encoded"),
                    f"-{level}",
                )
                == 0
            )
            assert (work / "encoded").read_bytes() == vector, name
            if name == "run8":
                assert stats["offset_3"] == 1
            if name == "longdist16k":
                assert stats["offset_18"] == 1
            if name == "longdist32k":
                assert stats["offset_19"] == 1
                assert stats["stored"] == 1
            print("PASS byte-exact v6", name)

        # The RUN fixture has no initialized MRU distance. Change its OF RLE
        # symbol to REP0; the independent and C decoders must reject it.
        invalid = bytearray((ROOT / "vectors" / "run8.lz9a").read_bytes())
        desc_len = struct.unpack_from("<I", invalid, 20 + 20)[0]
        desc_start = 20 + 48
        descriptor = invalid[desc_start : desc_start + desc_len]
        index = 0
        tables = [None] * 5
        offset_start = None
        for kind in (0, 1, 2, 3):
            start = index
            alphabet = 512 if kind == 0 else 20 if kind == 3 else 32
            parsed, index = parse_descriptor(descriptor, index, kind, alphabet, tables)
            if kind == 3:
                assert parsed[0] == 2 and descriptor[start + 1] == 3
                offset_start = start
        assert index == len(descriptor) and offset_start is not None
        invalid[desc_start + offset_start + 1] = 0
        (work / "invalid.lz9a").write_bytes(invalid)
        assert (
            command(str(binary), "d", str(work / "invalid.lz9a"), str(work / "failed"))
            != 0
        )
        try:
            decode(invalid)
        except FormatError:
            pass
        else:
            raise AssertionError("Python decoder accepted uninitialized REP0")
        print("PASS rejection of uninitialized repeat-distance code")

        # A reuse descriptor is illegal until that stream has installed a table.
        # The empty table state is represented by the first table's alphabet 0.
        reuse = bytearray((ROOT / "vectors" / "run8.lz9a").read_bytes())
        reuse[desc_start] = 4
        (work / "invalid-reuse.lz9a").write_bytes(reuse)
        assert (
            command(
                str(binary),
                "d",
                str(work / "invalid-reuse.lz9a"),
                str(work / "failed-reuse"),
            )
            != 0
        )
        try:
            decode(reuse)
        except FormatError:
            pass
        else:
            raise AssertionError("Python decoder accepted uninitialized table reuse")
        print("PASS rejection of table reuse before initialization")


if __name__ == "__main__":
    main()
