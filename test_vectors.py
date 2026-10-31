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
        "9bc9b0a79566eda38946773b3ace46c25652eb95a7f9d9a020e57fd746807b61",
    ),
    "abc8": (
        "c8",
        1,
        "6bc328d99e4a01b9733b6534290c8531a2a47f29a9fe61d6296c06cce8cbb3bc",
    ),
    "run8": (
        "c8",
        6,
        "a589c141902aa34fe17e37601f247c6a4e8d7217adf378a4cb61bc8607d17659",
    ),
    "mixed9": (
        "c9",
        6,
        "f63c3c56a8d2ee11e547b7c5712d1e0fc05e6e2891558e2e0c9458ee4d7c8ce9",
    ),
    "switch8": (
        "c8",
        6,
        "9e5ab81c964c216a4f59db7dac2342eb40284468aeab4a1ca8fbf42484639056",
    ),
    "longdist16k": (
        "c8",
        9,
        "3cfb9c5336e9fecc333df7160dc8f542c97ee1f4ede9c199aa027b5b3cc85c50",
    ),
    "longdist32k": (
        "c8",
        9,
        "bb544e11fffd452aeed4a9694da5a95461b3e72ef373469375397b690fa639d0",
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
            assert struct.unpack_from("<I", vector, 12)[0] == 0x18, name
            if name == "mixed9":
                block_header = vector[20:68]
                assert block_header[:4] == b"LZ9B"
                assert (block_header[5] & 3) == 1
                assert block_header[5] & 16
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
