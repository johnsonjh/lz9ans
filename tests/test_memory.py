#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent
BINARY = Path(os.environ.get("LZ9ANS_BIN", str(ROOT / "../lz9ans"))).resolve()


def memory_report(option):
    result = subprocess.run(
        [str(BINARY), option],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(result.stderr.decode(errors="replace"))
    text = result.stdout.decode(errors="replace")

    def value(label):
        match = re.search(rf"^{re.escape(label)}: ([0-9]+) bytes", text, re.M)
        if not match:
            raise AssertionError(f"missing memory report field {label!r}:\n{text}")
        return int(match.group(1))

    return (
        value("decoder base allocated state"),
        value("decoder maximum allocated state"),
        value("decoder maximum allocated state for 8-bit output"),
    )


def main():
    for option, window in (("--memory", 32768), ("--memory-64k", 65536)):
        base9, maximum9, maximum8 = memory_report(option)
        assert maximum9 >= base9
        assert maximum9 - base9 == 24600
        assert maximum9 - maximum8 == window
        print(
            f"PASS {option}: 9-bit maximum {maximum9} bytes; "
            f"8-bit maximum {maximum8} bytes; ring saving {window} bytes"
        )


if __name__ == "__main__":
    main()
