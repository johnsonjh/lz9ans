#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

from hashlib import sha256
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from inspect_lz9ans import (
    FormatError,
    block_header,
    decode,
    frame_header,
    parse_descriptor,
)

ROOT = Path(__file__).resolve().parent
VECTORS = {
    "empty8": (
        "c8",
        1,
        "c2cc46e5e963872f68ee43ff3592cf72320110c97f0d270cc11ce0a2782e1d84",
    ),
    "abc8": (
        "c8",
        1,
        "775135fe7d9a110e1c4c0e7b46612fe292c174500c0a3bc7cb921d5bcafc0bc5",
    ),
    "run8": (
        "c8",
        6,
        "dab23891e60115f4b978b0e91465a91f0df66bb40e032ace094fc3ba229434a6",
    ),
    "mixed9": (
        "c9",
        6,
        "aaccb9f56512dd68a5f58bd68ca2f12a001a75935e596baed28c6ddcf18eec33",
    ),
    "switch8": (
        "c8",
        6,
        "352bd0b529f643a1a5e292c63b01e7c09b7500a39239895e768b2b467fceb1b0",
    ),
    "longdist16k": (
        "c8",
        9,
        "b710c0a6e4fb656cfe84764c4c53857bf58c497d53a7014a9fce3c732af01d31",
    ),
    "longdist32k": (
        "c8",
        9,
        "93c7724edf107fda106720e011d54f8a78c4b8771903aa451d2f25135b5d9d61",
    ),
    "phased9": (
        "c9",
        9,
        "ea0602478f02c4065f28ef27897872b48e250ab8bcf297b5704667c83f948cfb",
    ),
}


def command(*parts):
    result = subprocess.run(
        parts, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
    )
    return result.returncode


def main():
    binary = Path(os.environ.get("LZ9ANS_BIN", str(ROOT / "../lz9ans"))).resolve()
    with tempfile.TemporaryDirectory(prefix="lz9ans-golden-") as tmp:
        work = Path(tmp)
        for name, (mode, level, expected_hash) in VECTORS.items():
            raw = (ROOT / "../vectors" / f"{name}.input").read_bytes()
            vector = (ROOT / "../vectors" / f"{name}.lz9a").read_bytes()
            assert sha256(vector).hexdigest() == expected_hash, name
            assert vector[:4] == b"LZ9A" and vector[4] == 9, name
            assert frame_header(vector)["length"] == 17, name
            assert vector[12] == (0x18 if level <= 5 else 0x1E), name
            if len(vector) > 17:
                header = block_header(vector, 17)
                assert header["header_size"] in (7, 23), name
                assert vector[17:21] != b"LZ9B", name
            if name == "mixed9":
                header = block_header(vector, 17)
                assert header["type"] == 1
                assert header["aligned"]
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
                    str(ROOT / "../vectors" / f"{name}.input"),
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
            if name == "phased9":
                assert all(stats[f"phased_kind{k}"] > 0 for k in (0, 1, 2)), stats
                assert stats["match_2"] > 0, stats
            print("PASS byte-exact v9", name)

        # The RUN fixture has no initialized MRU distance. Change its OF RLE
        # symbol to REP0; the independent and C decoders must reject it.
        invalid = bytearray((ROOT / "../vectors" / "run8.lz9a").read_bytes())
        block = block_header(invalid, 17)
        desc_len = block["desc_length"]
        desc_start = 17 + block["header_size"]
        descriptor = block["descriptor"]
        index = 0
        tables = [None] * 10
        offset_start = None
        for kind in (0, 1, 2, 3):
            start = index
            alphabet = 512 if kind == 0 else 20 if kind == 3 else 32
            parsed, index = parse_descriptor(
                descriptor, index, kind, alphabet, tables, bool(vector[12] & 0x02)
            )
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
        reuse = bytearray((ROOT / "../vectors" / "run8.lz9a").read_bytes())
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
