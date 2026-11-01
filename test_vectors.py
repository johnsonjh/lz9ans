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
        "fd76cddc8175fb63f9b16c3d082ceea254bf684dc8240a1f99b0964c2cf4265f",
    ),
    "abc8": (
        "c8",
        1,
        "c3c7e9ce160dcc5b767b462da109be9c6c163b2508a3520639275f8f651a05f4",
    ),
    "run8": (
        "c8",
        6,
        "0f8e1acd5795861afdb9da92eeafb5846391a794041f9aac2ed48c8825d0c743",
    ),
    "mixed9": (
        "c9",
        6,
        "c8cdb0550ebe11242a3ac2b664af92652bbd3e2c4bdcaeef2d1d1719a807a1af",
    ),
    "switch8": (
        "c8",
        6,
        "fd66b1558f29ea94f648c070b6d2a9358a6cc7296daa3c4b978ed52aba6c15e5",
    ),
    "longdist16k": (
        "c8",
        9,
        "574ffc85c9bd00c2012f8626caebee42bf88f2caf49f72ba07aa0ac7531c53c7",
    ),
    "longdist32k": (
        "c8",
        9,
        "170a43b6fe53b2bf19e20b20419213ba87a249975c781908983ffaf2cebd970a",
    ),
}


def command(*parts):
    result = subprocess.run(
        parts, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
    )
    return result.returncode


def main():
    binary = Path(os.environ.get("LZ9ANS_BIN", str(ROOT / "lz9ans"))).resolve()
    with tempfile.TemporaryDirectory(prefix="lz9ans-golden-") as tmp:
        work = Path(tmp)
        for name, (mode, level, expected_hash) in VECTORS.items():
            raw = (ROOT / "vectors" / f"{name}.input").read_bytes()
            vector = (ROOT / "vectors" / f"{name}.lz9a").read_bytes()
            assert sha256(vector).hexdigest() == expected_hash, name
            assert vector[:4] == b"LZ9A" and vector[4] == 8, name
            assert frame_header(vector)["length"] == 17, name
            assert vector[12] == 0x18, name
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
            print("PASS byte-exact v8", name)

        # The RUN fixture has no initialized MRU distance. Change its OF RLE
        # symbol to REP0; the independent and C decoders must reject it.
        invalid = bytearray((ROOT / "vectors" / "run8.lz9a").read_bytes())
        block = block_header(invalid, 17)
        desc_len = block["desc_length"]
        desc_start = 17 + block["header_size"]
        descriptor = block["descriptor"]
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
