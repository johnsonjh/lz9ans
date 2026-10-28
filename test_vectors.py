#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

from hashlib import sha256
from pathlib import Path
import subprocess
import tempfile

from inspect_lz9ans import FormatError, decode

ROOT = Path(__file__).resolve().parent
VECTORS = {
    "empty8": (
        "c8",
        1,
        "3d5a4c6bfb436c5d37bf598f67176678c0b791823da1a4f82e337e72c9f2e117",
    ),
    "abc8": (
        "c8",
        1,
        "0f9e26d6f5e14c382bcda0c74e30ac064be58e5c89fcb0e0ff77e0fd1892d563",
    ),
    "run8": (
        "c8",
        6,
        "bd1c000a5da8bc944ff97da8442eafe7261459fcb1a117184bf81ac47433921c",
    ),
    "mixed9": (
        "c9",
        6,
        "b1b33489ebb293371ddb6e2d4ade65de43be81b2e35933f310a5674c83e5ecd9",
    ),
    "switch8": (
        "c8",
        6,
        "78557387e91d9ff33db2f97c804dc327ff3450b506cd8d42d37c53dd59cae669",
    ),
    "longdist16k": (
        "c8",
        9,
        "83e3f89be3579929a981fc3c222e301e442a8f56ae17170791cd641167b3b909",
    ),
    "longdist32k": (
        "c8",
        9,
        "832d58995ae2092f2099f6b2e2f16c63d4acf1f084c4fd8f8bf9938074dc3226",
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
            frame = (ROOT / "vectors" / f"{name}.lz9a").read_bytes()
            assert sha256(frame).hexdigest() == expected_hash, name
            result, counter = decode(frame)
            assert result == raw, name
            assert (
                command(
                    str(binary),
                    "d",
                    str(ROOT / "vectors" / f"{name}.lz9a"),
                    str(work / "decoded"),
                )
                == 0
            ), name
            assert (work / "decoded").read_bytes() == raw, name
            assert (
                command(
                    str(binary),
                    mode,
                    str(ROOT / "vectors" / f"{name}.input"),
                    str(work / "encoded"),
                    f"-{level}",
                )
                == 0
            ), name
            assert (work / "encoded").read_bytes() == frame, name
            if name == "run8":
                assert counter["offset_3"] == 1
            if name == "longdist16k":
                assert counter["offset_18"] == 1
            if name == "longdist32k":
                assert counter["offset_19"] == 1
                assert counter["stored"] == 1
            print("PASS byte-exact", name)

        # This RUN vector has no previously initialized MRU distance.
        # Change its OF RLE symbol from RUN (3) to REP0 (0). No block
        # integrity adjustment is necessary: the decoder must reject the
        # invalid repeat distance before completing the block.
        invalid = bytearray((ROOT / "vectors" / "run8.lz9a").read_bytes())
        assert invalid[-2] == 3
        invalid[-2] = 0
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


if __name__ == "__main__":
    main()
