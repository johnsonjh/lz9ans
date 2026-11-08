#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

"""Check 64-bit input-size discovery and 32-bit stream-count limits."""

import ctypes
import os
from pathlib import Path
import subprocess
import tempfile


def make_sparse_file(path, size):
    """Create a sparse file where supported, without writing its contents."""
    with open(path, "wb") as stream:
        if os.name == "nt":
            import msvcrt
            from ctypes import wintypes

            fsctl_set_sparse = 0x000900C4
            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            device_io_control = kernel32.DeviceIoControl
            device_io_control.argtypes = (
                wintypes.HANDLE,
                wintypes.DWORD,
                wintypes.LPVOID,
                wintypes.DWORD,
                wintypes.LPVOID,
                wintypes.DWORD,
                ctypes.POINTER(wintypes.DWORD),
                wintypes.LPVOID,
            )
            device_io_control.restype = wintypes.BOOL
            returned = wintypes.DWORD()
            handle = msvcrt.get_osfhandle(stream.fileno())
            if not device_io_control(
                wintypes.HANDLE(handle),
                fsctl_set_sparse,
                None,
                0,
                None,
                0,
                ctypes.byref(returned),
                None,
            ):
                raise OSError(ctypes.get_last_error(), "FSCTL_SET_SPARSE failed")

        stream.truncate(size)

    if path.stat().st_size != size:
        raise AssertionError("sparse file size was not set correctly")


def invoke(binary, mode, source, output):
    return subprocess.run(
        [binary, mode, str(source), str(output), "-v", "-1"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=60,
        check=False,
    )


def main():
    binary = str(Path(os.environ.get("LZ9ANS_BIN", "./lz9ans")).resolve())

    # These lengths exceed the 32-bit stream symbol-count limit.  c9's valid
    # packed representation of 2^32 symbols is larger than 4 GiB, so this also
    # verifies that the physical byte count and its conversion are 64-bit.
    cases = (
        ("c8", (1 << 32) + 1, "c8-too-many-symbols.bin"),
        ("c9", ((1 << 32) * 9 + 7) // 8, "c9-too-many-symbols.bin"),
    )

    with tempfile.TemporaryDirectory(prefix="lz9ans-large-files-") as temporary:
        directory = Path(temporary)

        for mode, size, filename in cases:
            source = directory / filename
            output = directory / (filename + ".lz9a")

            try:
                make_sparse_file(source, size)
            except OSError as exc:
                if os.name == "nt":
                    print(
                        "SKIP: Windows filesystem cannot create sparse "
                        ">4-GiB test files: {}".format(exc)
                    )
                    return
                raise

            result = invoke(binary, mode, source, output)
            report = result.stderr.decode("utf-8", "replace")
            if result.returncode != 2 or "Invalid input size." not in report:
                raise AssertionError(
                    "{} {}-byte input was not rejected at the 32-bit "
                    "symbol-count limit (rc={}):\n{}".format(
                        mode, size, result.returncode, report
                    )
                )
            if "Cannot determine input size" in report:
                raise AssertionError(
                    "64-bit file size was not discovered for {}".format(mode)
                )
            if output.exists():
                raise AssertionError(
                    "output was created before oversized input rejection"
                )
    print(
        "PASS 64-bit file sizes, packed-9 size arithmetic, "
        "and 32-bit count rejection"
    )


if __name__ == "__main__":
    main()
