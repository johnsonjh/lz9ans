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
        "9eb5cbe8690f77c6e69a856d203fb8afd97db7bd41b810bc25673017206d7cce",
    ),
    "abc8": (
        "c8",
        1,
        "7ce8c8b130773180db46300850ba1c9e7df258535920c5ce770c5f4cdc0361e9",
    ),
    "run8": (
        "c8",
        6,
        "ec9946197edbf49bd87cb9765ff61c40f9b7a97275e1b38cbb9b47c0ba9a84a9",
    ),
    "mixed9": (
        "c9",
        6,
        "8a7fc1ff272b2adc9e9e0b28df16ada9e1d3a617ff15930d9539a6e74d8dd4d6",
    ),
    "switch8": (
        "c8",
        6,
        "9a3492b91610c647edc7071db29312d59e18e49b9c617d3671892d99699116e7",
    ),
    "longdist16k": (
        "c8",
        9,
        "5291fbb28f736bd0e9eed950189744f0b20404ffd976fb8c6de7d885e5d536d7",
    ),
    "longdist32k": (
        "c8",
        9,
        "3d58c47ff1a0d272f814f0b06aec12a3d646dd5a7366920f5be30e0e299048f0",
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
            assert vector[:4] == b"LZ9A" and vector[4] == 7, name
            assert struct.unpack_from("<H", vector, 6)[0] == 17, name
            assert vector[12] == 0x18, name
            if name == "mixed9":
                block_header = vector[17:65]
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
            print("PASS byte-exact v7", name)

        # The RUN fixture has no initialized MRU distance. Change its OF RLE
        # symbol to REP0; the independent and C decoders must reject it.
        invalid = bytearray((ROOT / "vectors" / "run8.lz9a").read_bytes())
        desc_len = struct.unpack_from("<I", invalid, 17 + 20)[0]
        desc_start = 17 + 48
        descriptor = invalid[desc_start : desc_start + desc_len]
        index = 0
        tables = [None] * 4
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
